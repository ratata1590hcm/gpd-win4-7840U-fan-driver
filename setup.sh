#!/bin/bash
sudo insmod ./gpd-fan.ko
sudo chmod 777 gpd_fan_set
sudo cp -rf gpd_fan_set /usr/local/bin/