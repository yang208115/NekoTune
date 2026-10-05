"""Bundle installed NekoTune binaries and their Qt/native runtime dependencies."""

from __future__ import annotations

import argparse
import hashlib
import os
from pathlib import Path
import re
import shutil
import subprocess
import tarfile
import zipfile


def run(*command: str, env: dict[str, str] | None = None) -> str:
    return subprocess.check_output(command, text=True, env=env, errors="replace")


def copy_file(source: Path, destination: Path) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source.resolve(), destination)


def bundle_linux(stage: Path, qt: Path, extra_library_dirs: list[Path]) -> None:
    # Multimedia also installs optional Quick3D modules without their 3D dependencies.
    # Ship the host's 2D UI modules and the multimedia API used by extensions.
    for module in ("QtQml", "QtQuick", "Qt", "QtMultimedia"):
        source = qt / "qml" / module
        if source.exists():
            shutil.copytree(source, stage / "qml" / module, dirs_exist_ok=True)
    for kind in (
        "platforms", "platforminputcontexts", "xcbglintegrations", "imageformats",
        "tls", "sqldrivers", "multimedia", "styles", "iconengines",
        "networkinformation",
    ):
        source = qt / "plugins" / kind
        if source.exists():
            for plugin in source.glob("*.so"):
                if kind == "sqldrivers" and plugin.name != "libqsqlite.so":
                    continue
                if kind == "platforms" and plugin.name not in {
                    "libqxcb.so", "libqoffscreen.so", "libqminimal.so",
                }:
                    continue
                if kind == "imageformats" and not plugin.name.startswith("libq"):
                    continue
                copy_file(plugin, stage / "plugins" / kind / plugin.name)
    library_dir = stage / "lib"
    library_dir.mkdir(exist_ok=True)
    # The loader, libc and graphics drivers belong to the target OS.
    system_library = re.compile(
        r"^(ld-linux|lib(c|m|dl|pthread|rt|resolv|util|nss_[^.]+)\.so"
        r"|lib(GL|EGL|GLX|GLdispatch|OpenGL|drm|gbm|vulkan)\.so)"
    )
    environment = dict(os.environ)
    environment["LD_LIBRARY_PATH"] = os.pathsep.join(
        [str(library_dir), str(qt / "lib"), *map(str, extra_library_dirs),
         environment.get("LD_LIBRARY_PATH", "")]
    )
    pending = [file for file in stage.rglob("*") if file.is_file()
               and (".so" in file.name or file.parent.name == "bin")]
    seen: set[Path] = set()
    while pending:
        binary = pending.pop()
        if binary in seen:
            continue
        with binary.open("rb") as stream:
            if stream.read(4) != b"\x7fELF":
                continue
        seen.add(binary)
        output = run("ldd", str(binary), env=environment)
        if "=> not found" in output:
            raise RuntimeError(f"Unresolved dependency in {binary}:\n{output}")
        for name, filename in re.findall(r"^\s*(\S+) => (/\S+)", output, re.MULTILINE):
            if system_library.match(name):
                continue
            destination = library_dir / name
            if not destination.exists():
                copy_file(Path(filename), destination)
                pending.append(destination)
    launcher = stage / "NekoTune"
    launcher.write_text(
        '#!/bin/sh\nset -eu\n'
        'app_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)\n'
        'export LD_LIBRARY_PATH="$app_dir/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"\n'
        'export QT_PLUGIN_PATH="$app_dir/plugins"\n'
        'export QML_IMPORT_PATH="$app_dir/qml"\n'
        'export QML2_IMPORT_PATH="$app_dir/qml"\n'
        'exec "$app_dir/bin/nekotune" "$@"\n', encoding="utf-8"
    )
    launcher.chmod(0o755)
    (stage / "bin" / "qt.conf").write_text(
        "[Paths]\nPrefix=..\nLibraries=lib\nPlugins=plugins\nQmlImports=qml\n",
        encoding="utf-8",
    )


def bundle_windows(stage: Path, qt: Path) -> None:
    binary_dir = stage / "bin"
    deploy = qt / "bin" / "windeployqt6.exe"
    if not deploy.exists():
        deploy = qt / "bin" / "windeployqt.exe"
    run(str(deploy), "--release", "--no-translations", "--qmldir",
        str(Path("frontend/qt-qml/qml").resolve()), "--dir", str(binary_dir),
        str(binary_dir / "nekotune.exe"))
    # windeployqt copies every SQL plugin, including drivers the app never uses.
    for driver in (binary_dir / "sqldrivers").glob("*.dll"):
        if driver.name.lower() != "qsqlite.dll":
            driver.unlink()
    sources = {file.name.lower(): file for file in (qt / "bin").glob("*.dll")}
    windows = Path(os.environ.get("SystemRoot", "C:/Windows")) / "System32"
    system_dlls = {file.name.lower() for file in windows.glob("*.dll")}
    pending = [file for file in stage.rglob("*")
               if file.suffix.lower() in {".exe", ".dll"}]
    seen: set[Path] = set()
    while pending:
        binary = pending.pop()
        if binary in seen:
            continue
        seen.add(binary)
        output = run("objdump", "-p", str(binary))
        for name in re.findall(r"DLL Name:\s*(\S+)", output):
            key = name.lower()
            destination = binary_dir / name
            if destination.exists():
                continue
            if key in sources:
                copy_file(sources[key], destination)
                pending.append(destination)
            elif key not in system_dlls and not key.startswith(("api-ms-", "ext-ms-")):
                raise RuntimeError(f"Unresolved dependency {name} in {binary}")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--stage", type=Path, required=True)
    parser.add_argument("--qt-prefix", type=Path, required=True)
    parser.add_argument("--library-dir", type=Path, action="append", default=[])
    parser.add_argument("--platform", choices=["linux", "windows"], required=True)
    parser.add_argument("--version", required=True)
    parser.add_argument("--output", type=Path, default=Path("dist"))
    args = parser.parse_args()
    stage = args.stage.resolve()
    qt = args.qt_prefix.resolve()
    if args.platform == "linux":
        bundle_linux(stage, qt, [directory.resolve() for directory in args.library_dir])
    else:
        bundle_windows(stage, qt)
    licenses = stage / "licenses"
    licenses.mkdir(exist_ok=True)
    for filename in ("LICENSE", "README.md"):
        if Path(filename).exists():
            copy_file(Path(filename), stage / filename)
    # Preserve notices from the actual installed distributions.
    for source in (qt / "share/licenses", qt / "Licenses", Path(".ci-qtkeychain/COPYING")):
        if source.is_dir():
            shutil.copytree(source, licenses / source.name, dirs_exist_ok=True)
        elif source.is_file():
            copy_file(source, licenses / "QtKeychain-LICENSE")
    (licenses / "THIRD-PARTY.md").write_text(
        "This distribution includes Qt (LGPL/GPL), QtKeychain (BSD), "
        "FFmpeg and its codec libraries, Node.js and their dependencies.\n"
        "Node.js notices are in libexec/nekotune/NODE-LICENSE.\n"
        "Qt source and licenses: https://code.qt.io/ and https://www.qt.io/licensing/\n"
        "QtKeychain source: https://github.com/frankosterfeld/qtkeychain/tree/0.17.0\n"
        "FFmpeg source and licenses: https://ffmpeg.org/legal.html\n"
        "MSYS2 package sources and notices: https://github.com/msys2/MINGW-packages\n",
        encoding="utf-8",
    )
    args.output.mkdir(parents=True, exist_ok=True)
    name = f"NekoTune-{args.version}-{args.platform}-x64"
    if args.platform == "windows":
        archive = args.output / f"{name}.zip"
        with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as output:
            for file in sorted(stage.rglob("*")):
                if file.is_file():
                    output.write(file, Path(name) / file.relative_to(stage))
    else:
        archive = args.output / f"{name}.tar.gz"
        with tarfile.open(archive, "w:gz") as output:
            output.add(stage, arcname=name)
    with archive.open("rb") as stream:
        checksum = hashlib.file_digest(stream, "sha256").hexdigest()
    archive.with_name(archive.name + ".sha256").write_text(
        f"{checksum}  {archive.name}\n", encoding="utf-8"
    )
    print(f"Packaged {archive} ({archive.stat().st_size:,} bytes)")


if __name__ == "__main__":
    main()
