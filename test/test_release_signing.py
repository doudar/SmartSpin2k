"""Exercise the release CLI with disposable keys, including publication failure paths."""

import hashlib
import json
import os
from pathlib import Path
import secrets
import subprocess
import sys
import tempfile
import unittest
import zipfile

from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec


ROOT = Path(__file__).resolve().parents[1]
TAG = "26.11.21"
ARCHIVE = f"SmartSpin2kFirmware-{TAG}.bin.zip"
MANIFEST = "firmware-manifest.json"
MANIFEST_SIG = "firmware-manifest.sig"
IMAGES = ("firmware.bin", "S3firmware.bin")
ALGORITHM = ec.ECDSA(hashes.SHA256())


class ReleaseSigningTest(unittest.TestCase):
    def setUp(self):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.root = Path(directory.name)
        self.build = self.root / "build"
        self.output = self.root / "public-assets"
        self.public_path = self.root / "publisher.pem"
        self.private = ec.generate_private_key(ec.SECP256R1())
        self.public = self.private.public_key()
        self.public_path.write_bytes(self.public.public_bytes(
            serialization.Encoding.PEM, serialization.PublicFormat.SubjectPublicKeyInfo))
        self.passphrase = secrets.token_hex(32)
        self.encrypted_pem = self.private.private_bytes(
            serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8,
            serialization.BestAvailableEncryption(self.passphrase.encode())).decode()
        self.inputs = {}
        for env, prefix in (("release", ""), ("S3release", "S3")):
            directory = self.build / env
            directory.mkdir(parents=True)
            for suffix in ("bootloader.bin", "partitions.bin", "firmware.factory.bin", "firmware.bin"):
                name = prefix + suffix
                self.inputs[name] = name.encode() + bytes(range(256))
                (directory / name).write_bytes(self.inputs[name])

    def cli(self, action, *, tag=TAG, passphrase=None, success=True):
        # Only generated test credentials may enter the child process.
        env = {k: v for k, v in os.environ.items() if k not in ("SS2K_PRIVATE_KEY", "SS2K_KEY_PASS")}
        if action == "sign":
            env.update(SS2K_PRIVATE_KEY=self.encrypted_pem,
                       SS2K_KEY_PASS=self.passphrase if passphrase is None else passphrase)
        result = subprocess.run(
            [sys.executable, str(ROOT / "scripts/sign_release.py"), action, "--tag", tag,
             "--public-key", str(self.public_path), "--build-dir", str(self.build), "--output", str(self.output)],
            env=env, text=True, capture_output=True)
        self.assertNotIn(self.passphrase, result.stdout + result.stderr)
        self.assertNotIn("PRIVATE KEY", result.stdout + result.stderr)
        self.assertEqual(result.returncode == 0, success, result.stderr)

    def test_exact_manifest_and_manual_signatures(self):
        self.cli("sign")
        self.cli("verify")
        self.assertEqual({p.name for p in self.output.iterdir()}, {ARCHIVE, MANIFEST, MANIFEST_SIG})
        raw = (self.output / MANIFEST).read_bytes()
        self.public.verify((self.output / MANIFEST_SIG).read_bytes(), raw, ALGORITHM)
        manifest = json.loads(raw)
        archive_bytes = (self.output / ARCHIVE).read_bytes()
        self.assertEqual(manifest["archive"], dict(name=ARCHIVE, size=len(archive_bytes),
                                                   sha256=hashlib.sha256(archive_bytes).hexdigest()))
        self.assertEqual((manifest["schema"], manifest["tag"]), (1, TAG))
        self.assertEqual(set(manifest["images"]), set(IMAGES))
        with zipfile.ZipFile(self.output / ARCHIVE) as archive:
            self.assertCountEqual(archive.namelist(), [*self.inputs, *(name + ".sig" for name in IMAGES)])
            for name, data in self.inputs.items():
                self.assertEqual(archive.read(name), data)
            for name in IMAGES:
                data = self.inputs[name]
                self.public.verify(archive.read(name + ".sig"), data, ALGORITHM)
                self.assertEqual(manifest["images"][name], dict(size=len(data), sha256=hashlib.sha256(data).hexdigest()))

    def test_wrong_pin_wrong_passphrase_and_missing_secrets(self):
        for passphrase in ("", self.passphrase + "-wrong"):
            self.cli("sign", passphrase=passphrase, success=False)
        for curve in (ec.SECP256R1(), ec.SECP384R1()):
            wrong_pin = ec.generate_private_key(curve).public_key()
            self.public_path.write_bytes(wrong_pin.public_bytes(
                serialization.Encoding.PEM, serialization.PublicFormat.SubjectPublicKeyInfo))
            self.cli("sign", success=False)
        self.assertFalse(self.output.exists())

    def test_failed_input_leaves_no_partial_bundle_and_can_be_retried(self):
        missing = self.build / "S3release/S3partitions.bin"
        missing.unlink()
        self.cli("sign", success=False)
        self.assertFalse(self.output.exists())
        self.assertEqual(list(self.root.glob(".ss2k-release-*")), [])
        missing.write_bytes(self.inputs[missing.name])
        self.cli("sign")
        self.cli("sign", success=False)
        self.cli("verify")

    def test_reject_modified_manifest_tag_and_archive(self):
        self.cli("sign")
        raw = (self.output / MANIFEST).read_bytes()
        (self.output / MANIFEST).write_bytes(raw + b" ")
        self.cli("verify", success=False)
        (self.output / MANIFEST).write_bytes(raw)
        self.cli("verify", tag="26.11.20", success=False)
        with (self.output / ARCHIVE).open("ab") as archive:
            archive.write(b"extra")
        self.cli("verify", success=False)

    def test_reject_swapped_image_signatures_even_in_authenticated_archive(self):
        self.cli("sign")
        archive_path = self.output / ARCHIVE
        with zipfile.ZipFile(archive_path) as archive:
            members = {name: archive.read(name) for name in archive.namelist()}
        members["firmware.bin.sig"] = members["S3firmware.bin.sig"]
        with zipfile.ZipFile(archive_path, "w") as archive:
            for name, data in members.items():
                archive.writestr(name, data)
        archive_bytes = archive_path.read_bytes()
        manifest = json.loads((self.output / MANIFEST).read_bytes())
        manifest["archive"].update(size=len(archive_bytes), sha256=hashlib.sha256(archive_bytes).hexdigest())
        raw = json.dumps(manifest).encode()
        (self.output / MANIFEST).write_bytes(raw)
        (self.output / MANIFEST_SIG).write_bytes(self.private.sign(raw, ALGORITHM))
        self.cli("verify", success=False)


if __name__ == "__main__":
    unittest.main()
