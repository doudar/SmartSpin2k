"""Exercise release notes against real Git histories, without publishing releases."""

from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from scripts.release_notes import build_notes, git


class ReleaseNotesTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        git(self.root, "init", "-b", "develop")
        git(self.root, "config", "user.name", "Release test")
        git(self.root, "config", "user.email", "release@example.com")
        git(self.root, "config", "commit.gpgsign", "false")
        (self.root / "CHANGELOG.md").write_text(
            "# Changelog\n\n## [Unreleased]\n\n### Added\n\n### Changed\n"
            "- Better high-target handling.\n\n### Hardware\n\n"
            "## [26.10.13]\n\n### Changed\n- Old change.\n", encoding="utf-8",
        )
        self.commit("Old change")

    def commit(self, subject):
        git(self.root, "add", ".")
        git(self.root, "commit", "--allow-empty", "-m", subject)
        return git(self.root, "rev-parse", "HEAD")

    def notes(self):
        return build_notes(self.root, "26.10.18", "example/firmware")

    def test_pr_commits_and_direct_pushes_are_included(self):
        git(self.root, "tag", "26.10.13")
        git(self.root, "checkout", "-b", "fix")
        first = self.commit("Fix TMC2225 detection")
        second = self.commit("Improve high-target recovery")
        git(self.root, "checkout", "develop")
        git(self.root, "merge", "--no-ff", "fix", "-m", "Merge pull request #123")
        direct = self.commit("Direct maintenance fix")
        notes = self.notes()
        for sha in (first, second, direct):
            self.assertIn(f"https://github.com/example/firmware/commit/{sha}", notes)
        self.assertLess(notes.index(first), notes.index(second))
        self.assertNotIn("Merge pull request", notes)
        self.assertNotIn("Old change", notes)
        self.assertIn("Better high-target handling.", notes)
        self.assertNotIn("### Added", notes)
        self.assertNotIn("### Hardware", notes)

    def test_same_day_rebuild_retains_earlier_changes_and_filed_highlights(self):
        git(self.root, "tag", "26.10.13")
        first = self.commit("First build change")
        git(self.root, "tag", "26.10.18")
        with (self.root / "CHANGELOG.md").open("a", encoding="utf-8") as stream:
            stream.write("\n## [26.10.18]\n\n### Changed\n- Earlier today.\n")
        second = self.commit("Second build change")
        notes = self.notes()
        self.assertIn(first, notes)
        self.assertIn(second, notes)
        self.assertIn("Earlier today.", notes)
        self.assertIn(f"/compare/26.10.13...{second}", notes)
        self.assertEqual(notes, self.notes())

    def test_first_release_lists_full_history(self):
        sha = self.commit("First release feature")
        notes = self.notes()
        self.assertIn("Old change", notes)
        self.assertIn(sha, notes)
        self.assertNotIn("/compare/", notes)

    def test_only_reachable_version_tags_set_the_boundary(self):
        git(self.root, "tag", "26.9.30")
        self.commit("Previously released")
        git(self.root, "tag", "26.10.13")
        git(self.root, "checkout", "-b", "unmerged")
        self.commit("Unmerged work")
        git(self.root, "tag", "26.10.17")
        git(self.root, "checkout", "develop")
        sha = self.commit("New work")
        git(self.root, "tag", "v99-experiment")
        notes = self.notes()
        self.assertNotIn("Previously released", notes)
        self.assertNotIn("Unmerged work", notes)
        self.assertIn(sha, notes)
        self.assertIn("/compare/26.10.13...", notes)

    def test_cli_writes_utf8_and_treats_commit_subjects_as_text(self):
        self.commit("Fix [link](bad) and <b>markup</b> — café `code` $HOME")
        output = self.root / "notes.md"
        subprocess.run(
            [sys.executable, str(ROOT / "scripts/release_notes.py"), "--tag", "26.10.18",
             "--repository", "example/firmware", "--output", str(output)],
            cwd=self.root, check=True, capture_output=True,
        )
        notes = output.read_text(encoding="utf-8")
        self.assertIn(r"\[link\]\(bad\)", notes)
        self.assertIn(r"\<b\>", notes)
        self.assertIn("— café", notes)
        self.assertIn(r"\`code\` $HOME", notes)
        self.assertEqual(notes, self.notes())

    def test_empty_change_range_is_explicit(self):
        git(self.root, "tag", "26.10.13")
        self.assertIn("No new commits since the previous release.", self.notes())


if __name__ == "__main__":
    unittest.main()
