// SPDX-License-Identifier: GPL-2.0+
/*
 * GPD Win 4 (7840U / 8840U) Fan Control Driver
 * Clean version - 7840U ONLY
 *
 * Based on Cryolitia's original work
 * https://github.com/Cryolitia/gpd-fan-driver
 */

#include <linux/dmi.h>
#include <linux/hwmon.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#define DRIVER_NAME "gpd-win4-7840u-fan"

/* EC ports used on 7840U+ Win 4 */
#define EC_ADDR_PORT    0x4E
#define EC_DATA_PORT    0x4F

/* Register map (same as Win Max 2) */
#define REG_MANUAL_ENABLE   0x0275   /* 0 = auto, 1 = manual */
#define REG_RPM_HIGH        0x0218
#define REG_RPM_LOW         0x0219
#define REG_PWM             0x1809   /* 1..184 → scaled from 0-255 */
#define PWM_MAX_VAL         184      /* EC uses 1 to 184 */

static DEFINE_MUTEX(ec_lock);

struct gpd_fan_data {
    u8 pwm_value;       /* 0-255 */
    bool manual_mode;
};

static struct gpd_fan_data fan_data = {
    .pwm_value = 255,
    .manual_mode = false,
};

/* Low-level EC RAM access */
static void ec_write(u16 addr, u8 val)
{
    outb(0x2E, EC_ADDR_PORT);
    outb(0x11, EC_DATA_PORT);
    outb(0x2F, EC_ADDR_PORT);
    outb(addr >> 8, EC_DATA_PORT);
    outb(0x2E, EC_ADDR_PORT);
    outb(0x10, EC_DATA_PORT);
    outb(0x2F, EC_ADDR_PORT);
    outb(addr & 0xFF, EC_DATA_PORT);
    outb(0x2E, EC_ADDR_PORT);
    outb(0x12, EC_DATA_PORT);
    outb(0x2F, EC_ADDR_PORT);
    outb(val, EC_DATA_PORT);
}

static u8 ec_read(u16 addr)
{
    u8 val;
    outb(0x2E, EC_ADDR_PORT);
    outb(0x11, EC_DATA_PORT);
    outb(0x2F, EC_ADDR_PORT);
    outb(addr >> 8, EC_DATA_PORT);
    outb(0x2E, EC_ADDR_PORT);
    outb(0x10, EC_DATA_PORT);
    outb(0x2F, EC_ADDR_PORT);
    outb(addr & 0xFF, EC_DATA_PORT);
    outb(0x2E, EC_ADDR_PORT);
    outb(0x12, EC_DATA_PORT);
    outb(0x2F, EC_ADDR_PORT);
    val = inb(EC_DATA_PORT);
    return val;
}

/* Scale 0-255 → 1-184 */
static u8 scale_pwm(u8 val)
{
    if (val >= 255) return PWM_MAX_VAL;
    if (val == 0)   return 1;
    return 1 + DIV_ROUND_CLOSEST(val * (PWM_MAX_VAL - 1), 255);
}

/* Read fan RPM */
static int read_rpm(void)
{
    u16 rpm;
    mutex_lock(&ec_lock);
    rpm = (ec_read(REG_RPM_HIGH) << 8) | ec_read(REG_RPM_LOW);
    mutex_unlock(&ec_lock);
    return rpm;
}

/* Set fan speed */
static void set_fan_speed(u8 pwm_255)
{
    u8 scaled = scale_pwm(pwm_255);
    mutex_lock(&ec_lock);
    ec_write(REG_PWM, scaled);
    ec_write(REG_MANUAL_ENABLE, 1);   /* Force manual mode */
    mutex_unlock(&ec_lock);
    fan_data.pwm_value = pwm_255;
}

/* Switch to auto mode */
static void set_auto_mode(void)
{
    mutex_lock(&ec_lock);
    ec_write(REG_MANUAL_ENABLE, 0);
    mutex_unlock(&ec_lock);
    fan_data.manual_mode = false;
}

/* hwmon callbacks */
static umode_t fan_is_visible(const void *data, enum hwmon_sensor_types type,
                              u32 attr, int channel)
{
    if (type == hwmon_fan && attr == hwmon_fan_input)
        return 0444;
    if (type == hwmon_pwm) {
        if (attr == hwmon_pwm_enable || attr == hwmon_pwm_input)
            return 0644;
    }
    return 0;
}

static int fan_read(struct device *dev, enum hwmon_sensor_types type,
                    u32 attr, int channel, long *val)
{
    if (type == hwmon_fan && attr == hwmon_fan_input) {
        *val = read_rpm();
        return 0;
    }
    if (type == hwmon_pwm) {
        if (attr == hwmon_pwm_enable) {
            *val = fan_data.manual_mode ? 1 : 0;
            return 0;
        }
        if (attr == hwmon_pwm_input) {
            *val = fan_data.manual_mode ? fan_data.pwm_value : 0;
            return 0;
        }
    }
    return -EOPNOTSUPP;
}

static int fan_write(struct device *dev, enum hwmon_sensor_types type,
                     u32 attr, int channel, long val)
{
    if (type != hwmon_pwm)
        return -EOPNOTSUPP;

    if (attr == hwmon_pwm_enable) {
        if (val < 0 || val > 1)
            return -EINVAL;
        if (val == 1) {
            fan_data.manual_mode = true;
            set_fan_speed(fan_data.pwm_value);  /* apply current value */
        } else {
            set_auto_mode();
        }
        return 0;
    }

    if (attr == hwmon_pwm_input) {
        if (val < 0 || val > 255)
            return -EINVAL;
        fan_data.pwm_value = val;
        if (fan_data.manual_mode)
            set_fan_speed(val);
        return 0;
    }

    return -EOPNOTSUPP;
}

static const struct hwmon_ops fan_ops = {
    .is_visible = fan_is_visible,
    .read       = fan_read,
    .write      = fan_write,
};

static const struct hwmon_channel_info *fan_info[] = {
    HWMON_CHANNEL_INFO(fan, HWMON_F_INPUT),
    HWMON_CHANNEL_INFO(pwm, HWMON_PWM_ENABLE | HWMON_PWM_INPUT),
    NULL
};

static const struct hwmon_chip_info chip_info = {
    .ops  = &fan_ops,
    .info = fan_info,
};

/* DMI match - GPD Win 4 7840U/8840U */
static const struct dmi_system_id gpd_win4_7840u[] = {
    {
        .matches = {
            DMI_MATCH(DMI_SYS_VENDOR, "GPD"),
            DMI_MATCH(DMI_PRODUCT_NAME, "G1618-04"),
        },
        .driver_data = NULL,
    },
    { }
};

static int gpd_probe(struct platform_device *pdev)
{
    struct device *hwmon;

    if (!dmi_check_system(gpd_win4_7840u))
        return -ENODEV;

    hwmon = devm_hwmon_device_register_with_info(&pdev->dev, DRIVER_NAME,
                                                 NULL, &chip_info, NULL);
    if (IS_ERR(hwmon))
        return PTR_ERR(hwmon);

    dev_info(&pdev->dev, "GPD Win 4 7840U/8840U fan control loaded\n");
    return 0;
}

static void gpd_remove(struct platform_device *pdev)
{
    set_auto_mode();  /* Return control to BIOS */
}

static struct platform_driver gpd_driver = {
    .probe  = gpd_probe,
    .remove = gpd_remove,
    .driver = {
        .name = DRIVER_NAME,
    },
};

static struct platform_device *gpd_plat_dev;

static int __init gpd_init(void)
{
    struct resource res = {
        .start = EC_ADDR_PORT,
        .end   = EC_DATA_PORT,
        .flags = IORESOURCE_IO,
    };

    if (!dmi_check_system(gpd_win4_7840u))
        return -ENODEV;

    gpd_plat_dev = platform_create_bundle(&gpd_driver, gpd_probe,
                                          &res, 1, NULL, 0);
    return PTR_ERR_OR_ZERO(gpd_plat_dev);
}

static void __exit gpd_exit(void)
{
    platform_device_unregister(gpd_plat_dev);
    platform_driver_unregister(&gpd_driver);
}

module_init(gpd_init);
module_exit(gpd_exit);

MODULE_DEVICE_TABLE(dmi, gpd_win4_7840u);
MODULE_AUTHOR("Based on Cryolitia <Cryolitia@gmail.com>");
MODULE_DESCRIPTION("GPD Win 4 (7840U/8840U) fan control driver");
MODULE_LICENSE("GPL");