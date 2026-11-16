#!/usr/bin/env python3
"""Package the focused UI with the verified upstream Kindle runtime."""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import shutil
import stat
import tarfile
import tempfile
import zipfile

UPSTREAM_SHA256 = "9b8e0b5cd9768e62c3d2cc320871bbe57b7b27420101c8e11b9ff0d0bb169048"
ROOT = Path(__file__).resolve().parent


def package(extension_zip, binary, output, expected_sha256=UPSTREAM_SHA256):
    extension_zip, binary, output = map(Path, (extension_zip, binary, output))
    if hashlib.sha256(extension_zip.read_bytes()).hexdigest() != expected_sha256:
        raise ValueError("Upstream archive SHA256 mismatch")
    elf = binary.read_bytes()[:52]
    if len(elf) < 52 or elf[:6] != b"\x7fELF\x01\x01" or int.from_bytes(elf[18:20], "little") != 40:
        raise ValueError("App binary must be a little-endian ARM32 ELF")
    with tempfile.TemporaryDirectory(prefix="chess-kpm-pack-") as scratch:
        stage = Path(scratch)
        shutil.copytree(ROOT / "kpm", stage, dirs_exist_ok=True)
        payload = stage / "payload"
        payload.mkdir()
        with zipfile.ZipFile(extension_zip) as upstream:
            for member in upstream.infolist():
                path = PurePosixPath(member.filename)
                if path.is_absolute() or ".." in path.parts:
                    raise ValueError("Unsafe upstream archive path")
                prefix = ("extensions", "exact-chinesechess")
                if path.parts[:2] != prefix or len(path.parts) < 3:
                    continue
                relative = PurePosixPath(*path.parts[2:])
                if relative.parts[0] not in {"bin", "lib", "assets", "LICENSES", "NOTICE.txt",
                                            "PIKAFISH.txt", "ASSETS.txt", "README-package.txt"}:
                    continue
                mode = member.external_attr >> 16
                if stat.S_ISLNK(mode):
                    raise ValueError("Unexpected upstream archive symlink")
                destination = payload.joinpath(*relative.parts)
                if member.is_dir():
                    destination.mkdir(parents=True, exist_ok=True)
                else:
                    destination.parent.mkdir(parents=True, exist_ok=True)
                    destination.write_bytes(upstream.read(member))
                    destination.chmod((mode & 0o777) or 0o644)
        app = payload / "bin/armhf/exact-chinesechess"
        shutil.copyfile(binary, app)
        app.chmod(0o755)
        for required in ["bin/armhf/pikafish", "bin/armhf/pikafish.nnue",
                         "lib/armhf/ld-linux-armhf.so.3", "assets/xiangqi/board.png"]:
            if not (payload / required).is_file():
                raise ValueError("Missing runtime asset: " + required)
        (payload / "bin/armhf/pikafish").chmod(0o755)
        (payload / "lib/armhf/ld-linux-armhf.so.3").chmod(0o755)
        for script in stage.rglob("*.sh"):
            script.chmod(0o755)
        (stage / "SOURCE.txt").write_text(
            "Based on jckhng/exact-chinesechess v0.1.3, GPL-3.0-or-later.\n"
            "Upstream: https://github.com/jckhng/exact-chinesechess/tree/v0.1.3\n"
            "Local modifications: focused touch UI and KPM integration.\n"
            "Corresponding modified source accompanies this package as source.tar.gz.\n"
            "Pikafish and runtime notices are preserved under payload/LICENSES.\n")
        manifest = json.loads((stage / "manifest.json").read_text())
        assert manifest["manifest_version"] == 2
        output.parent.mkdir(parents=True, exist_ok=True)
        with tarfile.open(output, "w:gz") as archive:
            for item in sorted(stage.iterdir()):
                archive.add(item, arcname=item.name)
    return output


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--extension-zip", type=Path, required=True)
    parser.add_argument("--app-binary", type=Path, default=ROOT / "exact-chinesechess")
    parser.add_argument("--output", type=Path,
                        default=ROOT / "release/xiangqi.kpkg")
    args = parser.parse_args()
    result = package(args.extension_zip, args.app_binary, args.output)
    print(result)
    print(hashlib.sha256(result.read_bytes()).hexdigest())
