#!/bin/sh
killall led

./build/devices/led/led &
sleep 2
./build/devices/led-gpio/led-gpio /tmp/led-gpio.sock &
