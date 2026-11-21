#
# Copyright (C) 2020  Anthony Doud & Joel Baranick
# All rights reserved
#
# SPDX-License-Identifier: GPL-2.0-only
#

Import("env")

from pathlib import Path
from SCons.Script import COMMAND_LINE_TARGETS


# Work around intermittent malformed x509_crt_bundle.S generation.
# Leave artifacts alone during IDE inspection and let clean remove its own files.
if not env.IsIntegrationDump() and not env.IsCleanTarget() and "envdump" not in COMMAND_LINE_TARGETS:
    build_dir = Path(env.subst("$BUILD_DIR"))
    for file_name in ("x509_crt_bundle", "x509_crt_bundle.S"):
        generated = build_dir / file_name
        if generated.exists():
            generated.unlink()
            print(f"[pre_build_cleanup] removed stale {generated}")
