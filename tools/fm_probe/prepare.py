#!/usr/bin/env python3
"""Prepare the pinned new-FM compute probe outside the firmware project."""
import argparse
from pathlib import Path
import re
import shutil
import subprocess

SOURCE_REVISION = "4000b2ec2975f07e76c99aed7d83bb72630720f0"
SOURCES = (
    "synth_fm.cpp", "synth_fm_tonal.cpp", "synth_fm_percussion.cpp",
    "synth_fm_machines.cpp", "synth_filter.cpp", "synth_osc.cpp",
)
INCLUDE = re.compile(r'^\s*#include\s+"([^"]+)"', re.MULTILINE)


def read_source(repository: Path, name: str) -> str:
    return subprocess.check_output(
        ["git", "show", f"{SOURCE_REVISION}:lib/core/src/{name}"],
        cwd=repository, text=True, stderr=subprocess.PIPE,
    )


def required_headers(source: dict[str, str], repository: Path) -> dict[str, str]:
    headers: dict[str, str] = {}
    pending = {name for text in source.values() for name in INCLUDE.findall(text)}
    pending.add("synth_fm_machines.h")
    while pending:
        name = pending.pop()
        if name in headers:
            continue
        text = read_source(repository, name)
        headers[name] = text
        pending.update(INCLUDE.findall(text))
    return headers


def adapt_sample_rate(headers: dict[str, str]) -> None:
    oscillator = headers["synth_osc.h"]
    old_rate = "constexpr int kSynthRate = 44100;"
    if oscillator.count(old_rate) != 1:
        raise RuntimeError("Pinned source no longer has the expected 44.1 kHz declaration")
    headers["synth_osc.h"] = oscillator.replace(old_rate, "constexpr int kSynthRate = 32000;")


def validate_output(output: Path, repository: Path) -> Path:
    output = output.expanduser().resolve()
    if output == repository or repository in output.parents:
        raise ValueError("Probe output must be outside the firmware repository")
    return output


def write_project(output: Path, source: dict[str, str], headers: dict[str, str], template: Path) -> None:
    for folder in ("src", "include"):
        (output / folder).mkdir(parents=True, exist_ok=True)
    for name, text in source.items():
        (output / "src" / name).write_text(text)
    for name, text in headers.items():
        (output / "include" / name).write_text(text)
    shutil.copyfile(template / "main.cpp", output / "src" / "main.cpp")
    shutil.copyfile(template / "platformio.ini", output / "platformio.ini")
    (output / "SOURCE_REVISION").write_text(SOURCE_REVISION + "\nrate=32000\n")


def prepare(output: Path) -> Path:
    template = Path(__file__).resolve().parent
    repository = template.parents[1]
    output = validate_output(output, repository)
    # Resolve and validate everything before writing: a bad revision leaves no partial probe.
    source = {name: read_source(repository, name) for name in SOURCES}
    headers = required_headers(source, repository)
    adapt_sample_rate(headers)
    write_project(output, source, headers, template)
    return output


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=Path("/private/tmp/dtrk-newfm-probe"))
    args = parser.parse_args()
    print(prepare(args.output))


if __name__ == "__main__":
    main()
