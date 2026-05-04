#
# Copyright (C) 2020  Anthony Doud & Joel Baranick
# All rights reserved
#
# SPDX-License-Identifier: GPL-2.0-only
#

Import("env")

from pathlib import Path


# Work around intermittent malformed x509_crt_bundle.S generation.
# Removing stale generated files before each build avoids carrying
# corrupted artifacts between runs.
build_dir = Path(env.subst("$BUILD_DIR"))
for file_name in ("x509_crt_bundle", "x509_crt_bundle.S"):
    generated = build_dir / file_name
    if generated.exists():
        generated.unlink()
        print(f"[pre_build_cleanup] removed stale {generated}")

# Keep build deterministic for generated asm artifacts.
env.SetOption("num_jobs", 1)
