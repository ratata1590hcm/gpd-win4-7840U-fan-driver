# Makefile — builds gpd-fan.ko for GPD Win 4 7840U/8840U
# Works with source file named gpd-fan.c or gpd-win4-fan.c

obj-m += gpd-fan.o

# Force the final module filename to be gpd-fan.ko
gpd-fan-objs := $(obj-m:.o=.c:%.c=%.o)

KERNEL_SRC ?= /lib/modules/$(shell uname -r)/build
PWD        := $(shell pwd)

all: modules

modules:
	$(MAKE) -C $(KERNEL_SRC) M=$(PWD) modules

modules_install:
	$(MAKE) -C $(KERNEL_SRC) M=$(PWD) modules_install

clean:
	$(MAKE) -C $(KERNEL_SRC) M=$(PWD) clean

install: modules modules_install
	depmod -a
	-modprobe gpd-fan 2>/dev/null || true
	modprobe gpd-fan

uninstall:
	-rmmod gpd-fan 2>/dev/null || true
	rm -f /lib/modules/$(shell uname -r)/extra/gpd-fan.ko*

help:
	@echo "make          → build gpd-fan.ko"
	@echo "make install  → build + install + load"
	@echo "make uninstall→ remove module"
	@echo "make clean    → clean"

.PHONY: all modules modules_install clean install uninstall help