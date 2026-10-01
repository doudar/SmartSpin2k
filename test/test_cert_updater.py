"""Certificate refresh must gate releases, including when an old header exists."""

from contextlib import redirect_stderr, redirect_stdout
import importlib.util
import io
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("cert_updater", ROOT / "cert_updater.py")
updater = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(updater)


class TestCertificateUpdater(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.header = Path(temporary.name) / "cert.h"
        self.header.write_text("existing certificate")
        certificate_path = patch.object(updater, "CERT_FILE_PATH", str(self.header))
        certificate_path.start()
        self.addCleanup(certificate_path.stop)

    def run_main(self):
        output = io.StringIO()
        with redirect_stderr(output), redirect_stdout(output):
            status = updater.main()
        return status, output.getvalue()

    def test_refresh_failure_does_not_accept_existing_header(self):
        with patch.object(updater, "get_certificate", return_value=(None, None, None)):
            status, output = self.run_main()
        self.assertEqual(status, 1)
        self.assertIn("ERROR: GitHub TLS certificate refresh failed", output)
        self.assertNotIn("validated and ready", output)
        self.assertEqual(self.header.read_text(), "existing certificate")

    def test_refresh_failure_without_existing_header(self):
        self.header.unlink()
        with patch.object(updater, "get_certificate", return_value=(None, None, None)):
            status, output = self.run_main()
        self.assertEqual(status, 1)
        self.assertIn("Firmware release must not proceed", output)
        self.assertFalse(self.header.exists())

    def test_changed_and_unchanged_valid_certificates_succeed(self):
        certificate = "-----BEGIN CERTIFICATE-----\nTEST\n-----END CERTIFICATE-----"
        with patch.object(updater, "get_certificate", return_value=(certificate, "Test CA", "2035")):
            status, output = self.run_main()
            self.assertEqual(status, 0, output)
            content = self.header.read_bytes()
            # An unchanged, freshly validated certificate must not need a write.
            with patch.object(Path, "write_text", side_effect=AssertionError("unnecessary rewrite")):
                status, output = self.run_main()
            self.assertEqual(status, 0, output)
            self.assertIn("already current", output)
            self.assertEqual(self.header.read_bytes(), content)

    def test_write_failure_stops_release(self):
        with patch.object(updater, "get_certificate", return_value=("new certificate", "Test CA", "2035")), \
                patch.object(Path, "write_text", side_effect=OSError("disk full")):
            status, output = self.run_main()
        self.assertEqual(status, 1)
        self.assertIn("disk full", output)
        self.assertIn("Firmware release must not proceed", output)

    def test_unexpected_exception_stops_release(self):
        with patch.object(updater, "update_ca_certificate", side_effect=RuntimeError("connection failed")):
            status, output = self.run_main()
        self.assertEqual(status, 1)
        self.assertIn("connection failed", output)
        self.assertIn("ERROR: GitHub TLS certificate refresh failed", output)


if __name__ == "__main__":
    unittest.main()
