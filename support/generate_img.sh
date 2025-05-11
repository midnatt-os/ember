#!/bin/bash


git clone https://github.com/limine-bootloader/limine.git --branch=v9.x-binary --depth=1

make -C limine

mkdir -p build/iso_root/EFI/BOOT
cp limine/BOOTX64.EFI build/iso_root/EFI/BOOT/

rm -rf limine

cp support/limine.conf build/iso_root/  # copy limine.conf
cp $1 build/iso_root/  # copy kernel elf

sym_gen $1 build/iso_root/aloe_symbols.symf
mkimg --config=support/mkimg_aloe.toml

rm -rf build/iso_root
