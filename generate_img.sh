#!/bin/bash

rm -rf iso_root

git clone https://github.com/limine-bootloader/limine.git --branch=v9.x-binary --depth=1

make -C limine

mkdir -p iso_root/EFI/BOOT
cp limine/BOOTX64.EFI iso_root/EFI/BOOT/

cp limine.conf iso_root/  # copy limine.conf
cp $1 iso_root/  # copy kernel elf

mkimg --config=mkimg_aloe.toml
