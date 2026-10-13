"""CI selection and no-skip enforcement, without recursively running host suites."""
from contextlib import redirect_stderr, redirect_stdout
import io
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from scripts import ci_host_tests as ci


class ScopeTests(unittest.TestCase):
    def test_tooling_paths(self):
        for path in ("scripts/a.py", "test/data/a.csv", "lib/ArduinoCompat/Arduino.h",
                     ".github/workflows/host.yml", "requirements.txt", "requirements-ci.txt",
                     "platformio.ini", "dependencies.lock", "pyproject.toml", "setup.cfg",
                     "setup.py", ".python-version"):
            with self.subTest(path=path):
                self.assertTrue(ci.is_tooling_path(path))
        for path in ("src/Main.cpp", "include/Main.h", "docs/readme.md", "data/index.html",
                     "hardware/board", "Hardware/board", "lib/SS2K/a.cpp", "README.md",
                     "scripts-other/a.py", "docs/requirements.txt"):
            with self.subTest(path=path):
                self.assertFalse(ci.is_tooling_path(path))

    def test_missing_zero_invalid_and_unavailable_revisions_enable_smoke(self):
        with redirect_stderr(io.StringIO()):
            for base in (None, "", "0" * 40, "HEAD", "--help", "a" * 39):
                with self.subTest(base=base), patch.object(ci.subprocess, "run") as git:
                    self.assertTrue(ci.tooling_changed(base, "a" * 40))
                    git.assert_not_called()
            with patch.object(ci.subprocess, "run", side_effect=subprocess.CalledProcessError(128, "git")):
                self.assertTrue(ci.tooling_changed("a" * 40, "b" * 40))

    def test_nul_delimited_diff_preserves_newlines_in_paths(self):
        with patch.object(ci.subprocess, "run", return_value=subprocess.CompletedProcess([], 0, b"docs/readme\nnotes.md\0scripts/tool\nname.py\0")) as git:
            self.assertTrue(ci.tooling_changed("a" * 40, "b" * 40))
        command = git.call_args.args[0]
        self.assertIn("-z", command)
        self.assertIn("--no-renames", command)

    def test_scope_cli_without_shas_emits_true(self):
        output = io.StringIO()
        with patch.dict(os.environ, {"GITHUB_OUTPUT": ""}), redirect_stdout(output), redirect_stderr(io.StringIO()):
            self.assertEqual(ci.main(["scope"]), 0)
        self.assertEqual(output.getvalue(), "tooling_changed=true\n")

    def test_github_output_is_appended(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "output"
            output.write_text("other=value\n", encoding="utf-8")
            with patch.dict(os.environ, {"GITHUB_OUTPUT": str(output)}), redirect_stdout(io.StringIO()):
                ci.write_scope(False)
                ci.write_scope(True)
            self.assertEqual(output.read_text(), "other=value\ntooling_changed=false\ntooling_changed=true\n")

    def test_real_git_deleted_renamed_and_explicit_endpoints(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)

            def git(*args):
                return subprocess.check_output(["git", *args], cwd=root, stderr=subprocess.PIPE).decode().strip()

            def commit():
                git("add", "-A")
                git("-c", "user.name=CI Test", "-c", "user.email=ci@example.invalid",
                    "-c", "commit.gpgsign=false", "commit", "-qm", "test")
                return git("rev-parse", "HEAD")

            git("init", "-q")
            (root / "scripts").mkdir()
            (root / "src").mkdir()
            (root / "scripts/tool name.py").write_text("tool\n")
            (root / "src/main.cpp").write_text("before\n")
            base = commit()
            (root / "src/main.cpp").write_text("after\n")
            firmware = commit()
            self.assertFalse(ci.tooling_changed(base, firmware, root))
            git("mv", "scripts/tool name.py", "src/moved.cpp")
            moved_out = commit()
            self.assertTrue(ci.tooling_changed(firmware, moved_out, root))
            git("mv", "src/moved.cpp", "scripts/restored.py")
            moved_in = commit()
            self.assertTrue(ci.tooling_changed(moved_out, moved_in, root))
            (root / "scripts/restored.py").unlink()
            deleted = commit()
            self.assertTrue(ci.tooling_changed(moved_in, deleted, root))
            # HEAD now contains tooling edits, but explicit earlier endpoints do not.
            self.assertFalse(ci.tooling_changed(base, firmware, root))
            self.assertFalse(ci.tooling_changed(deleted, deleted, root))


class RunnerTests(unittest.TestCase):
    def run_case(self, method):
        case = type("SyntheticCase", (unittest.TestCase,), {"test_case": method})
        stream = io.StringIO()
        code = ci.run_tests(unittest.defaultTestLoader.loadTestsFromTestCase(case), stream)
        return code, stream.getvalue()

    def test_skipped_test_fails_ci(self):
        code, output = self.run_case(lambda case: case.skipTest("missing dependency"))
        self.assertEqual(code, 1)
        self.assertIn("CI coverage failure", output)
        self.assertIn("missing dependency", output)

    def test_pass_failure_and_error(self):
        self.assertEqual(self.run_case(lambda case: None)[0], 0)
        self.assertEqual(self.run_case(lambda case: case.fail("regression"))[0], 1)
        self.assertEqual(self.run_case(lambda case: 1 / 0)[0], 1)

    def test_expected_failures_remain_visible_and_unexpected_success_fails(self):
        expected = unittest.expectedFailure(lambda case: case.fail("known failure"))
        code, output = self.run_case(expected)
        self.assertEqual(code, 0)
        self.assertIn("expected failure", output)
        self.assertEqual(self.run_case(unittest.expectedFailure(lambda case: None))[0], 1)

    def test_empty_suite_fails(self):
        self.assertEqual(ci.run_tests(unittest.TestSuite(), io.StringIO()), 1)

    def test_smoke_selection_is_exact(self):
        patterns = []

        def discover(start_dir, pattern):
            patterns.append(pattern)
            return unittest.TestSuite([unittest.FunctionTestCase(lambda: None)])

        with patch.object(ci.unittest.TestLoader, "discover", side_effect=discover):
            self.assertEqual(ci.load_suite("smoke").countTestCases(), 3)
        self.assertEqual(patterns, ["test_virtual_ble_ingress.py", "test_virtual_ftms.py", "test_bike_bench.py"])

    def test_full_selection_discovers_every_test_module(self):
        with patch.object(ci.unittest.TestLoader, "discover", return_value=unittest.TestSuite([unittest.FunctionTestCase(lambda: None)])) as discover:
            ci.load_suite("full")
        discover.assert_called_once_with(str(ROOT / "test"), pattern="test_*.py")

    def test_missing_selected_suite_fails(self):
        with patch.object(ci.unittest.TestLoader, "discover", return_value=unittest.TestSuite()):
            with self.assertRaisesRegex(RuntimeError, "No host tests"):
                ci.load_suite("smoke")

    def test_smoke_preflight_needs_only_compilers(self):
        with patch.object(ci.shutil, "which", return_value="compiler") as which, patch.object(ci.importlib, "import_module") as imports:
            self.assertEqual(ci.preflight("smoke"), [])
        self.assertEqual([call.args[0] for call in which.call_args_list], ["gcc", "g++"])
        imports.assert_not_called()

    def test_full_preflight_reports_all_missing_dependencies(self):
        with tempfile.TemporaryDirectory() as directory:
            with patch.object(ci.shutil, "which", return_value=None), patch.object(ci.importlib, "import_module", side_effect=ImportError("absent")):
                errors = ci.preflight("full", Path(directory))
        for dependency in ("gcc", "g++", "node", *ci.PYTHON_DEPENDENCIES, "ArduinoJson"):
            self.assertTrue(any(dependency in error for error in errors), dependency)

    def test_failed_preflight_does_not_discover_tests(self):
        with patch.object(ci, "preflight", return_value=["g++ is required"]), patch.object(ci, "load_suite") as load, redirect_stderr(io.StringIO()):
            self.assertEqual(ci.main(["run", "--suite", "smoke"]), 1)
        load.assert_not_called()


if __name__ == "__main__":
    unittest.main()
