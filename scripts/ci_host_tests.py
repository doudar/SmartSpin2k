#!/usr/bin/env python3
"""Select portable CI jobs and run host tests without silently skipping coverage."""
import argparse
import fnmatch
import importlib
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
SMOKE_TESTS = (
    "test_virtual_ble_ingress.py",
    "test_virtual_ftms.py",
    "test_bike_bench.py",
)
TOOLING_PREFIXES = ("scripts/", "test/", "lib/ArduinoCompat/", ".github/workflows/")
TOOLING_FILES = {
    "platformio.ini", "dependencies.lock", "pyproject.toml", "setup.cfg",
    "setup.py", ".python-version",
}
PYTHON_DEPENDENCIES = ("numpy", "scipy", "matplotlib", "fitdecode", "dbus_next")


def is_tooling_path(path):
    """Git paths are repository-relative POSIX names on every runner OS."""
    return (path.startswith(TOOLING_PREFIXES) or path in TOOLING_FILES
            or ("/" not in path and fnmatch.fnmatchcase(path, "requirements*.txt")))


def tooling_changed(base, head, root=ROOT):
    """Compare explicit PR endpoints, never the checked-out synthetic merge.

    --no-renames exposes both sides of moves, including moves out of tooling.
    Unknown revisions must request smoke jobs rather than silently omit them.
    """
    for label, revision in (("base", base), ("head", head)):
        if not revision or not re.fullmatch(r"(?:[0-9a-fA-F]{40}|[0-9a-fA-F]{64})", revision) or set(revision) == {"0"}:
            print(f"Scope: missing/invalid {label} SHA; enabling portable smoke jobs.", file=sys.stderr)
            return True
    try:
        for revision in (base, head):
            subprocess.run(
                ["git", "cat-file", "-e", f"{revision}^{{commit}}"], cwd=root,
                check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            )
        changed = subprocess.run(
            ["git", "diff", "--no-ext-diff", "--no-renames", "--name-only", "-z", base, head, "--"],
            cwd=root, check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        ).stdout
    except (OSError, subprocess.CalledProcessError) as exc:
        print(f"Scope: unable to compare PR SHAs ({exc}); enabling portable smoke jobs.", file=sys.stderr)
        return True
    return any(is_tooling_path(os.fsdecode(path)) for path in changed.split(b"\0") if path)


def write_scope(changed):
    output = f"tooling_changed={'true' if changed else 'false'}\n"
    print(output, end="")
    if os.environ.get("GITHUB_OUTPUT"):
        with open(os.environ["GITHUB_OUTPUT"], "a", encoding="utf-8") as stream:
            stream.write(output)


def preflight(suite, root=ROOT):
    """Return actionable dependency errors before any expensive test work."""
    errors = []
    for command in ("gcc", "g++") + (("node",) if suite == "full" else ()):
        if not shutil.which(command):
            errors.append(f"{command} is required on PATH")
    if suite == "full":
        for name in PYTHON_DEPENDENCIES:
            try:
                importlib.import_module(name)
            except (ImportError, OSError) as exc:
                errors.append(f"Python dependency {name} cannot be imported: {exc}")
        if not any((root / ".pio/libdeps").glob("*/ArduinoJson/src/ArduinoJson.h")):
            errors.append("ArduinoJson headers are required at .pio/libdeps/<environment>/ArduinoJson/src/ArduinoJson.h; install CI firmware dependencies")
    return errors


def load_suite(suite, root=ROOT):
    sys.path.insert(0, str(root))
    loader = unittest.TestLoader()
    patterns = ("test_*.py",) if suite == "full" else SMOKE_TESTS
    tests = unittest.TestSuite()
    for pattern in patterns:
        selected = loader.discover(str(root / "test"), pattern=pattern)
        if selected.countTestCases() == 0:
            raise RuntimeError(f"No host tests discovered for {pattern}")
        tests.addTests(selected)
    return tests


def run_tests(tests, stream=None):
    """Keep unittest failure/expected-failure semantics; additionally forbid skips."""
    stream = sys.stderr if stream is None else stream
    result = unittest.TextTestRunner(verbosity=2, stream=stream).run(tests)
    if result.skipped:
        print(f"CI coverage failure: {len(result.skipped)} test(s) skipped; all selected tests must run.", file=stream)
    return 0 if result.wasSuccessful() and not result.skipped and result.testsRun else 1


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    scope = commands.add_parser("scope", help="Emit tooling_changed using explicit PR base/head SHAs")
    scope.add_argument("--base", default="")
    scope.add_argument("--head", default="")
    run = commands.add_parser("run", help="Run a host suite, failing on skipped tests")
    run.add_argument("--suite", choices=("full", "smoke"), required=True)
    args = parser.parse_args(argv)
    if args.command == "scope":
        write_scope(tooling_changed(args.base, args.head))
        return 0
    errors = preflight(args.suite)
    if errors:
        print("Host test preflight failed:\n- " + "\n- ".join(errors), file=sys.stderr)
        return 1
    return run_tests(load_suite(args.suite))


if __name__ == "__main__":
    sys.exit(main())
