MK2000 Kernel
=============

### Some links
* [LG G5] XDA thread.
* [LG V20] XDA thread.
* [LG G6] XDA thread (uses a different source, it's in my repo).

This kernel is for the following LG DEVICES:
-----------------------------------------------
### G5
- H850		International (Global)

* H830		T-Mobile (US)

* RS988		Unlocked (US)

### V20
- H910		AT&T (US)

* H918		T-Mobile (US)

* US996		US Cellular & Unlocked (US)

* US996Santa	US Cellular & Unlocked (US)
  * Unlocked with Kernel Exploit

* VS995		Verizon (US)

* H990		International (Global)

* LS997		Sprint (US)
  * Needs updated defconfig

## COMPILE THE KERNEL

### Clone
	git clone https://github.com/stendro/msm8996_lge_kernel.git -b mk2k-gold

### Build
	./build.sh "DEVICE" && ./copy_finished.sh

* "DEVICE" will be one of the above names (case sensitive).

## A bit of info

In the mk2k-gold branch I have upstreamed kernel manually.
There where many corrections and catchup patches applied. Do a search for "edit:" to see most of the commits that I've changed.

[LG G5]: <https://forum.xda-developers.com/lg-g5/development/h850-mk2000-kernel-t3707822>
[LG V20]: <https://forum.xda-developers.com/v20/development/h918-h910-us996-ucl-mk2000-kernel-t3708330>
[LG G6]: <https://forum.xda-developers.com/lg-g6/development/us997-h870-mk2000-kernel-t3739494>
