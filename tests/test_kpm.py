import base64
import os
from pathlib import Path
import subprocess
import shutil
import signal
import struct
import tempfile
import time
import unittest

ROOT = Path(__file__).resolve().parents[1]


class KpmHooksTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="chess-kpm-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.documents = self.root / "documents"
        self.data = self.root / "data"
        self.env = dict(os.environ, EXACT_CHINESECHESS_DOCUMENTS_DIR=str(self.documents),
                        EXACT_CHINESECHESS_DATA_DIR=str(self.data))

    def hook(self, name, *args, success=True):
        result = subprocess.run(["sh", str(ROOT / "kpm" / name), *args],
                                cwd=ROOT, env=self.env, capture_output=True, text=True)
        if success:
            self.assertEqual(result.returncode, 0, result.stderr)
        else:
            self.assertNotEqual(result.returncode, 0)
        return result

    def test_install_places_runnable_library_entry(self):
        self.hook("install.sh")
        entry = self.documents / "xiangqi.sh"
        self.assertTrue(os.access(entry, os.X_OK))
        self.assertEqual(entry.read_bytes(), (ROOT / "kpm/scriptlets/xiangqi.sh").read_bytes())

    def test_installed_entry_embeds_matching_png_cover_in_first_six_lines(self):
        self.hook("install.sh")
        lines = (self.documents / "xiangqi.sh").read_text().splitlines()
        header = lines[:6]
        icons = [line for line in header if line.startswith("# Icon: ")]
        self.assertEqual(len(icons), 1, "Scriptlet parser only reads its first six lines")
        prefix = "# Icon: data:image/png;base64,"
        self.assertTrue(icons[0].startswith(prefix))
        cover = base64.b64decode(icons[0][len(prefix):], validate=True)
        self.assertEqual(cover, (ROOT / "assets/branding/kindle-cover-thumbnail.png").read_bytes())
        self.assertEqual(cover[:8], b"\x89PNG\r\n\x1a\n")
        self.assertEqual(cover[8:16], b"\x00\x00\x00\rIHDR")
        self.assertEqual(struct.unpack(">II", cover[16:24]), (256, 384))
        self.assertEqual(header[0], "#!/bin/sh")
        self.assertIn("# Name: 象棋", header)
        self.assertIn("# Package: exactchinesechess", header)
        self.assertIn("# DontUseFBInk", header)

    def test_upgrade_keeps_saved_game(self):
        self.hook("install.sh")
        save = self.data / "game.save"
        save.write_text("a game the player must not lose")
        self.hook("uninstall.sh", "upgrade")
        self.hook("install.sh", "upgrade")
        self.assertEqual(save.read_text(), "a game the player must not lose")

    def test_uninstall_removes_owned_entry_and_keeps_saved_game(self):
        self.hook("install.sh")
        save = self.data / "game.save"
        save.write_text("keep")
        self.hook("uninstall.sh")
        self.assertFalse((self.documents / "xiangqi.sh").exists())
        self.assertEqual(save.read_text(), "keep")

    def test_user_changed_entry_is_neither_overwritten_nor_deleted(self):
        self.hook("install.sh")
        entry = self.documents / "xiangqi.sh"
        entry.write_text("user script")
        self.hook("uninstall.sh")
        self.assertEqual(entry.read_text(), "user script")
        self.hook("install.sh", success=False)
        self.assertEqual(entry.read_text(), "user script")

    def test_repeated_launch_keeps_one_running_game(self):
        package = self.root / "package"
        shutil.copytree(ROOT / "kpm", package)
        binary = package / "payload/bin/armhf/exact-chinesechess"
        binary.parent.mkdir(parents=True)
        binary.write_text('#!/bin/sh\nprintf "started\\n" >> "$EXACT_CHINESECHESS_DATA_DIR/starts"\nsleep 10\n')
        binary.chmod(0o755)
        children = []
        try:
            first = subprocess.Popen(["sh", str(package / "launch.sh")], env=self.env,
                                     start_new_session=True)
            children.append(first)
            marker = self.data / "starts"
            deadline = time.monotonic() + 3
            while not marker.exists() and time.monotonic() < deadline:
                time.sleep(0.01)
            self.assertTrue(marker.exists())
            second = subprocess.Popen(["sh", str(package / "launch.sh")], env=self.env,
                                      start_new_session=True)
            children.append(second)
            try:
                self.assertEqual(second.wait(timeout=1), 0)
            except subprocess.TimeoutExpired:
                self.fail("Second launch started another game instead of returning")
            self.assertEqual(marker.read_text(), "started\n")
        finally:
            for child in children:
                try:
                    os.killpg(child.pid, signal.SIGTERM)
                except ProcessLookupError:
                    pass
                child.wait()


if __name__ == "__main__":
    unittest.main()
