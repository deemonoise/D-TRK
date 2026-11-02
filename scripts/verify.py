#!/usr/bin/env python3
"""Run the same local checks as CI, without uploading firmware or accessing devices."""
import argparse
import os
from pathlib import Path
import shlex
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def run(*args, env=None):
    print("+ " + shlex.join(args), flush=True)
    subprocess.run(args, cwd=ROOT, env=env, check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--tests-only", action="store_true")
    mode.add_argument("--builds-only", action="store_true")
    args = parser.parse_args()
    if not args.builds_only:
        run(sys.executable, "-m", "platformio", "test", "-e", "native")
        libraries = list((ROOT / ".pio/build/native").glob("*/libcore.a"))
        if len(libraries) != 1:
            raise RuntimeError(f"Expected one native core archive, found {libraries}")
        test_env = {**os.environ, "DTRK_NATIVE_CORE": str(libraries[0])}
        run(sys.executable, "-m", "unittest", "discover", "-s", "scripts/tests", "-v", env=test_env)
        run(sys.executable, "-m", "unittest", "discover", "-s", "test", "-p", "test_*.py", "-v")
    if not args.tests_only:
        run(sys.executable, "-m", "platformio", "run", "-e", "wt32", "-e", "teensy41")


if __name__ == "__main__":
    try:
        main()
    except subprocess.CalledProcessError as error:
        sys.exit(error.returncode if error.returncode > 0 else 1)
