#!/bin/bash
rm cart64_16k.o rpimenu_sub.o main.o menu.bin crt0.o main.s c64-cart.lib c64-cart.lib.temp
#patching c64.lib

CC65_C64_LIB="/usr/share/cc65/lib/c64.lib"

#if [ ! -f "${CC65_C64_LIB}" ]; then


if [ ! -f "${CC65_C64_LIB}" ]; then
	echo "Install cc65, can't find ${CC65_C64_LIB}!"
	exit 0
fi

cp ${CC65_C64_LIB} ./c64-cart.lib

ar65 d c64-cart.lib crt0.o
ca65 cart64_16k.s
ar65 a c64-cart.lib cart64_16k.o

cc65 -t c64 -Cl -T main.c 
ca65 -t c64 main.s
ca65 -t c64 rpimenu_sub.s
ld65 -v -vm -m cart.map --config cart64_16k.cfg -o menu.bin main.o rpimenu_sub.o cart64_16k.o c64-cart.lib
