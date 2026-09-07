#!/usr/bin/env python3
"""Fail-closed launcher for authoritative Lumenwork project builds on the M5 host."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import posixpath
import subprocess
import sys
from typing import Any, Callable, Iterable


EXPECTED_PROJECT_ROOT = Path("/Users/Shared/GH/_deliveries/Lumenwork")
REJECTED_PROJECT_ROOT = Path("/Users/Shared/GH/Lumenwork")
EXPECTED_ORIGIN = "https://github.com/AgileLens/Lumenwork.git"
EXPECTED_ORIGINS = {
    EXPECTED_ORIGIN,
    "git@github.com:AgileLens/Lumenwork.git",
    "ssh://git@github.com/AgileLens/Lumenwork.git",
}
REQUIRED_REF = "refs/remotes/origin/codex/lumenwork-authoritative-delivery-20260906"
DELIVERY_BASE = "1f40918ea2664f85a6e4aba5c8adb95b937194b4"
REJECTED_HEAD = "7803a36ea4b94a8c65919c1caa1b072a631d6ed3"
EXPECTED_ENGINE_ROOT = Path(
    "/Users/Shared/GH/UnrealEngineVisionOS/.claude/worktrees/ue582-xr-main-m5"
)
EXPECTED_ENGINE_HEAD = "eb47362c29cc1cde71afeb6bd885c86ff7e8127a"
DEFAULT_ACTION_BUDGET = 200
UNSAFE_PREVIEW_REASON = (
    "UBT -WriteOutdatedActions is quarantined: BuildMode deletes outdated produced items "
    "before exporting the graph (P0 incident 2026-09-07)"
)


class GuardError(RuntimeError):
    pass


def _git(root: Path, *arguments: str) -> str:
    result = subprocess.run(
        ["git", "-C", str(root), *arguments],
        check=False,
        capture_output=True,
        text=True,
    )
    if result.returncode:
        detail = result.stderr.strip() or result.stdout.strip()
        raise GuardError(f"git {' '.join(arguments)} failed in {root}: {detail}")
    return result.stdout.strip()


def collect_identity(project_root: Path, engine_root: Path) -> dict[str, Any]:
    project_root = project_root.resolve()
    engine_root = engine_root.resolve()
    head = _git(project_root, "rev-parse", "HEAD")
    ref_head = _git(project_root, "rev-parse", REQUIRED_REF)
    delivery_base_present = subprocess.run(
        ["git", "-C", str(project_root), "merge-base", "--is-ancestor", DELIVERY_BASE, head],
        check=False,
    ).returncode == 0
    return {
        "project_root": str(project_root),
        "origin": _git(project_root, "remote", "get-url", "origin"),
        "head": head,
        "required_ref_head": ref_head,
        "delivery_base_present": delivery_base_present,
        "tracked_dirty": bool(_git(project_root, "status", "--porcelain", "--untracked-files=no")),
        "engine_root": str(engine_root),
        "engine_head": _git(engine_root, "rev-parse", "HEAD"),
        "engine_tracked_dirty": bool(_git(engine_root, "status", "--porcelain", "--untracked-files=no")),
    }


def validate_identity(facts: dict[str, Any]) -> None:
    project_root = Path(str(facts.get("project_root", "")))
    head = str(facts.get("head", ""))
    errors: list[str] = []

    if project_root == REJECTED_PROJECT_ROOT and head == REJECTED_HEAD:
        errors.append("explicitly rejected legacy checkout /Users/Shared/GH/Lumenwork at 7803a36")
    if project_root != EXPECTED_PROJECT_ROOT:
        errors.append(f"project root must be {EXPECTED_PROJECT_ROOT}, got {project_root}")
    if facts.get("origin") not in EXPECTED_ORIGINS:
        errors.append(f"origin must resolve to AgileLens/Lumenwork, got {facts.get('origin')}")
    if head != facts.get("required_ref_head"):
        errors.append(f"HEAD must exactly match {REQUIRED_REF}")
    if facts.get("delivery_base_present") is not True:
        errors.append(f"required delivery base {DELIVERY_BASE} is absent")
    if facts.get("tracked_dirty") is not False:
        errors.append("project has tracked dirt")
    if Path(str(facts.get("engine_root", ""))) != EXPECTED_ENGINE_ROOT:
        errors.append(f"engine root must be {EXPECTED_ENGINE_ROOT}")
    if facts.get("engine_head") != EXPECTED_ENGINE_HEAD:
        errors.append(f"engine HEAD must be {EXPECTED_ENGINE_HEAD}")
    if facts.get("engine_tracked_dirty") is not False:
        errors.append("engine has tracked dirt")
    if errors:
        raise GuardError("identity gate failed: " + "; ".join(errors))


def combined_target_arguments(project_file: Path) -> list[str]:
    project = str(project_file)
    targets = (
        f"LumenworkEditor Mac Development -Project={project}",
        f"ShaderCompileWorker Mac Development -Project={project}",
        f"UnrealPak Mac Development -Project={project}",
        f"Lumenwork VisionOS Development -Project={project}",
    )
    return [f"-Target={target}" for target in targets]


def preview_command(engine_root: Path, project_file: Path, graph_file: Path) -> list[str]:
    dotnet = engine_root / "Engine/Binaries/ThirdParty/DotNet/10.0/mac-arm64/dotnet"
    ubt = engine_root / "Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.dll"
    return [
        str(dotnet),
        str(ubt),
        *combined_target_arguments(project_file),
        "-NoUBA",
        f"-WriteOutdatedActions={graph_file}",
    ]


def execution_command(
    engine_root: Path, project_file: Path, target_set: str = "combined"
) -> list[str]:
    command = preview_command(engine_root, project_file, Path("unused-preview.json"))
    command = [part for part in command if not part.startswith("-WriteOutdatedActions=")]
    if target_set == "editor":
        command = [part for part in command if not part.startswith("-Target=")]
        command.insert(
            2,
            f"-Target=LumenworkEditor Mac Development -Project={project_file}",
        )
    elif target_set != "combined":
        raise GuardError(f"unknown execution target set: {target_set}")
    command.append("-NoEngineChanges")
    return command


def load_actions(graph_file: Path) -> list[dict[str, Any]]:
    with graph_file.open("r", encoding="utf-8") as stream:
        graph = json.load(stream)
    if isinstance(graph, list):
        actions = graph
    elif isinstance(graph, dict):
        if "Actions" in graph:
            actions = graph["Actions"]
        else:
            actions = graph.get("actions")
    else:
        actions = None
    if not isinstance(actions, list) or any(not isinstance(action, dict) for action in actions):
        raise GuardError("outdated-action graph has no recognized Actions array")
    return actions


def _strings(value: Any) -> Iterable[str]:
    if isinstance(value, str):
        yield value
    elif isinstance(value, list):
        for item in value:
            yield from _strings(item)
    elif isinstance(value, dict):
        for item in value.values():
            yield from _strings(item)


def action_outputs(action: dict[str, Any]) -> Iterable[str]:
    for key, value in action.items():
        normalized = key.lower().replace("_", "")
        if "produced" in normalized or normalized in {
            "output",
            "outputs",
            "outputfile",
            "outputfiles",
        }:
            yield from _strings(value)


def engine_outputs(actions: list[dict[str, Any]], engine_root: Path) -> list[str]:
    def as_posix(value: Any) -> PurePosixPath:
        return PurePosixPath(str(value).replace("\\", "/"))

    engine_root = as_posix(engine_root)
    protected_roots = (
        engine_root / "Engine/Binaries",
        engine_root / "Engine/Intermediate",
        engine_root / "Engine/Saved",
    )
    found: set[str] = set()
    for action in actions:
        working_directory = as_posix(action.get("WorkingDirectory", engine_root / "Engine/Source"))
        for raw_output in action_outputs(action):
            candidate = as_posix(raw_output)
            if not candidate.is_absolute():
                candidate = working_directory / candidate
            candidate = PurePosixPath(posixpath.normpath(str(candidate)))
            if any(candidate == root or root in candidate.parents for root in protected_roots):
                found.add(str(candidate))
    return sorted(found)


def recovery_authorized(
    receipt_file: Path | None,
    token: str | None,
    action_count: int,
    facts: dict[str, Any],
) -> bool:
    if receipt_file is None and token is None:
        return False
    if receipt_file is None or not token:
        raise GuardError("manager recovery override requires both receipt and token")
    with receipt_file.open("r", encoding="utf-8") as stream:
        receipt = json.load(stream)
    expected_token_hash = hashlib.sha256(token.encode("utf-8")).hexdigest()
    checks = {
        "schema_version": receipt.get("schema_version") == 1,
        "authorization": receipt.get("authorization") == "engine-recovery",
        "token_sha256": receipt.get("token_sha256") == expected_token_hash,
        "project_head": receipt.get("project_head") == facts.get("head"),
        "engine_head": receipt.get("engine_head") == facts.get("engine_head"),
        "allow_engine_outputs": receipt.get("allow_engine_outputs") is True,
        "max_actions": isinstance(receipt.get("max_actions"), int)
        and receipt["max_actions"] >= action_count,
    }
    failed = [name for name, passed in checks.items() if not passed]
    if failed:
        raise GuardError("manager recovery receipt rejected: " + ", ".join(failed))
    return True


def validate_preview(
    actions: list[dict[str, Any]],
    engine_root: Path,
    budget: int,
    recovery_allowed: bool,
) -> None:
    outputs = engine_outputs(actions, engine_root)
    violations: list[str] = []
    if len(actions) > budget:
        violations.append(f"{len(actions)} actions exceed ordinary budget {budget}")
    if outputs:
        violations.append(f"would produce {len(outputs)} engine outputs (first: {outputs[0]})")
    if violations and not recovery_allowed:
        raise GuardError("preview gate failed: " + "; ".join(violations))


def guarded_build(
    project_root: Path,
    engine_root: Path,
    graph_file: Path,
    budget: int = DEFAULT_ACTION_BUDGET,
    receipt_file: Path | None = None,
    recovery_token: str | None = None,
    preview_only: bool = False,
    identity_only: bool = False,
    execute_target_set: str = "combined",
    runner: Callable[..., subprocess.CompletedProcess[str]] = subprocess.run,
) -> int:
    facts = collect_identity(project_root, engine_root)
    validate_identity(facts)
    project_file = project_root / "Lumenwork.uproject"
    if not project_file.is_file():
        raise GuardError(f"missing project file: {project_file}")

    if identity_only:
        print(f"IDENTITY_OK head={facts['head']} engine={facts['engine_head']}")
        return 0

    # This must remain before graph directory creation and before any runner call.
    # UE BuildMode calls DeleteOutdatedProducedItems before its
    # WriteOutdatedActions branch, so the nominal preview is destructive.
    raise GuardError(UNSAFE_PREVIEW_REASON)

    graph_file.parent.mkdir(parents=True, exist_ok=True)
    graph_file.unlink(missing_ok=True)
    preview = runner(preview_command(engine_root, project_file, graph_file), check=False, text=True)
    if preview.returncode:
        raise GuardError(f"UBT action preview failed with exit code {preview.returncode}")
    if not graph_file.is_file():
        raise GuardError("UBT preview returned success without writing the action graph")

    actions = load_actions(graph_file)  # pragma: no cover - quarantined above
    allowed = recovery_authorized(receipt_file, recovery_token, len(actions), facts)
    validate_preview(actions, engine_root, budget, allowed)
    print(f"IDENTITY_OK head={facts['head']} engine={facts['engine_head']}")
    print(f"PREVIEW_OK actions={len(actions)} engine_outputs={len(engine_outputs(actions, engine_root))}")
    if preview_only:
        return 0
    execute = runner(
        execution_command(engine_root, project_file, execute_target_set),
        check=False,
        text=True,
    )
    if execute.returncode:
        raise GuardError(f"guarded UBT build failed with exit code {execute.returncode}")
    return 0


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--project-root", type=Path, default=EXPECTED_PROJECT_ROOT)
    parser.add_argument("--engine-root", type=Path, default=EXPECTED_ENGINE_ROOT)
    parser.add_argument(
        "--action-graph",
        type=Path,
        default=Path("/Users/Shared/GH/_deliveries/logs/lumenwork-ubt-outdated-actions.json"),
    )
    parser.add_argument("--action-budget", type=int, default=DEFAULT_ACTION_BUDGET)
    parser.add_argument("--manager-recovery-receipt", type=Path)
    parser.add_argument(
        "--manager-recovery-token",
        default=os.environ.get("LUMENWORK_MANAGER_RECOVERY_TOKEN"),
        help="one-use token; prefer the LUMENWORK_MANAGER_RECOVERY_TOKEN environment variable",
    )
    parser.add_argument("--preview-only", action="store_true")
    parser.add_argument(
        "--identity-only",
        action="store_true",
        help="validate project and engine identity without invoking UBT",
    )
    parser.add_argument(
        "--execute-target-set",
        choices=("combined", "editor"),
        default="combined",
        help="targets to execute after the exact combined-target preview passes",
    )
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(sys.argv[1:] if argv is None else argv)
    if args.action_budget < 1:
        print("GUARD_REJECTED: action budget must be positive", file=sys.stderr)
        return 2
    try:
        return guarded_build(
            project_root=args.project_root,
            engine_root=args.engine_root,
            graph_file=args.action_graph,
            budget=args.action_budget,
            receipt_file=args.manager_recovery_receipt,
            recovery_token=args.manager_recovery_token,
            preview_only=args.preview_only,
            identity_only=args.identity_only,
            execute_target_set=args.execute_target_set,
        )
    except (GuardError, FileNotFoundError, json.JSONDecodeError) as error:
        print(f"GUARD_REJECTED: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
