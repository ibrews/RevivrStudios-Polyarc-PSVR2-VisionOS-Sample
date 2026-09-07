#!/usr/bin/env python3
"""Verify that a METAL_SM5_IOS cook contains Lumen Lite global shaders."""

from __future__ import annotations

import argparse
import csv
from pathlib import Path
import sys


REQUIRED_SHADERS = {
    "FDownsampleDepthAndNormalCS": 1,
    "FMarkProbesUsedByGBufferCS": 2,
    "FInterpolateIrradianceProbesToPixelsCS": 4,
}


class VerificationError(RuntimeError):
    pass


def verify_shader_stats(path: Path) -> dict[str, tuple[int, int]]:
    if path.name != "ShaderTypeStats-METAL_SM5_IOS.csv":
        raise VerificationError(
            "expected ShaderTypeStats-METAL_SM5_IOS.csv, got " + path.name
        )
    with path.open("r", encoding="utf-8-sig", newline="") as stream:
        reader = csv.DictReader(stream)
        required_columns = {"ShaderTypeName", "ShaderType", "NumShaders", "ShadersSize"}
        missing_columns = required_columns.difference(reader.fieldnames or [])
        if missing_columns:
            raise VerificationError(
                "shader stats missing columns: " + ", ".join(sorted(missing_columns))
            )
        matches: dict[str, tuple[int, int]] = {}
        for row in reader:
            name = row.get("ShaderTypeName", "")
            if name not in REQUIRED_SHADERS:
                continue
            if name in matches:
                raise VerificationError(f"duplicate shader stats row: {name}")
            if row.get("ShaderType") != "Global":
                raise VerificationError(f"{name} is not recorded as a Global shader")
            try:
                count = int(row["NumShaders"])
                size = int(row["ShadersSize"])
            except (TypeError, ValueError) as error:
                raise VerificationError(f"invalid numeric values for {name}") from error
            matches[name] = (count, size)

    failures: list[str] = []
    for name, minimum in REQUIRED_SHADERS.items():
        if name not in matches:
            failures.append(f"{name}=missing")
            continue
        count, size = matches[name]
        if count < minimum or size <= 0:
            failures.append(
                f"{name}=NumShaders:{count},ShadersSize:{size}; require >= {minimum}, > 0"
            )
    if failures:
        raise VerificationError("Lumen Lite shader gate failed: " + "; ".join(failures))
    return matches


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("shader_stats", type=Path)
    args = parser.parse_args(argv)
    try:
        matches = verify_shader_stats(args.shader_stats)
    except (OSError, VerificationError) as error:
        print(f"LUMEN_LITE_SHADER_GATE_REJECTED: {error}", file=sys.stderr)
        return 2
    for name in REQUIRED_SHADERS:
        count, size = matches[name]
        print(f"LUMEN_LITE_SHADER_OK name={name} count={count} bytes={size}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
