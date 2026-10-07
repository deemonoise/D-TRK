# PlatformIO pre-script: the firmware version (git commit, "+" if the tree has changes) as DTRK_REV.
import subprocess

Import("env")


def git(*args):
    try:
        return subprocess.check_output(["git"] + list(args), cwd=env["PROJECT_DIR"], stderr=subprocess.DEVNULL).decode().strip()
    except Exception:
        return ""


rev = git("rev-parse", "--short=7", "HEAD") or "dev"
if git("status", "--porcelain", "--untracked-files=no"):
    rev += "+"
env.Append(CPPDEFINES=[("DTRK_REV", '\\"%s\\"' % rev)])
