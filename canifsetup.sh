#!/bin/bash
# load kernel modules
sudo modprobe can
sudo modprobe vcan
# add and activate vcan0 network interface
sudo ip link add dev vcan0 type vcan
sudo ip link set up vcan0