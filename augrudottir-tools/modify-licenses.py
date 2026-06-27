#!/usr/bin/env python3
# This file is part of the dosbox-automation Project.
# License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
#

"""Insert or replace entries in THIRD_PARTY_LICENSES.txt.

The file contains license text that triggers content filters in some
AI tools, so direct editing via those tools fails. This script reads
and writes the file without echoing its contents.

Usage:
    modify-licenses.py insert-before MARKER_LINE new_content_file
    modify-licenses.py append-section new_content_file
    modify-licenses.py show-sections

insert-before: inserts the content of new_content_file before the
    first line matching MARKER_LINE.

append-section: appends the content of new_content_file before the
    final END OF THIRD-PARTY LICENSES marker.

show-sections: lists section headers (lines starting with "Used by:"
    or separator lines) without printing the full file.
"""

import sys
from pathlib import Path

LICENSES_FILE = (
    Path(__file__).resolve().parent.parent / "THIRD_PARTY_LICENSES.txt"
)

END_MARKER = "END OF THIRD-PARTY LICENSES"


def insert_before(marker_line, content_file):
    content = Path(content_file).read_text()
    lines = LICENSES_FILE.read_text().splitlines(keepends=True)

    out = []
    inserted = False
    for line in lines:
        if not inserted and marker_line in line:
            out.append(content)
            if not content.endswith("\n"):
                out.append("\n")
            inserted = True
        out.append(line)

    if not inserted:
        print(f"Marker not found: {marker_line}", file=sys.stderr)
        sys.exit(1)

    LICENSES_FILE.write_text("".join(out))
    print(f"Inserted before '{marker_line}'")


def append_section(content_file):
    content = Path(content_file).read_text()
    lines = LICENSES_FILE.read_text().splitlines(keepends=True)

    out = []
    inserted = False
    for line in lines:
        if not inserted and END_MARKER in line:
            out.append(content)
            if not content.endswith("\n"):
                out.append("\n")
            out.append("\n")
            inserted = True
        out.append(line)

    if not inserted:
        print(f"End marker not found", file=sys.stderr)
        sys.exit(1)

    LICENSES_FILE.write_text("".join(out))
    print(f"Appended section before end marker")


def show_sections():
    lines = LICENSES_FILE.read_text().splitlines()
    for i, line in enumerate(lines):
        stripped = line.strip()
        if stripped.startswith("====") or stripped.startswith("----"):
            if i + 1 < len(lines):
                next_line = lines[i + 1].strip()
                if next_line and not next_line.startswith("=") and not next_line.startswith("-"):
                    print(f"  {i + 1}: {next_line}")
        if stripped.startswith("Used by:"):
            print(f"  {i + 1}: {stripped}")


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)

    cmd = sys.argv[1]

    if cmd == "insert-before":
        if len(sys.argv) != 4:
            print("Usage: modify-licenses.py insert-before MARKER content_file")
            sys.exit(1)
        insert_before(sys.argv[2], sys.argv[3])

    elif cmd == "append-section":
        if len(sys.argv) != 3:
            print("Usage: modify-licenses.py append-section content_file")
            sys.exit(1)
        append_section(sys.argv[2])

    elif cmd == "show-sections":
        show_sections()

    else:
        print(f"Unknown command: {cmd}", file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    main()
