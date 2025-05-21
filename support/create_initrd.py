#! /usr/bin/env python
import os
import shutil
import subprocess
import tarfile


def create_init_program():
    print("Creating init program...")
    subprocess.run(["make"], cwd="init", stdout=subprocess.DEVNULL, check=True)
    shutil.move("init/init.elf", "build/initrd/")

def archive_initrd():
    print("Archiving initrd...")

    with tarfile.open("build/aloe.initrd", "w", format=tarfile.USTAR_FORMAT) as tar:
        for entry in os.listdir("build/initrd"):
            print("Archiving " + entry)
            tar.add("build/initrd/" + entry, arcname=entry)

    shutil.rmtree("build/initrd")

os.mkdir("build/initrd")

create_init_program()
archive_initrd()

print("Done")
