import base64
import os
from pathlib import Path
import subprocess
import shutil
import signal
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
        entry = self.documents / "chinese-chess.sh"
        self.assertTrue(os.access(entry, os.X_OK))
        self.assertEqual(entry.read_bytes(), (ROOT / "kpm/scriptlets/xiangqi.sh").read_bytes())

    def test_installed_entry_embeds_matching_jpeg_cover_in_first_six_lines(self):
        self.hook("install.sh")
        lines = (self.documents / "chinese-chess.sh").read_text().splitlines()
        header = lines[:6]
        icons = [line for line in header if line.startswith("# Icon: ")]
        self.assertEqual(len(icons), 1, "Scriptlet parser only reads its first six lines")
        prefix = "# Icon: data:image/jpeg;base64,"
        self.assertTrue(icons[0].startswith(prefix))
        cover = base64.b64decode(icons[0][len(prefix):], validate=True)
        self.assertEqual(cover, (ROOT / "assets/branding/kindle-cover-thumbnail.jpeg").read_bytes())
        self.assertTrue(cover.startswith(b"\xff\xd8") and cover.endswith(b"\xff\xd9"))
        self.assertEqual(header[0], "#!/bin/sh")
        self.assertIn("# Name: 象棋", header)
        self.assertIn("# Package: exactchinesechess", header)
        self.assertIn("# DontUseFBInk", header)

    def test_install_migrates_only_an_owned_legacy_entry(self):
        self.documents.mkdir()
        legacy = self.documents / "xiangqi.sh"
        legacy.write_bytes((ROOT / "kpm/scriptlets/xiangqi.sh").read_bytes())
        self.hook("install.sh")
        self.assertFalse(legacy.exists())
        self.assertTrue((self.documents / "chinese-chess.sh").exists())
        legacy.write_text("user script")
        self.hook("install.sh")
        self.hook("uninstall.sh")
        self.assertEqual(legacy.read_text(), "user script")

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
        self.assertFalse((self.documents / "chinese-chess.sh").exists())
        self.assertEqual(save.read_text(), "keep")

    def test_user_changed_entry_is_neither_overwritten_nor_deleted(self):
        self.hook("install.sh")
        entry = self.documents / "chinese-chess.sh"
        entry.write_text("user script")
        self.hook("uninstall.sh")
        self.assertEqual(entry.read_text(), "user script")
        self.hook("install.sh", success=False)
        self.assertEqual(entry.read_text(), "user script")

    def test_existing_process_exit_starts_replacement(self):
        package = self.root / "package"
        shutil.copytree(ROOT / "kpm", package)
        binary = package / "payload/bin/armhf/exact-chinesechess"
        binary.parent.mkdir(parents=True)
        binary.write_text('''#!/usr/bin/env python3
import os, sys, time
from pathlib import Path
data = Path(os.environ["EXACT_CHINESECHESS_DATA_DIR"])
if "--activate" in sys.argv:
    (data / "activation-failed").touch()
    sys.exit(1)
marker = data / "starts"
replacement = marker.exists()
with marker.open("a") as output:
    output.write("replacement\\n" if replacement else "first\\n")
time.sleep(0.4)
''')
        binary.chmod(0o755)
        first = subprocess.Popen(["sh", str(package / "launch.sh")], env=self.env)
        second = None
        try:
            marker = self.data / "starts"
            deadline = time.monotonic() + 3
            while not marker.exists() and time.monotonic() < deadline:
                time.sleep(0.01)
            self.assertTrue(marker.exists())
            second = subprocess.Popen(["sh", str(package / "launch.sh")], env=self.env)
            self.assertEqual(first.wait(timeout=3), 0)
            self.assertEqual(second.wait(timeout=4), 0)
            self.assertTrue((self.data / "activation-failed").exists())
            self.assertEqual(marker.read_text(), "first\nreplacement\n")
        finally:
            for child in [first, second]:
                if child is not None:
                    if child.poll() is None:
                        child.terminate()
                    child.wait(timeout=2)

    def test_repeated_launch_keeps_one_running_game(self):
        package = self.root / "package"
        shutil.copytree(ROOT / "kpm", package)
        binary = package / "payload/bin/armhf/exact-chinesechess"
        binary.parent.mkdir(parents=True)
        binary.write_text('''#!/usr/bin/env python3
import os, sys, time
from pathlib import Path
data = Path(os.environ["EXACT_CHINESECHESS_DATA_DIR"])
if "--activate" in sys.argv:
    attempts = data / "activation-attempts"
    count = int(attempts.read_text()) if attempts.exists() else 0
    attempts.write_text(str(count + 1))
    if count == 0:
        sys.exit(1)  # The first process exists before its window is ready.
    (data / "activated").touch()
    sys.exit(0)
with (data / "starts").open("a") as output:
    output.write("started\\n")
time.sleep(10)
''')
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
            activated = self.data / "activated"
            deadline = time.monotonic() + 3
            while not activated.exists() and time.monotonic() < deadline:
                time.sleep(0.01)
            self.assertTrue(activated.exists(), "Repeated launch must reactivate the game")
            self.assertGreaterEqual(int((self.data / "activation-attempts").read_text()), 2)
            self.assertIsNone(second.poll(), "Keep the scriptlet open while the game runs")
            self.assertEqual(marker.read_text(), "started\n")
            first.terminate()
            first.wait(timeout=2)
            self.assertEqual(second.wait(timeout=3), 0)
        finally:
            for child in children:
                if child.poll() is None:
                    child.terminate()
                child.wait(timeout=2)


if __name__ == "__main__":
    unittest.main()
