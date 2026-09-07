import tempfile
import unittest
from pathlib import Path
from verify_staged_payload import verify


class PayloadTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.stage = Path(self.temp.name) / "VisionOS"
        self.app = Path(self.temp.name) / "Lumenwork.app"
        for root in (self.stage, self.app):
            payload = root / "cookeddata/lumenwork/content/paks"
            payload.mkdir(parents=True)
            for name in ("global.utoc", "main.utoc", "main.ucas", "main.pak"):
                (payload / name).write_bytes(name.encode())

    def payload(self, name):
        return self.app / "cookeddata/lumenwork/content/paks" / name

    def test_exact_match(self):
        self.assertEqual(len(verify(self.stage, self.app)["files"]), 4)

    def test_old_payload_same_size(self):
        self.payload("main.ucas").write_bytes(b"old.ucas!")
        with self.assertRaisesRegex(ValueError, "changed=.*main.ucas"):
            verify(self.stage, self.app)

    def test_missing_chunk(self):
        self.payload("main.pak").unlink()
        with self.assertRaisesRegex(ValueError, "missing=.*main.pak"):
            verify(self.stage, self.app)

    def test_extra_old_chunk(self):
        self.payload("old.ucas").write_bytes(b"obsolete")
        with self.assertRaisesRegex(ValueError, "extra=.*old.ucas"):
            verify(self.stage, self.app)

    def test_empty_chunk(self):
        self.payload("main.ucas").write_bytes(b"")
        with self.assertRaisesRegex(ValueError, "empty payload"):
            verify(self.stage, self.app)

    def test_no_iostore(self):
        self.payload("main.ucas").unlink()
        with self.assertRaisesRegex(ValueError, "IoStore"):
            verify(self.stage, self.app)


if __name__ == "__main__":
    unittest.main()

