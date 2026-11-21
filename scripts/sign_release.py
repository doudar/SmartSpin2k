"""Package and verify SmartSpin2k releases signed with ECDSA P-256 / SHA-256."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import sys
import tempfile
import zipfile

from cryptography.exceptions import InvalidSignature, UnsupportedAlgorithm
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec


APP_IMAGES = {"firmware.bin": "release", "S3firmware.bin": "S3release"}
BINARY_SUFFIXES = ("firmware.bin", "firmware.factory.bin", "partitions.bin", "bootloader.bin")
MANIFEST = "firmware-manifest.json"
MANIFEST_SIG = "firmware-manifest.sig"
PUBLIC_KEY = Path(__file__).resolve().parents[1] / "include/ss2k-release-public.pem"


def archive_name(tag):
    if not re.fullmatch(r"[0-9]{2}\.[0-9]{1,2}\.[0-9]{1,2}", tag):
        raise ValueError("Expected a SmartSpin2k date-version release tag")
    return f"SmartSpin2kFirmware-{tag}.bin.zip"


def file_info(data):
    return {"size": len(data), "sha256": hashlib.sha256(data).hexdigest()}


def public_key(path):
    key = serialization.load_pem_public_key(path.read_bytes())
    if not isinstance(key, ec.EllipticCurvePublicKey) or not isinstance(key.curve, ec.SECP256R1):
        raise ValueError("The pinned release key must be an ECDSA P-256 public key")
    return key


def signing_key(pem, passphrase, trusted):
    if not pem or not passphrase:
        raise ValueError("The encrypted signing key and passphrase must both be configured")
    try:
        key = serialization.load_pem_private_key(pem, password=passphrase)
    except (TypeError, ValueError, UnsupportedAlgorithm):
        raise ValueError("Unable to load the encrypted release signing key") from None
    if not isinstance(key, ec.EllipticCurvePrivateKey) or key.public_key().public_numbers() != trusted.public_numbers():
        raise ValueError("The release signing key does not match the pinned P-256 public key")
    return key


def signature_algorithm():
    return ec.ECDSA(hashes.SHA256())


def verify_release(directory, tag, trusted):
    name = archive_name(tag)
    raw = (directory / MANIFEST).read_bytes()
    trusted.verify((directory / MANIFEST_SIG).read_bytes(), raw, signature_algorithm())
    archive_path = directory / name
    expected = {"schema": 1, "tag": tag, "archive": {"name": name, **file_info(archive_path.read_bytes())}, "images": {}}
    with zipfile.ZipFile(archive_path) as archive:
        required = {prefix + suffix for prefix in ("", "S3") for suffix in BINARY_SUFFIXES}
        required.update(image + ".sig" for image in APP_IMAGES)
        if sorted(archive.namelist()) != sorted(required):
            raise ValueError("Release ZIP member names do not match the expected artifact set")
        for image in APP_IMAGES:
            payload = archive.read(image)
            trusted.verify(archive.read(image + ".sig"), payload, signature_algorithm())
            expected["images"][image] = file_info(payload)
    # Compare values after authenticating the exact UTF-8 manifest bytes.
    if json.loads(raw.decode("utf-8")) != expected:
        raise ValueError("Release manifest disagrees with its tag, archive or application images")


def create_release(build_dir, output, tag, key, trusted):
    name = archive_name(tag)
    # Stage outside the output directory. Verification must succeed before a
    # single rename exposes the ZIP and both matching manifest files to CI.
    if output.exists():
        raise ValueError("Choose a new output directory for this release bundle")
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".ss2k-release-", dir=output.parent) as staging:
        bundle = Path(staging) / "bundle"
        bundle.mkdir()
        archive_path = bundle / name
        images = {}
        with zipfile.ZipFile(archive_path, "x", compression=zipfile.ZIP_DEFLATED) as archive:
            for image, environment in APP_IMAGES.items():
                prefix = image.removesuffix("firmware.bin")
                for suffix in BINARY_SUFFIXES:
                    member = prefix + suffix
                    source = build_dir / environment / member
                    if suffix == "firmware.bin":
                        # Sign and package the same byte string even if the build
                        # directory is replaced while the archive is being made.
                        payload = source.read_bytes()
                        if not payload:
                            raise ValueError(f"Application image is empty: {member}")
                        images[member] = file_info(payload)
                        archive.writestr(member, payload)
                        archive.writestr(member + ".sig", key.sign(payload, signature_algorithm()))
                    else:
                        archive.write(source, member)
        manifest = {"schema": 1, "tag": tag, "archive": {"name": name, **file_info(archive_path.read_bytes())}, "images": images}
        raw = (json.dumps(manifest, separators=(",", ":"), ensure_ascii=False) + "\n").encode("utf-8")
        (bundle / MANIFEST).write_bytes(raw)
        (bundle / MANIFEST_SIG).write_bytes(key.sign(raw, signature_algorithm()))
        verify_release(bundle, tag, trusted)
        bundle.rename(output)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("sign", "verify"))
    parser.add_argument("--tag", required=True)
    parser.add_argument("--public-key", type=Path, default=PUBLIC_KEY)
    parser.add_argument("--build-dir", type=Path, default=Path(".pio/build"))
    parser.add_argument("--output", type=Path, default=Path("signed-release"))
    args = parser.parse_args()
    trusted = public_key(args.public_key)
    if args.action == "verify":
        verify_release(args.output, args.tag, trusted)
    else:
        # The private material stays in this signing process and is never
        # placed on disk or passed to a subprocess or command-line argument.
        key = signing_key(os.environ.pop("SS2K_PRIVATE_KEY", "").encode(),
                          os.environ.pop("SS2K_KEY_PASS", "").encode(), trusted)
        create_release(args.build_dir, args.output, args.tag, key, trusted)
    print(f"Verified signed SmartSpin2k release {args.tag}")


if __name__ == "__main__":
    try:
        main()
    except InvalidSignature:
        sys.exit("Release signature verification failed")
    except (OSError, ValueError, KeyError, zipfile.BadZipFile) as exc:
        sys.exit(f"Release packaging failed: {exc}")
