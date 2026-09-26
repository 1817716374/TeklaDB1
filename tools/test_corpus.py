"""Offline tests for pinned nested ZIP retrieval; all work stays under argv[1]."""
import hashlib
import io
from pathlib import Path
import sys
import tempfile
import unittest
import warnings
import zipfile
import corpus

ROOT = Path(sys.argv.pop(1)).resolve()
ROOT.mkdir(parents=True, exist_ok=True)


def digest(data):
    return dict(size=len(data), sha256=hashlib.sha256(data).hexdigest())


def packed(entries):
    out = io.BytesIO()
    with warnings.catch_warnings():
        warnings.simplefilter("ignore", UserWarning)
        with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
            for name, value in entries:
                z.writestr(name, value)
    return out.getvalue()


class Archives(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="corpus-test-", dir=ROOT)
        self.root = Path(self.temp.name).resolve()
        assert self.root.is_relative_to(ROOT)
        self.addCleanup(self.temp.cleanup)
        self.value = b"model-data"
        inner = packed([("model.db1", self.value)])
        outer = packed([("nested.zip", inner)])
        source = self.root / "source.zip"
        source.write_bytes(outer)
        self.archives = {"outer": dict(url=source.as_uri(), **digest(outer)),
                         "inner": dict(archive="outer", member="nested.zip", **digest(inner))}
        self.spec = dict(archive="inner", member="model.db1", **digest(self.value))
        self.dest = self.root / "data/model.db1"

    def run_fetch(self):
        corpus.materialize(self.spec, self.dest, self.archives, self.root / "data")

    def test_nested_and_corrupt_cache(self):
        self.run_fetch()
        self.assertEqual(self.dest.read_bytes(), self.value)
        self.dest.write_bytes(b"bad")
        self.run_fetch()
        self.assertEqual(self.dest.read_bytes(), self.value)

    def test_member_hash(self):
        self.spec["sha256"] = "0" * 64
        with self.assertRaisesRegex(ValueError, "hash/size"):
            self.run_fetch()
        self.assertFalse(self.dest.exists())

    def test_member_size(self):
        self.spec["size"] += 1
        with self.assertRaisesRegex(ValueError, "byte count"):
            self.run_fetch()

    def test_parent_hash(self):
        self.archives["outer"]["sha256"] = "0" * 64
        with self.assertRaisesRegex(ValueError, "hash/size"):
            self.run_fetch()
        self.assertFalse(list(self.root.rglob("*.partial")))

    def test_cycle(self):
        self.archives["inner"]["archive"] = "inner"
        with self.assertRaisesRegex(ValueError, "cyclic"):
            self.run_fetch()

    def test_missing_archive(self):
        del self.archives["outer"]
        with self.assertRaisesRegex(ValueError, "missing"):
            self.run_fetch()

    def test_duplicate_member(self):
        source = self.root / "duplicate.zip"
        source.write_bytes(packed([("model.db1", self.value), ("model.db1", self.value)]))
        with self.assertRaisesRegex(ValueError, "ambiguous"):
            corpus.extract_member(self.spec, source, self.dest)

    def test_path_escape(self):
        self.spec["archive"] = "../../outside"
        self.archives[self.spec["archive"]] = self.archives["outer"]
        with self.assertRaisesRegex(ValueError, "unsafe"):
            self.run_fetch()


if __name__ == "__main__":
    unittest.main(verbosity=2)
