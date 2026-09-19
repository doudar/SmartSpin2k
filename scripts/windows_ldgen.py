#
# Copyright (C) 2020  Anthony Doud & Joel Baranick
# All rights reserved
#
# SPDX-License-Identifier: GPL-2.0-only
#

"""Avoid cmd.exe's 8191-character limit for ESP-IDF linker fragments."""

Import("env")

import os
import subprocess


if os.name == "nt":
    original_spawn = env["SPAWN"]

    def spawn_ldgen(sh, escape, cmd, args, env):
        # The pinned platform runs Python's ldgen.py with every fragment's
        # absolute path. SCons already quotes these arguments for Windows.
        # Pass that command line directly to CreateProcess, preserving its
        # quoting and build environment without cmd.exe's smaller limit.
        if len(args) > 1 and os.path.basename(args[1].strip('"')) == "ldgen.py":
            return subprocess.call(" ".join(args), env=env, shell=False)
        return original_spawn(sh, escape, cmd, args, env)

    env.Replace(SPAWN=spawn_ldgen)
