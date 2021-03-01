import subprocess

branch = (
    subprocess.check_output(["git", "rev-parse", "--abbrev-ref", "HEAD"])
    .strip()
    .decode("utf-8")
)
tag = (
    subprocess.check_output(["git", "describe", "--tags"])
    .strip()
    .decode("utf-8")
)
if branch != "master":
    tag_parts = tag.split("-")
    tag = "%s-%s" % (tag_parts[0], branch)
    if len(tag_parts) > 1:
        tag = "%s-%s" % (tag, "-".join(tag_parts[1:]))
print("-DFIRMWARE_VERSION='\"%s\"'" % tag)
