import hashlib
import importlib.util
import json
from pathlib import Path
import tarfile
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("package_kpm", ROOT / "package_kpm.py")
packager = importlib.util.module_from_spec(spec)
spec.loader.exec_module(packager)


class PackageTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="chess-package-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.zip = self.root / "runtime.zip"
        self.binary = self.root / "app"
        elf = bytearray(52)
        elf[:6] = b"\x7fELF\x01\x01"
        elf[18:20] = (40).to_bytes(2, "little")
        self.binary.write_bytes(elf)
        with zipfile.ZipFile(self.zip, "w") as archive:
            for path in ["bin/armhf/pikafish", "bin/armhf/pikafish.nnue",
                         "lib/armhf/ld-linux-armhf.so.3", "assets/xiangqi/board.png"]:
                archive.writestr("extensions/exact-chinesechess/" + path, b"asset")
        self.output = self.root / "xiangqi.kpkg"

    def build(self):
        digest = hashlib.sha256(self.zip.read_bytes()).hexdigest()
        return packager.package(self.zip, self.binary, self.output, digest)

    def test_package_contains_manifest_hooks_and_replacement_binary(self):
        self.build()
        with tarfile.open(self.output, "r:gz") as archive:
            manifest = json.load(archive.extractfile("manifest.json"))
            self.assertEqual(manifest["id"], "exactchinesechess")
            self.assertEqual(archive.extractfile("payload/bin/armhf/exact-chinesechess").read(),
                             self.binary.read_bytes())
            self.assertTrue(archive.getmember("payload/bin/armhf/pikafish").mode & 0o111)
            for hook in ["install.sh", "launch.sh", "uninstall.sh", "scriptlets/xiangqi.sh"]:
                self.assertTrue(archive.getmember(hook).mode & 0o111)

    def test_modified_upstream_archive_requires_matching_checksum(self):
        with self.assertRaisesRegex(ValueError, "SHA256"):
            packager.package(self.zip, self.binary, self.output)
        self.assertFalse(self.output.exists())

    def test_rejects_archive_path_traversal(self):
        with zipfile.ZipFile(self.zip, "a") as archive:
            archive.writestr("../../unrelated", b"do not extract")
        with self.assertRaisesRegex(ValueError, "Unsafe"):
            self.build()
        self.assertFalse(self.output.exists())

    def test_rejects_non_arm32_binary(self):
        self.binary.write_bytes(b"not a Kindle binary")
        with self.assertRaisesRegex(ValueError, "ARM32"):
            self.build()


if __name__ == "__main__":
    unittest.main()
