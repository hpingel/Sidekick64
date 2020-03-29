#!/bin/bash

#FIXME: we have to find a solution for these two lines on linux
#64tass --nostart cart.a --output=launch.cbm80
#64tass --nostart cart_ultimax.a --output=launch_ultimax.cbm80

cc65 -v -t c64 -T -O --static-locals rpimenu.c
ca65 -v -t c64 rpimenu_sub.s
ca65 -v -t c64 rpimenu.s
#ld65 -C ..\cfg\c64.cfg -o rpimenu.prg rpimenu.o rpimenu_sub.o -L ..\lib --lib c64.lib
#ld65 -C /usr/share/cc65/cfg/c64.cfg -o rpimenu.prg rpimenu.o rpimenu_sub.o -L ..\lib --lib c64.lib
#let's see if this works
ld65 -v -t c64 -o rpimenu.prg rpimenu.o rpimenu_sub.o -L ..\lib --lib c64.lib
