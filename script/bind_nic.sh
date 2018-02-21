#! /bin/bash
set -e

BESS_HOME="${BESS_HOME:-$HOME/bess}"

DEVICE_ID=00:04.0

# Install drivers
if ! lsmod | grep -q '^uio'; then
	sudo modprobe uio
fi


if ! lsmod | grep -q '^igb_uio'; then
	sudo insmod $BESS_HOME/deps/dpdk-17.05/build/kmod/igb_uio.ko
fi

# Bind NIC to driver
sudo $BESS_HOME/bin/dpdk-devbind.py --bind=igb_uio $DEVICE_ID

#Print current NIC binding status
$BESS_HOME/bin/dpdk-devbind.py --status
