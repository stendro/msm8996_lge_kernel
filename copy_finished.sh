#!/bin/bash

find build/lib/ -name '*.ko' -exec cp {} out/modules  \;
cp build/arch/arm64/boot/Image out/
cp build/arch/arm64/boot/Image.lz4 out/
cp build/arch/arm64/boot/Image.lz4-dtb out/