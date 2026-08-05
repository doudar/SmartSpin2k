#
# Copyright (C) 2020  Anthony Doud & Joel Baranick
# All rights reserved
#
# SPDX-License-Identifier: GPL-2.0-only
#

Import("env")

from pathlib import Path


def rename_s3_artifact(source, target, env):
    artifact = Path(str(target[0]))
    prefixed = artifact.with_name(f"S3{artifact.name}")
    artifact.replace(prefixed)
    print(f"[name_build_artifacts] renamed {artifact} to {prefixed}")


if env.subst("$PIOENV") in ("S3release", "S3debug"):
    env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", rename_s3_artifact)
    env.AddPostAction("$BUILD_DIR/littlefs.bin", rename_s3_artifact)
    env.AddPostAction("$BUILD_DIR/partitions.bin", rename_s3_artifact)
    env.AddPostAction("$BUILD_DIR/bootloader.bin", rename_s3_artifact)
