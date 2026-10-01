"""Run the local ERG regression from pioarduino's native build/test action."""

import importlib.util
import os
from pathlib import Path
import subprocess
import sys

from SCons.Script import DEFAULT_TARGETS, Default, Import

Import("env")


def run_erg(source, target, env):
    root = Path(env.subst("$PROJECT_DIR"))
    python = env.subst("$PYTHONEXE")
    missing = [name for name in ("numpy", "scipy", "fitdecode", "matplotlib")
               if importlib.util.find_spec(name) is None]
    if missing:
        print("ERG simulator dependencies missing: " + ", ".join(missing), file=sys.stderr)
        print(f'Install with: "{python}" -m pip install -r scripts/erg_sim/requirements.txt', file=sys.stderr)
        return 1
    result = subprocess.run(
        [python, "-B", "-m", "unittest", "discover", "-s", "test",
         "-p", "test_erg_simulator.py", "-v"], cwd=root,
    )
    print(f"ERG graphs and grades: {root / 'test/output/erg_regression'}", file=sys.stderr, flush=True)
    return result.returncode


# A default alias runs even when the binary is up to date. __test depends on
# these same defaults, so VS Code's native Test action also runs the simulator.
# Keep this local: the existing GitHub workflow needs no simulator dependencies.
if not env.IsIntegrationDump() and os.environ.get("GITHUB_ACTIONS") != "true":
    ride = env.Alias("erg_sim", list(DEFAULT_TARGETS),
                     env.VerboseAction(run_erg, "Running ERG workout regression (FTP 305 W)..."))
    env.AlwaysBuild(ride)
    Default(ride)
