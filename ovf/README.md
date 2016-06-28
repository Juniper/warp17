# WARP17 Virtual Machine

## Table of Contents
  * [Introduction](#chapter-1)
  * [Using with VMware](#chapter-2)
  * [Adjusting Huge Pages](#chapter-3)
  * [Using with qemu](#chapter-4)
  * [Using with qemu and VT-d](#chapter-5)


## Introduction<a id="chapter-1"></a>
This virtual machine was exported from VMware Fusion, having 4 cores and 16G of
memory allocated. It’s based on Ubuntu Server 14.04 and should work on most VM hosts.
Further down there is an example on how to get it up and running
using qemu, including how to use it in VT-d mode.

But first a note on using the VM basic drivers, they only support a single
receive and transmit queue in DPDK, so only one core per port is supported.
This does not give any decent performance, but its good enough to check out its
capabilities.

In addition because the VM driver does not support hardware based IP/TCP/UDP
checksums, they are not verified/generated, and therefore the VM nic will only work
in a back2back configuration. If you do want to test it with a router in
between you could force IP checksum generating in ipv4_build_ipv4_hdr(), and
change the “if (true)” to “if (false)”.

**NOTE:** The following patch was applied to get DPDK working in combination
with the VMware virtual e1000 driver,
“http://dpdk.org/dev/patchwork/patch/945/“.

The examples below assume interface eth0 is used for management, and eth1
and eth2 need to be connected back to back in your hypervisor.


## Using with VMware<a id="chapter-2"></a>
Fire up your hypervisor and log into the vm using it’s assigned IP address on
eth0, with both the user name and password being “warp17”.

For example:

```
$ ssh warp17@192.168.255.131
warp17@192.168.255.131's password:

warp17@warp17:~$
```

Now we need to setup the two interfaces eth1 and eth2 to be used by DPDK:

```
warp17@warp17:~$ $RTE_SDK/tools/dpdk_nic_bind.py --status

Network devices using DPDK-compatible driver
============================================
<none>

Network devices using kernel driver
===================================
0000:02:01.0 '82545EM Gigabit Ethernet Controller (Copper)' if=eth0 drv=e1000 unused=igb_uio *Active*
0000:02:02.0 '82545EM Gigabit Ethernet Controller (Copper)' if=eth1 drv=e1000 unused=igb_uio
0000:02:03.0 '82545EM Gigabit Ethernet Controller (Copper)' if=eth2 drv=e1000 unused=igb_uio

Other network devices
=====================
<none>
```

```
warp17@warp17:~$ sudo $RTE_SDK/tools/dpdk_nic_bind.py -b igb_uio 02:02.0
warp17@warp17:~$ sudo $RTE_SDK/tools/dpdk_nic_bind.py -b igb_uio 02:03.0
```

```
warp17@warp17:~$ $RTE_SDK/tools/dpdk_nic_bind.py --status

Network devices using DPDK-compatible driver
============================================
0000:02:02.0 '82545EM Gigabit Ethernet Controller (Copper)' drv=igb_uio unused=
0000:02:03.0 '82545EM Gigabit Ethernet Controller (Copper)' drv=igb_uio unused=

Network devices using kernel driver
===================================
0000:02:01.0 '82545EM Gigabit Ethernet Controller (Copper)' if=eth0 drv=e1000 unused=igb_uio *Active*

Other network devices
=====================
<none>
```

We are ready to run the example(s) supplied with WARP17:

```
sudo ./warp17/build/warp17 -c f -m 12288 -- --qmap-default=max-c \
	    --tcb-pool-sz 20 \
	    --cmd-file /home/warp17/warp17/examples/test_4_http_10M_sessions.cfg
```


## Adjusting Huge Pages<a id="chapter-3"></a>
For this VM we set aside 12G of memory for huge pages, however if you have more
or less memory available you could change it as follows:

Edit /etc/default/grub, and changes the number of hugepages in the below
statement:

```
GRUB_CMDLINE_LINUX="default_hugepagesz=1G hugepagesz=1G hugepages=12”
```

Once this is done, update grub, and reboot;

```
	sudo update-grub
	sudo reboot
```


## Using with qemu<a id="chapter-4"></a>
Unpack the .ova image on your host:

```
tar -xvf WARP17.ova
```

Convert the image to be used with qemu:

```
qemu-img convert -O qcow2 -c WARP17-disk1.vmdk WARP17-disk1.qcow2
```

Setup the tap interfaces to bridge between the two interfaces:

```
sudo ip link add warp_bridge type bridge
sudo ip tuntap add tap01 mode tap user root
sudo ip tuntap add tap02 mode tap user root
sudo ip link set tap01 up
sudo ip link set tap02 up
sudo ip link set tap01 master warp_bridge
sudo ip link set tap02 master warp_bridge
```

Start the VM:

```
sudo qemu-system-x86_64 -enable-kvm -cpu host -smp 4 -hda WARP17-disk1.qcow2 \
       -m 16384 -display vnc=:0 -redir tcp:2222::22 \
       -net nic,model=e1000 -net user,name=mynet0 \
       -net nic,model=e1000,macaddr=DE:AD:1E:00:00:01 \
       -net tap,ifname=tap01,script=no,downscript=no \
       -net nic,model=e1000,macaddr=DE:AD:1E:00:00:02 \
       -net tap,ifname=tap02,script=no,downscript=no
```

**NOTE:** you can either start a VNC session to your local host, or do
<code>ssh -p 2222 warp17@localhost</code> to connect to the VM.


Now we need to setup the two interfaces eth1 and eth2 to be used by DPDK:

```
warp17@warp17:~$ $RTE_SDK/tools/dpdk_nic_bind.py --status

Network devices using DPDK-compatible driver
============================================
<none>

Network devices using kernel driver
===================================
0000:00:03.0 '82540EM Gigabit Ethernet Controller' if=eth0 drv=e1000 unused=igb_uio *Active*
0000:00:04.0 '82540EM Gigabit Ethernet Controller' if=eth1 drv=e1000 unused=igb_uio
0000:00:05.0 '82540EM Gigabit Ethernet Controller' if=eth2 drv=e1000 unused=igb_uio

Other network devices
=====================
<none>
```

```
warp17@warp17:~$ sudo $RTE_SDK/tools/dpdk_nic_bind.py -b igb_uio 00:04.0
warp17@warp17:~$ sudo $RTE_SDK/tools/dpdk_nic_bind.py -b igb_uio 00:05.0
```

```
warp17@warp17:~$ $RTE_SDK/tools/dpdk_nic_bind.py --status


Network devices using DPDK-compatible driver
============================================
0000:00:04.0 '82540EM Gigabit Ethernet Controller' drv=igb_uio unused=
0000:00:05.0 '82540EM Gigabit Ethernet Controller' drv=igb_uio unused=

Network devices using kernel driver
===================================
0000:00:03.0 '82540EM Gigabit Ethernet Controller' if=eth0 drv=e1000 unused=igb_uio *Active*

Other network devices
=====================
<none>
```

We are ready to run the example(s) supplied with WARP17:

```
sudo ./warp17/build/warp17 -c f -m 12288 -- --qmap-default=max-c \
	    --tcb-pool-sz 20 \
	    --cmd-file /home/warp17/warp17/examples/test_4_http_10M_sessions.cfg
```



## Using with qemu and VT-d<a id="chapter-5"></a>

Get PCI addresses for the interfaces you would like to use, in our case two XL710s:

```
$ lspci -nn | grep XL710
81:00.0 Ethernet controller [0200]: Intel Corporation Ethernet Controller XL710 for 40GbE QSFP+ [8086:1584] (rev 02)
82:00.0 Ethernet controller [0200]: Intel Corporation Ethernet Controller XL710 for 40GbE QSFP+ [8086:1584] (rev 02)
```

Load pci stub and set it up to use the two above interface:

```
sudo su
modprobe pci-stub
echo "8086 1584" > /sys/bus/pci/drivers/pci-stub/new_id
echo 0000:81:00.0 > /sys/bus/pci/devices/0000:81:00.0/driver/unbind
echo 0000:82:00.0 > /sys/bus/pci/devices/0000:82:00.0/driver/unbind
echo 0000:81:00.0 > /sys/bus/pci/drivers/pci-stub/bind
echo 0000:82:00.0 > /sys/bus/pci/drivers/pci-stub/bind
```

Start the VM:

```
qemu-system-x86_64 -enable-kvm -cpu host -smp 4 -hda WARP17-disk1.qcow2 -m 16384 \
  -display vnc=:0 -redir tcp:2222::22 \
  -net nic,model=e1000 -net user,name=mynet0 \
  -device pci-assign,romfile=,host=81:00.0 \
  -device pci-assign,romfile=,host=82:00.0
```

Now we need to setup the two PCI interfaces to be used by DPDK:

```
warp17@warp17:~$ $RTE_SDK/tools/dpdk_nic_bind.py --status

Network devices using DPDK-compatible driver
============================================
<none>

Network devices using kernel driver
===================================
0000:00:03.0 '82540EM Gigabit Ethernet Controller' if=eth0 drv=e1000 unused=igb_uio *Active*
0000:00:04.0 'Ethernet Controller XL710 for 40GbE QSFP+' if=eth1 drv=i40e unused=igb_uio
0000:00:05.0 'Ethernet Controller XL710 for 40GbE QSFP+' if=eth2 drv=i40e unused=igb_uio

Other network devices
=====================
<none>
```

```
warp17@warp17:~$ sudo $RTE_SDK/tools/dpdk_nic_bind.py -b igb_uio 00:04.0
warp17@warp17:~$ sudo $RTE_SDK/tools/dpdk_nic_bind.py -b igb_uio 00:05.0
```

```
warp17@warp17:~$ $RTE_SDK/tools/dpdk_nic_bind.py --status

Network devices using DPDK-compatible driver
============================================
0000:00:04.0 'Ethernet Controller XL710 for 40GbE QSFP+' drv=igb_uio unused=
0000:00:05.0 'Ethernet Controller XL710 for 40GbE QSFP+' drv=igb_uio unused=

Network devices using kernel driver
===================================
0000:00:03.0 '82540EM Gigabit Ethernet Controller' if=eth0 drv=e1000 unused=igb_uio *Active*

Other network devices
=====================
<none>
```

We are ready to run the example(s) supplied with WARP17:

```
sudo ./warp17/build/warp17 -c f -m 12288 -- --qmap-default=max-c \
	    --tcb-pool-sz 20 \
	    --cmd-file /home/warp17/warp17/examples/test_4_http_10M_sessions.cfg
```

### Scaling the configuration

The next example will use the same setup as above, but we scale
the configuration to 16 CPU cores, and 64G of total memory. You also need to
increase the number of huge pages, [see above](#chapter-3).

Note that for the best performance you need to lock the affinity of the virtual
cores to the physical cores, preferable to the same socket as the NICs are
using.

Start the VM:

```
qemu-system-x86_64 -enable-kvm -cpu host -smp 16 -hda WARP17-disk1.qcow2 -m 65536 \
  -display vnc=:0 -redir tcp:2222::22 \
  -net nic,model=e1000 -net user,name=mynet0 \
  -device pci-assign,romfile=,host=81:00.0 \
  -device pci-assign,romfile=,host=82:00.0
```

We are ready to run the example(s) supplied with WARP17:

```
sudo ./warp17/build/warp17 -c ffff -m 32768 -- --qmap-default=max-c \
       --tcb-pool-sz 20\
       --cmd-file /home/warp17/warp17/examples/test_4_http_10M_sessions.cfg
```
