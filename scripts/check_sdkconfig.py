"""Check checked-in firmware configurations against the shared feature policy."""

from pathlib import Path
import re
import sys


ROOT = Path(__file__).resolve().parents[1]
CONFIGS = ("sdkconfig", "sdkconfig.release", "sdkconfig.debug", "sdkconfig.S3release", "sdkconfig.S3debug")
# Old names can override a changed modern setting when Kconfig migrates a file.
ALIASES = {
    "CONFIG_PPP_SUPPORT": "CONFIG_LWIP_PPP_SUPPORT",
    "CONFIG_SPIRAM_ALLOW_STACK_EXTERNAL_MEMORY": "CONFIG_FREERTOS_TASK_CREATE_ALLOW_EXT_MEM",
    "CONFIG_OPTIMIZATION_LEVEL_DEBUG": "CONFIG_COMPILER_OPTIMIZATION_DEBUG",
    "CONFIG_OPTIMIZATION_LEVEL_RELEASE": "CONFIG_COMPILER_OPTIMIZATION_SIZE",
    "CONFIG_COMPILER_OPTIMIZATION_LEVEL_DEBUG": "CONFIG_COMPILER_OPTIMIZATION_DEBUG",
    "CONFIG_COMPILER_OPTIMIZATION_DEFAULT": "CONFIG_COMPILER_OPTIMIZATION_DEBUG",
    "CONFIG_COMPILER_OPTIMIZATION_LEVEL_RELEASE": "CONFIG_COMPILER_OPTIMIZATION_SIZE",
}


def read_config(path):
    values = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        enabled = re.fullmatch(r"(CONFIG_\w+)=(.*)", line)
        disabled = re.fullmatch(r"# (CONFIG_\w+) is not set", line)
        if enabled:
            key, value = enabled.groups()
        elif disabled:
            key, value = disabled[1], "n"
        else:
            continue
        if key in values:
            raise ValueError(f"{path.name}: duplicate {key}")
        values[key] = value
    return values


def main():
    common = read_config(ROOT / "sdkconfig.defaults")
    s3 = read_config(ROOT / "sdkconfig.s3.defaults")
    errors = []
    for name in CONFIGS:
        actual = read_config(ROOT / name)
        expected = dict(common)
        if name.startswith("sdkconfig.S3"):
            expected.update(s3)
        expected["CONFIG_IDF_TARGET"] = '"esp32s3"' if name.startswith("sdkconfig.S3") else '"esp32"'
        for key, value in expected.items():
            # Kconfig omits disabled symbols whose dependencies are unavailable.
            if actual.get(key, "n") != value:
                errors.append(f"{name}: {key}={actual.get(key, 'n')}, expected {value}")
        for key in actual:
            if key.startswith("CONFIG_ARDUINO_SELECTIVE_") and key not in common:
                errors.append(f"{name}: Arduino option {key} needs a decision in sdkconfig.defaults")
        for alias, key in ALIASES.items():
            if alias in actual and actual[alias] != actual.get(key, "n"):
                errors.append(f"{name}: legacy {alias} disagrees with {key}")
    if errors:
        print("\n".join(errors), file=sys.stderr)
        return 1
    print(f"SDK feature policy matches all {len(CONFIGS)} firmware configurations.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
