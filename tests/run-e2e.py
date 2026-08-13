#!/usr/bin/env python3
# This file is part of the dosbox-automation Project.
# License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
#

"""dosbox-automation end-to-end test runner.

Run integration and E2E tests against a live headless DOSBox instance.

Usage:
    python3 tests/run-e2e.py                  # run all available tests
    python3 tests/run-e2e.py --category api   # API contract tests only
    python3 tests/run-e2e.py --category lua   # Lua endpoint + function tests
    python3 tests/run-e2e.py --category e2e   # E2E installer tests only
    python3 tests/run-e2e.py --test doom-shareware  # single E2E test by slug
    python3 tests/run-e2e.py --list           # list available tests and status
    python3 tests/run-e2e.py --list-games     # list game manifests and disk availability

Environment:
    DOSBOX_BIN    Path to dosbox binary (default: build/debug-linux/dosbox)

Categories:
    api     API contract tests (test_api_contract.py)
    lua     Lua REST endpoint and function tests (test_lua_api.py, test_lua_functions.py)
    capture ZMBV capture endpoint tests (test_capture_api.py)
    e2e     Manifest-driven installer automation (test_e2e_installs.py)
    all     Everything (default)
"""

import argparse
import subprocess
import sys
from pathlib import Path

try:
    import tomllib
except ImportError:
    try:
        import tomli as tomllib
    except ImportError:
        tomllib = None

TESTS_DIR = Path(__file__).resolve().parent / "integration"
DISKS_DIR = Path(__file__).resolve().parent / "files" / "disks"

CATEGORIES = {
    "api": ["test_api_contract.py"],
    "lua": ["test_lua_api.py", "test_lua_functions.py"],
    "capture": ["test_capture_api.py"],
    "e2e": ["test_e2e_installs.py"],
}


def list_games():
    if not DISKS_DIR.exists():
        print("No game manifests found.")
        return
    if not tomllib:
        print("tomllib/tomli not available, cannot parse manifests.")
        return

    for manifest_path in sorted(DISKS_DIR.glob("*/manifest.toml")):
        slug = manifest_path.parent.name
        try:
            with open(manifest_path, "rb") as f:
                data = tomllib.load(f)
            images = data.get("discs", {}).get("images", [])
            present = all(
                (manifest_path.parent / img).exists() for img in images
            )
            status = "ready" if present else "missing disks"
            license_type = data.get("game", {}).get("license", "unknown")
            media = data.get("game", {}).get("media", "unknown")
            name = data.get("game", {}).get("name", slug)
            print(
                f"  {slug:30s} {media:12s} {license_type:12s} "
                f"[{status}]  {name}"
            )
        except Exception as e:
            print(f"  {slug:30s} [error: {e}]")


def list_tests():
    for cat, files in CATEGORIES.items():
        for f in files:
            path = TESTS_DIR / f
            status = "exists" if path.exists() else "not yet"
            print(f"  [{cat:8s}] {f:40s} [{status}]")


def main():
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument(
        "--category", "-c",
        choices=list(CATEGORIES.keys()) + ["all"],
        default="all",
        help="test category to run (default: all)",
    )
    parser.add_argument(
        "--test", "-t",
        help="run a single E2E test by game slug",
    )
    parser.add_argument(
        "--list", "-l", action="store_true",
        help="list available test files",
    )
    parser.add_argument(
        "--list-games", action="store_true",
        help="list game manifests and disk availability",
    )
    parser.add_argument(
        "--verbose", "-v", action="store_true",
        help="verbose pytest output",
    )

    args = parser.parse_args()

    if args.list_games:
        list_games()
        return

    if args.list:
        list_tests()
        return

    # Build the pytest command.
    cmd = [sys.executable, "-m", "pytest"]
    if args.verbose:
        cmd.append("-v")
    cmd.append("--tb=short")

    if args.test:
        cmd.append("test_e2e_installs.py")
        cmd.extend(["-k", args.test])
    elif args.category == "all":
        for files in CATEGORIES.values():
            for f in files:
                if (TESTS_DIR / f).exists():
                    cmd.append(f)
    else:
        for f in CATEGORIES[args.category]:
            if (TESTS_DIR / f).exists():
                cmd.append(f)

    result = subprocess.run(cmd, cwd=str(TESTS_DIR))
    sys.exit(result.returncode)


if __name__ == "__main__":
    main()
