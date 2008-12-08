#!/bin/sh

echo Installing linuxwacom package...
yum -y install linuxwacom

echo Copying and installing Wacom kernel module...
wget http://dev.laptop.org/~pgf/wacom.ko -O /lib/modules/`uname -r`/kernel/drivers/usb/input/wacom.ko

echo Copying Xorg configuration file...
wget http://dev.laptop.org/~wadeb/xorg-dcon.conf -O /etc/X11/xorg-dcon.conf

echo Wacom installation complete!  Please restart your XO.
