import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile
import unittest


MODULE_PATH = Path(__file__).with_name("guarded_ubt.py")
SPEC = importlib.util.spec_from_file_location("guarded_ubt", MODULE_PATH)
guard = importlib.util.module_from_spec(SPEC)
assert SPEC and SPEC.loader
SPEC.loader.exec_module(guard)


def approved_facts():
    return {
        "project_root": str(guard.EXPECTED_PROJECT_ROOT),
        "origin": guard.EXPECTED_ORIGIN,
        "head": guard.EXPECTED_HEAD,
        "required_ref_head": guard.EXPECTED_HEAD,
        "required_ancestor_present": True,
        "tracked_dirty": False,
        "engine_root": str(guard.EXPECTED_ENGINE_ROOT),
        "engine_head": guard.EXPECTED_ENGINE_HEAD,
        "engine_tracked_dirty": False,
    }


class IdentityTests(unittest.TestCase):
    def test_approved_delivery_checkout_passes(self):
        guard.validate_identity(approved_facts())

    def test_rejected_legacy_checkout_fails(self):
        facts = approved_facts()
        facts["project_root"] = str(guard.REJECTED_PROJECT_ROOT)
        facts["head"] = guard.REJECTED_HEAD
        with self.assertRaisesRegex(guard.GuardError, "explicitly rejected legacy checkout"):
            guard.validate_identity(facts)

    def test_wrong_origin_ref_ancestry_and_dirt_fail(self):
        facts = approved_facts()
        facts.update(
            origin="https://github.com/example/fork.git",
            required_ref_head="bad",
            required_ancestor_present=False,
            tracked_dirty=True,
        )
        with self.assertRaises(guard.GuardError) as context:
            guard.validate_identity(facts)
        message = str(context.exception)
        self.assertIn("origin must be", message)
        self.assertIn("required ancestor", message)
        self.assertIn("tracked dirt", message)


class PreviewTests(unittest.TestCase):
    def test_3438_action_graph_is_rejected(self):
        actions = [{"ProducedItems": [f"/tmp/project/{index}.o"]} for index in range(3438)]
        with self.assertRaisesRegex(guard.GuardError, "3438 actions exceed"):
            guard.validate_preview(actions, guard.EXPECTED_ENGINE_ROOT, 200, False)

    def test_engine_output_is_rejected(self):
        actions = [
            {
                "WorkingDirectory": str(guard.EXPECTED_ENGINE_ROOT / "Engine/Source"),
                "ProducedItems": ["../Binaries/Mac/UnrealEditor"],
            }
        ]
        with self.assertRaisesRegex(guard.GuardError, "engine outputs"):
            guard.validate_preview(actions, guard.EXPECTED_ENGINE_ROOT, 200, False)

    def test_valid_recovery_receipt_allows_large_engine_graph(self):
        actions = [
            {"ProducedItems": [str(guard.EXPECTED_ENGINE_ROOT / "Engine/Binaries/Mac/UnrealEditor")]}
        ] * 3438
        token = "manager-one-use-token"
        receipt = {
            "schema_version": 1,
            "authorization": "engine-recovery",
            "token_sha256": hashlib.sha256(token.encode()).hexdigest(),
            "project_head": guard.EXPECTED_HEAD,
            "engine_head": guard.EXPECTED_ENGINE_HEAD,
            "allow_engine_outputs": True,
            "max_actions": 3438,
        }
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "receipt.json"
            path.write_text(json.dumps(receipt), encoding="utf-8")
            allowed = guard.recovery_authorized(path, token, len(actions), approved_facts())
        self.assertTrue(allowed)
        guard.validate_preview(actions, guard.EXPECTED_ENGINE_ROOT, 200, allowed)

    def test_command_uses_exact_targets_preview_and_no_engine_changes(self):
        project = guard.EXPECTED_PROJECT_ROOT / "Lumenwork.uproject"
        command = guard.preview_command(guard.EXPECTED_ENGINE_ROOT, project, Path("/tmp/actions.json"))
        targets = [part for part in command if part.startswith("-Target=")]
        self.assertEqual(4, len(targets))
        self.assertTrue(any(part.startswith("-WriteOutdatedActions=") for part in command))
        self.assertNotIn("-NoEngineChanges", command)
        self.assertNotIn("-WriteOutdatedActions=/tmp/actions.json", guard.execution_command(guard.EXPECTED_ENGINE_ROOT, project))
        self.assertIn("-NoEngineChanges", guard.execution_command(guard.EXPECTED_ENGINE_ROOT, project))

    def test_large_preview_fails_before_execution(self):
        calls = []
        original_collect = guard.collect_identity
        original_root = guard.EXPECTED_PROJECT_ROOT
        try:
            with tempfile.TemporaryDirectory() as directory:
                project_root = Path(directory)
                (project_root / "Lumenwork.uproject").write_text("{}", encoding="utf-8")
                guard.EXPECTED_PROJECT_ROOT = project_root
                facts = approved_facts()
                facts["project_root"] = str(project_root)
                guard.collect_identity = lambda *_: facts

                def runner(command, **kwargs):
                    calls.append(command)
                    graph_argument = next(
                        part for part in command if part.startswith("-WriteOutdatedActions=")
                    )
                    graph_path = Path(graph_argument.split("=", 1)[1])
                    graph_path.write_text(
                        json.dumps({"Actions": [{} for _ in range(3438)]}),
                        encoding="utf-8",
                    )
                    return subprocess.CompletedProcess(command, 0)

                with self.assertRaisesRegex(guard.GuardError, "3438 actions exceed"):
                    guard.guarded_build(
                        project_root,
                        guard.EXPECTED_ENGINE_ROOT,
                        project_root / "actions.json",
                        runner=runner,
                    )
        finally:
            guard.collect_identity = original_collect
            guard.EXPECTED_PROJECT_ROOT = original_root
        self.assertEqual(1, len(calls), "compile command must not run after rejected preview")

    def test_identity_failure_happens_before_preview_runner(self):
        called = False

        def runner(*args, **kwargs):
            nonlocal called
            called = True
            return subprocess.CompletedProcess(args[0], 0)

        original_collect = guard.collect_identity
        try:
            guard.collect_identity = lambda *_: {
                **approved_facts(),
                "project_root": str(guard.REJECTED_PROJECT_ROOT),
                "head": guard.REJECTED_HEAD,
            }
            with self.assertRaises(guard.GuardError):
                guard.guarded_build(
                    guard.REJECTED_PROJECT_ROOT,
                    guard.EXPECTED_ENGINE_ROOT,
                    Path("unused.json"),
                    runner=runner,
                )
        finally:
            guard.collect_identity = original_collect
        self.assertFalse(called)


if __name__ == "__main__":
    unittest.main()
