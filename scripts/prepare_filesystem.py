# Copyright (C) 2026 Anthony Doud & Joel Baranick
# SPDX-License-Identifier: GPL-2.0-only

"""Stage compact LittleFS contents before PlatformIO filesystem builds."""

Import("env")

import gzip
import json
import shutil
from pathlib import Path

from SCons.Script import COMMAND_LINE_TARGETS


FILESYSTEM_TARGETS = {"buildfs", "uploadfs", "uploadfsota"}
COMPRESSED_SUFFIXES = {".html", ".css"}


def update_repository_ota_assets(source_dir: Path) -> None:
    """Keep gzip files used by raw-GitHub automatic OTA in sync."""
    output_files = []
    expected_gzip_files = set()
    source_files = sorted(path for path in source_dir.rglob("*") if path.is_file())
    for source_file in source_files:
        relative_path = source_file.relative_to(source_dir)
        if relative_path.as_posix() == "list.json":
            continue
        if source_file.suffix == ".gz" and source_file.with_suffix("").suffix in COMPRESSED_SUFFIXES:
            continue

        if source_file.suffix in COMPRESSED_SUFFIXES:
            gzip_file = source_file.with_name(source_file.name + ".gz")
            gzip_file.write_bytes(gzip.compress(source_file.read_bytes(), compresslevel=9, mtime=0))
            relative_path = gzip_file.relative_to(source_dir)
            expected_gzip_files.add(gzip_file)
        output_files.append(relative_path.as_posix())

    for gzip_file in source_dir.rglob("*.gz"):
        if gzip_file.with_suffix("").suffix in COMPRESSED_SUFFIXES and gzip_file not in expected_gzip_files:
            gzip_file.unlink()

    (source_dir / "list.json").write_text(json.dumps(output_files, separators=(",", ":")), encoding="utf-8")


def stage_filesystem(source_dir: Path, output_dir: Path) -> None:
    if output_dir.exists():
        shutil.rmtree(output_dir)
    output_dir.mkdir(parents=True)

    output_files = []
    for source_file in sorted(path for path in source_dir.rglob("*") if path.is_file()):
        relative_path = source_file.relative_to(source_dir)
        if relative_path.as_posix() == "list.json":
            continue

        # Gzip copies checked into data/ are for repository-based automatic OTA.
        # Always regenerate filesystem copies from the readable source files.
        if source_file.suffix == ".gz" and source_file.with_suffix("").suffix in COMPRESSED_SUFFIXES:
            continue

        if source_file.suffix in COMPRESSED_SUFFIXES:
            relative_path = Path(relative_path.as_posix() + ".gz")
            destination = output_dir / relative_path
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes(gzip.compress(source_file.read_bytes(), compresslevel=9, mtime=0))
        else:
            destination = output_dir / relative_path
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source_file, destination)

        output_files.append(relative_path.as_posix())

    print(f"[prepare_filesystem] staged {len(output_files)} files in {output_dir}")


if FILESYSTEM_TARGETS.intersection(COMMAND_LINE_TARGETS):
    project_dir = Path(env.subst("$PROJECT_DIR"))
    build_dir = Path(env.subst("$BUILD_DIR"))
    source_name = "data_s3" if env.subst("$PIOENV") in ("S3release", "S3debug") else "data"
    staged_dir = build_dir / "filesystem_data"
    source_dir = project_dir / source_name
    update_repository_ota_assets(source_dir)
    stage_filesystem(source_dir, staged_dir)
    env.Replace(PROJECT_DATA_DIR=str(staged_dir))
