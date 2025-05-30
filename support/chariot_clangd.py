#!/usr/bin/env python

import sys
import os
from os.path import dirname, abspath
import subprocess



CHARIOT_CACHE_PATH = "/home/vincent/projects/ember-cache"

project_path = dirname(dirname(abspath(__file__)))
recipe_source_path = os.getcwd()

if len(sys.argv) < 3:
    print("Usage: chariot_clangd.py <package> <source name>")
    sys.exit(1)

mappings = [
    f"{recipe_source_path}=$SOURCES_DIR/{sys.argv[2]}"
]

result = subprocess.run([
    "chariot",
    "--config", project_path + "/config.chariot",
    "--cache", CHARIOT_CACHE_PATH,
    "--no-lockfile",
    "exec", "--rw",
    "--recipe-context", sys.argv[1],
    "-p", "clangd",
    "-e", "HOME=/root/clangd",
    "-e", "XDG_CACHE_HOME=/root/clangd/cache",
    "-m", recipe_source_path + "=" + "/chariot/sources/" + sys.argv[2] + ":ro",
    f"clangd --background-index --clang-tidy --header-insertion=iwyu --path-mappings {",".join(mappings)}"
])
