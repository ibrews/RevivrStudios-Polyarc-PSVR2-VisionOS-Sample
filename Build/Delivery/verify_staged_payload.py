#!/usr/bin/env python3
"""Bind the packaged IoStore/Pak payload to the exact stage used by this run."""
import argparse
import hashlib
import json
from pathlib import Path


def inventory(root):
    root = Path(root)
    if not root.is_dir():
        raise ValueError(f"missing cookeddata directory: {root}")
    files = {}
    for path in sorted(root.rglob("*")):
        if path.suffix.lower() not in {".utoc", ".ucas", ".pak"}:
            continue
        if path.is_symlink() or not path.is_file():
            raise ValueError(f"payload must be a regular file: {path}")
        if path.stat().st_size == 0:
            raise ValueError(f"empty payload: {path}")
        digest = hashlib.sha256()
        with path.open("rb") as stream:
            for block in iter(lambda: stream.read(1024 * 1024), b""):
                digest.update(block)
        files[path.relative_to(root).as_posix()] = {
            "bytes": path.stat().st_size, "sha256": digest.hexdigest()
        }
    suffixes = {Path(name).suffix.lower() for name in files}
    if not {".utoc", ".ucas"}.issubset(suffixes):
        raise ValueError(f"IoStore .utoc and .ucas are required: {root}")
    return files


def verify(stage, app):
    staged = inventory(Path(stage) / "cookeddata")
    packaged = inventory(Path(app) / "cookeddata")
    if staged != packaged:
        missing = sorted(staged.keys() - packaged.keys())
        extra = sorted(packaged.keys() - staged.keys())
        changed = sorted(name for name in staged.keys() & packaged.keys()
                         if staged[name] != packaged[name])
        raise ValueError(f"payload mismatch: missing={missing}, extra={extra}, changed={changed}")
    return {"stage": str(Path(stage).resolve()), "app": str(Path(app).resolve()),
            "result": "pass", "files": staged}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("stage", type=Path)
    parser.add_argument("app", type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    result = verify(args.stage, args.app)
    args.output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(f"STAGED_PAYLOAD_OK files={len(result['files'])} evidence={args.output}")


if __name__ == "__main__":
    main()

