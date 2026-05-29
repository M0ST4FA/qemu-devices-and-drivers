#!/bin/sh
killall led
killall lux

./build/devices/led/led &
sleep 2
./build/devices/lux/lux /tmp/lux-f0.sock /tmp/lux-f1.sock &
