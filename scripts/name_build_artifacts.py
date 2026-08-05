#
# Copyright (C) 2020  Anthony Doud & Joel Baranick
# All rights reserved
#
# SPDX-License-Identifier: GPL-2.0-only
#

Import("env")

from pathlib import Path
from shutil import copy2


def add_s3_prefix(source, target, env):
    artifact = Path(str(target[0]))
    prefixed = artifact.with_name(f"S3{artifact.name}")
    copy2(artifact, prefixed)
    print(f"[name_build_artifacts] created {prefixed}")


if env.subst("$PIOENV") in ("S3release", "S3debug"):
    env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", add_s3_prefix)
    env.AddPostAction("$BUILD_DIR/littlefs.bin", add_s3_prefix)
