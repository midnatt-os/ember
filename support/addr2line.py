#!/usr/bin/env python

import os
import subprocess
import sys

sys.dont_write_bytecode = True
import chariot_utils

CHARIOT_CACHE_PATH = "/home/vincent/projects/aloe-chariot-cache"

if len(sys.argv) < 2:
    print("Usage: addr2line.py <address>")
    sys.exit(1)

address = int(sys.argv[1], 16)

subprocess.run([
    "addr2line", "-fai",
    "-e", os.path.join(chariot_utils.path("package/aloe", "--cache", CHARIOT_CACHE_PATH), "usr/bin/aloe.elf"),
    hex(address)
])
