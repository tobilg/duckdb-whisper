#!/usr/bin/env python3

import re
import sys
from pathlib import Path


VERSION = r"(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)"
VERSION_LOCATIONS = (
    ("CMakeLists.txt", re.compile(rf"project\(\$\{{TARGET_NAME\}} VERSION (?P<version>{VERSION})\)")),
    ("extension_config.cmake", re.compile(rf"EXTENSION_VERSION v(?P<version>{VERSION})")),
    ("src/whisper_extension.cpp", re.compile(rf'return "v(?P<version>{VERSION})";')),
    ("src/functions/utility_functions.cpp", re.compile(rf'#define EXT_VERSION_WHISPER "(?P<version>{VERSION})"')),
    ("test/sql/whisper.test", re.compile(rf"whisper extension v(?P<version>{VERSION}) \(%")),
)


def fail(message: str) -> None:
    print(f"error: {message}", file=sys.stderr)
    raise SystemExit(2)


def main() -> None:
    if len(sys.argv) != 2 or re.fullmatch(VERSION, sys.argv[1]) is None:
        fail("version must use X.Y.Z format (for example, 0.5.0)")

    new_version = sys.argv[1]
    project_root = Path(__file__).resolve().parent.parent
    files: dict[Path, tuple[str, re.Match[str]]] = {}

    for relative_path, pattern in VERSION_LOCATIONS:
        path = project_root / relative_path
        content = path.read_text()
        matches = list(pattern.finditer(content))
        if len(matches) != 1:
            fail(f"expected exactly one version reference in {relative_path}, found {len(matches)}")
        files[path] = (content, matches[0])

    current_versions = {match.group("version") for _, match in files.values()}
    if len(current_versions) != 1:
        details = ", ".join(
            f"{path.relative_to(project_root)}={match.group('version')}"
            for path, (_, match) in files.items()
        )
        fail(f"version references are inconsistent: {details}")

    current_version = current_versions.pop()
    if current_version == new_version:
        print(f"Extension version is already {new_version}; all references are consistent.")
        return

    updated_files: dict[Path, str] = {}
    for path, (content, match) in files.items():
        start, end = match.span("version")
        updated_files[path] = content[:start] + new_version + content[end:]

    for path, content in updated_files.items():
        path.write_text(content)

    print(f"Updated extension version from {current_version} to {new_version} in {len(updated_files)} files.")


if __name__ == "__main__":
    main()
