#!/usr/bin/env python3
import argparse
import re
import zipfile
from pathlib import Path

def version(root: Path) -> str:
    text = (root / "core/version_generated.gen.h").read_text(encoding="utf-8")
    def value(name: str) -> str:
        match = re.search(rf"^#define {name} (.+)$", text, re.MULTILINE)
        if not match:
            raise RuntimeError(f"Missing macro: {name}")
        return match.group(1).strip().strip('"')
    major, minor, patch = value("VERSION_MAJOR"), value("VERSION_MINOR"), value("VERSION_PATCH")
    number = f"{major}.{minor}" + (f".{patch}" if patch != "0" else "")
    return f"{number}.{value('VERSION_STATUS')}{value('VERSION_MODULE_CONFIG')}"

def zip_tree(source: Path, output: Path) -> None:
    output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(output, "w", zipfile.ZIP_DEFLATED) as archive:
        for path in sorted(source.rglob("*")):
            if path.is_file():
                archive.write(path, path.relative_to(source).as_posix())

def zip_vita(app: Path, eboot: Path, output: Path) -> None:
    output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(output, "w", zipfile.ZIP_DEFLATED) as archive:
        archive.write(eboot, "eboot.bin")
        for path in sorted(app.rglob("*")):
            if path.is_file():
                archive.write(path, path.relative_to(app).as_posix())

def bundle(root: Path, output: Path, mappings: list[str]) -> None:
    output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(output, "w", zipfile.ZIP_DEFLATED) as archive:
        archive.writestr("version.txt", version(root) + "\n")
        for mapping in mappings:
            name, source = mapping.split("=", 1)
            archive.write(source, name)

def main() -> None:
    parser = argparse.ArgumentParser()
    sub = parser.add_subparsers(dest="command", required=True)
    tree = sub.add_parser("zip-tree")
    tree.add_argument("source", type=Path)
    tree.add_argument("output", type=Path)
    vita = sub.add_parser("vita")
    vita.add_argument("app", type=Path)
    vita.add_argument("eboot", type=Path)
    vita.add_argument("output", type=Path)
    pack = sub.add_parser("bundle")
    pack.add_argument("root", type=Path)
    pack.add_argument("output", type=Path)
    pack.add_argument("mapping", nargs="+")
    args = parser.parse_args()
    if args.command == "zip-tree":
        zip_tree(args.source, args.output)
    elif args.command == "vita":
        zip_vita(args.app, args.eboot, args.output)
    else:
        bundle(args.root, args.output, args.mapping)

if __name__ == "__main__":
    main()
