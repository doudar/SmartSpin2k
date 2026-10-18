"""Build release descriptions from the changelog and complete Git history."""

import argparse
from pathlib import Path
import re
import subprocess
from urllib.parse import quote


def git(root, *args):
    return subprocess.run(
        ["git", *args], cwd=root, check=True, capture_output=True,
        encoding="utf-8",
    ).stdout.strip()


def changelog_highlights(changelog, tag):
    """Include current entries, including already-filed entries on same-day reruns."""
    highlights = []
    sections = re.split(r"^## \[([^\]]+)\][^\n]*\n", changelog, flags=re.MULTILINE)
    for heading, body in zip(sections[1::2], sections[2::2]):
        if heading not in ("Unreleased", tag):
            continue
        # Keep category headings only when they contain actual release notes.
        categories = re.split(r"(^### [^\n]+\n)", body, flags=re.MULTILINE)
        if categories[0].strip():
            highlights.append(categories[0].strip())
        for category, content in zip(categories[1::2], categories[2::2]):
            if content.strip():
                highlights.append(category.strip() + "\n" + content.strip())
    return "\n\n".join(highlights)


def build_notes(root, tag, repository, head="HEAD"):
    # Release tags are date versions. Ignore unrelated tags and the tag being
    # updated so another build on the same day retains the whole day's changes.
    tags = git(root, "tag", "--merged", head, "--sort=-version:refname").splitlines()
    previous = next((value for value in tags
                     if value != tag and re.fullmatch(r"\d+\.\d+\.\d+", value)), None)
    revision_range = f"{previous}..{head}" if previous else head
    commits = git(root, "log", "--reverse", "--no-merges", "--format=%H %s", revision_range)
    base_url = f"https://github.com/{repository}"
    sections = []
    highlights = changelog_highlights((root / "CHANGELOG.md").read_text(encoding="utf-8"), tag)
    if highlights:
        sections.append("## Highlights\n\n" + highlights)
    changes = []
    for commit in commits.splitlines():
        sha, _, subject = commit.partition(" ")
        # Commit subjects are data, not Markdown or shell commands.
        subject = re.sub(r"([\\`*_{}\[\]()<>!#|])", r"\\\1", subject)
        changes.append(f"- {subject} ([{sha[:7]}]({base_url}/commit/{sha}))")
    sections.append("## Commits\n\n" + ("\n".join(changes) or "No new commits since the previous release."))
    if previous:
        # Use the actual build SHA: a same-day release tag may still point at an
        # earlier build, even though its asset and release body are overwritten.
        sha = git(root, "rev-parse", head)
        sections.append(f"**Changes since {previous}**: {base_url}/compare/{quote(previous, safe='')}...{sha}")
    return "\n\n".join(sections) + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tag", required=True)
    parser.add_argument("--repository", required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    args.output.write_text(build_notes(Path.cwd(), args.tag, args.repository), encoding="utf-8")


if __name__ == "__main__":
    main()
