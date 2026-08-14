#
# Copyright (C) 2020  Anthony Doud & Joel Baranick
# All rights reserved
#
# SPDX-License-Identifier: GPL-2.0-only
#

Import("env")

from pathlib import Path
from shutil import copy2


def copy_s3_artifact(source, target, env):
    artifact = Path(str(target[0]))
    prefixed = artifact.with_name(f"S3{artifact.name}")
    copy2(artifact, prefixed)
    print(f"[name_build_artifacts] created {prefixed}")


if env.subst("$PIOENV") in ("S3release", "S3debug"):
    # Make the upload and uploadfs targets consume the target-specific images
    # directly instead of renaming their inputs after PlatformIO creates them.
    env.Replace(PROGNAME="S3firmware", ESP32_FS_IMAGE_NAME="S3littlefs")

    # The ESP32 platform hard-codes these two intermediate names in its flash
    # dependency list, so keep them available for `upload` while also creating
    # the target-specific release artifacts.
    env.AddPostAction("$BUILD_DIR/partitions.bin", copy_s3_artifact)
    env.AddPostAction("$BUILD_DIR/bootloader.bin", copy_s3_artifact)
