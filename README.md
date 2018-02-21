MK2000 Kernel
=============

### Some links
* [LG G5] XDA thread.
* [LG V20] XDA thread.
* [LG G6] XDA thread.

This kernel is for the following LG DEVICES:
-----------------------------------------------
### G5
- H850 --- International (Global)

* H830 --- T-Mobile (US)

* RS988 --- Unlocked (US)

### V20
- H910 --- AT&T (US)

* H918 --- T-Mobile (US)

* US996 --- US Cellular & Unlocked (US)

* US996Santa --- US Cellular & Unlocked (US)
  * Unlocked with Kernel Exploit

* VS995 --- Verizon (US)

* H990DS --- International (Global)
  * For H990 (Single SIM) you'll need to change some cmdline settings in the device config

* LS997 --- Sprint (US)
  * Needs updated defconfig

### G6
- H870 --- International (Global)

* US997 --- US Cellular & Unlocked (US)

## COMPILE THE KERNEL

### Clone
	git clone https://github.com/stendro/msm8996_lge_kernel.git -b mk2k-gold

### Build
	./build.sh "DEVICE" && ./copy_finished.sh

* "DEVICE" will be one of the above names (case sensitive).

## A bit of info

In the mk2k-gold branch I have upstreamed kernel manually.
There where many corrections and catchup patches applied. Do a search for "edit:" to see most of the commits that I've changed.

Currently I'm working on the mk2k-platinum branch, this is a rebase onto the v11g G6 source.
The mk2k-gold branch has some problems with usb. I reverted few CAF patches and took gadget.c (drivers/usb/dwc3) up to linux stable.
This seemed to have fixed a fast-charging issue that caused it to drop out randomly, but introduced a mtp/pc connection bug.

G6: On this (platinum) branch, G6 will not boot for some unknown reason. To build for G6 see my [G6 Kernel] repository.

[LG G5]: <https://forum.xda-developers.com/lg-g5/development/h850-mk2000-kernel-t3707822>
[LG V20]: <https://forum.xda-developers.com/v20/development/h918-h910-us996-ucl-mk2000-kernel-t3708330>
[LG G6]: <https://forum.xda-developers.com/lg-g6/development/us997-h870-mk2000-kernel-t3739494>
[G6 Kernel]: <https://github.com/stendro/lge_g6_kernel/tree/mk2k-g6>
