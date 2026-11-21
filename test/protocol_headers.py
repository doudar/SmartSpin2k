"""Locate the same PlatformIO package used by the firmware's native tests."""
import os
from pathlib import Path


def protocol_include(root):
    libdeps = Path(os.environ.get("PLATFORMIO_LIBDEPS_DIR", root / ".pio/libdeps"))
    include = root / libdeps / "native/SS2kProtocol/c/include"
    if not (include / "ss2k_protocol.h").is_file():
        raise RuntimeError("Install the pinned custom protocol with 'pio pkg install -e native' from the firmware root.")
    return include
