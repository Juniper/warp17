#!/bin/bash
# This script create two different tap interfaces with a bridge

set -e

iface1="tap0"
iface2="tap1"

ip link add warp_bridge type bridge
ip tuntap add $iface1 mode tap user root
ip tuntap add $iface2 mode tap user root
ip link set $iface1 up
ip link set $iface2 up
ip link set $iface1 master warp_bridge
ip link set $iface2 master warp_bridge
ip link set warp_bridge up
