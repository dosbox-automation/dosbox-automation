#!/usr/bin/env python3

# This file is part of the dosbox-automation Project.
# License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
#

"""Emit a target_precompile_headers() block from transitive #include analysis.

Resolves includes through project/third-party headers, estimates parse cost
from file size, and selects headers used by many sources that are large (or
standard/gtest headers used by many sources).

Source discovery:
  default   sources listed in add_executable(<target> ...) of --cmake
  --crawl   every C++ source under --root
"""
from __future__ import annotations

import argparse
import re
import sys
from collections import Counter, defaultdict
from dataclasses import dataclass
from fnmatch import fnmatch
from functools import lru_cache
from pathlib import Path
from typing import Callable

INCLUDE_RE = re.compile(r'^\s*#\s*include\s*([<"])([^>"]+)[>"]', re.MULTILINE)
CMAKE_COMMENT_RE = re.compile(r"#.*$", re.MULTILINE)
SOURCE_SUFFIXES = frozenset({".cpp", ".cc", ".cxx"})
HEADER_SUFFIXES = frozenset({".h", ".hh", ".hpp", ".hxx", ".inl", ".ipp"})
FRAMEWORK_PREFIXES = ("gtest/", "gmock/")
DEFAULT_EXCLUDES = ("*/.git/*",)
BEGIN = "# BEGIN PCH"
END = "# END PCH"
BLOCK_RE = re.compile(rf"^{BEGIN}.*?^{END}[^\n]*$", re.MULTILINE | re.DOTALL)


@dataclass(frozen=True)
class Include:
    name: str
    angled: bool

    def render(self) -> str:
        return f"<{self.name}>" if self.angled else f'"{self.name}"'


@dataclass(frozen=True)
class Closure:
    files: frozenset[Path]       # resolved headers/sources reached
    leaves: frozenset[Include]   # unresolved includes (std, system, unknown)


@dataclass(frozen=True)
class Entry:
    text: str
    angled: bool
    count: int
    size: int


DirectFn = Callable[[Path], tuple[tuple[Include, "Path | None"], ...]]


def is_excluded(path: Path, excludes: tuple[str, ...]) -> bool:
    posix = path.as_posix()
    return any(fnmatch(posix, pat) for pat in excludes)


def sources_from_target(cmake_text: str, target: str, base: Path) -> list[Path]:
    text = CMAKE_COMMENT_RE.sub("", cmake_text)
    match = re.search(rf"add_executable\(\s*{re.escape(target)}\b(.*?)\)", text, re.DOTALL)
    if match is None:
        return []
    return [(base / t).resolve() for t in match.group(1).split()
            if Path(t).suffix in SOURCE_SUFFIXES]


def sources_from_crawl(root: Path, excludes: tuple[str, ...]) -> list[Path]:
    return sorted(p.resolve() for p in root.rglob("*")
                  if p.suffix in SOURCE_SUFFIXES and not is_excluded(p, excludes))


def build_name_index(dirs: tuple[Path, ...], excludes: tuple[str, ...]) -> dict[str, tuple[Path, ...]]:
    index: dict[str, list[Path]] = defaultdict(list)
    for d in dirs:
        for p in d.rglob("*"):
            if p.suffix in HEADER_SUFFIXES and p.is_file() and not is_excluded(p, excludes):
                index[p.name].append(p.resolve())
    return {k: tuple(v) for k, v in index.items()}


def resolve(name: str, includer: Path, incdirs: tuple[Path, ...],
            name_index: dict[str, tuple[Path, ...]]) -> Path | None:
    for d in (includer.parent, *incdirs):
        candidate = d / name
        if candidate.is_file():
            return candidate.resolve()
    tail = "/" + Path(name).as_posix()
    matches = [p for p in name_index.get(Path(name).name, ()) if p.as_posix().endswith(tail)]
    return min(matches, key=lambda p: len(p.parts)) if matches else None


def parse_includes(text: str) -> frozenset[Include]:
    return frozenset(Include(name=n, angled=b == "<") for b, n in INCLUDE_RE.findall(text))


def make_direct(incdirs: tuple[Path, ...], name_index: dict[str, tuple[Path, ...]]) -> DirectFn:
    @lru_cache(maxsize=None)
    def direct(path: Path) -> tuple[tuple[Include, Path | None], ...]:
        text = path.read_text(encoding="utf-8", errors="replace")
        return tuple((inc, resolve(inc.name, path, incdirs, name_index))
                     for inc in parse_includes(text))
    return direct


def closure_of(start: Path, direct: DirectFn) -> Closure:
    seen: set[Path] = set()
    leaves: set[Include] = set()
    stack = [start]
    while stack:
        for inc, resolved in direct(stack.pop()):
            if resolved is None:
                leaves.add(inc)
            elif resolved not in seen and resolved != start:
                seen.add(resolved)
                stack.append(resolved)
    return Closure(frozenset(seen), frozenset(leaves))


def is_std_like(inc: Include) -> bool:
    return inc.angled and ("." not in inc.name or inc.name.startswith(FRAMEWORK_PREFIXES))


def leaf_allowed(inc: Include, c_headers: bool) -> bool:
    if Path(inc.name).suffix in SOURCE_SUFFIXES:
        return False
    return is_std_like(inc) or (inc.angled and c_headers)


def spelling_for(path: Path, incdirs: tuple[Path, ...]) -> str | None:
    for d in sorted(incdirs, key=lambda d: len(d.parts), reverse=True):
        if path.is_relative_to(d):
            return path.relative_to(d).as_posix()
    return None


def prune_redundant(files: list[Path], direct: DirectFn) -> list[Path]:
    reachable = {f: closure_of(f, direct).files for f in files}
    return [f for f in files
            if not any(f in reachable[g] for g in files if g != f)]


def render_block(target: str, entries: list[Entry]) -> str:
    body = "\n".join(f"  {e.text}" for e in entries)
    return (
        f"{BEGIN} (generated by gen_pch.py; re-run when includes change)\n"
        f"target_precompile_headers({target} PRIVATE\n{body}\n)\n"
        f"{END}"
    )


def splice(cmake_text: str, block: str) -> str | None:
    if BLOCK_RE.search(cmake_text) is None:
        return None
    return BLOCK_RE.sub(lambda _: block, cmake_text, count=1)


def kb(n: int) -> str:
    return f"{n / 1024:8.0f} KB"


def parse_args(argv: list[str] | None) -> argparse.Namespace:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--cmake", type=Path, default=Path("CMakeLists.txt"))
    p.add_argument("--target", default="dosbox_tests")
    p.add_argument("--crawl", action="store_true")
    p.add_argument("--root", type=Path, default=None, help="crawl root (default: cmake dir)")
    p.add_argument("--include-dir", type=Path, action="append", default=[],
                   help="header search/index dir, like -I (repeatable; default: cmake dir)")
    p.add_argument("--exclude", action="append", default=[], metavar="GLOB")
    p.add_argument("--min-files", type=int, default=8,
                   help="min sources that must reach a header (default 8)")
    p.add_argument("--min-bytes", type=int, default=50_000,
                   help="min size of a resolved header to select it (default 50000)")
    p.add_argument("--include-c-headers", action="store_true",
                   help="allow unresolved <foo.h> headers such as <unistd.h>")
    p.add_argument("--top", type=int, default=15, help="rows in the cost report")
    mode = p.add_mutually_exclusive_group()
    mode.add_argument("--write", action="store_true")
    mode.add_argument("--check", action="store_true")
    return p.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    cmake_text = args.cmake.read_text(encoding="utf-8")
    base = args.cmake.parent.resolve()
    excludes = (*DEFAULT_EXCLUDES, *args.exclude)
    incdirs = tuple(d.resolve() for d in (args.include_dir or [base]))

    sources = (sources_from_crawl((args.root or base).resolve(), excludes) if args.crawl
               else sources_from_target(cmake_text, args.target, base))
    if not sources:
        print("error: no sources found", file=sys.stderr)
        return 2
    missing = [str(s) for s in sources if not s.is_file()]
    if missing:
        print(f"error: missing sources: {', '.join(missing)}", file=sys.stderr)
        return 2

    direct = make_direct(incdirs, build_name_index(incdirs, excludes))
    closures = {s: closure_of(s, direct) for s in sources}

    sizes: dict[Path, int] = {}
    def size(p: Path) -> int:
        if p not in sizes:
            sizes[p] = p.stat().st_size
        return sizes[p]

    file_counts = Counter(f for c in closures.values() for f in c.files)
    leaf_counts = Counter(i for c in closures.values() for i in c.leaves)

    big = [f for f, n in file_counts.items()
           if n >= args.min_files and size(f) >= args.min_bytes
           and f.suffix not in SOURCE_SUFFIXES]
    kept = prune_redundant(big, direct)

    entries: list[Entry] = []
    for f in kept:
        spelling = spelling_for(f, incdirs)
        if spelling is None:
            print(f"warning: {f} is outside every --include-dir; skipped", file=sys.stderr)
            continue
        entries.append(Entry(f'"{spelling}"', False, file_counts[f], size(f)))
    for inc, n in leaf_counts.items():
        if n >= args.min_files and leaf_allowed(inc, args.include_c_headers):
            entries.append(Entry(inc.render(), True, n, 0))
    entries.sort(key=lambda e: (not e.angled, e.text))
    if not entries:
        print("error: no headers met the thresholds", file=sys.stderr)
        return 2

    covered = frozenset(f for f in kept
                        for f in (f, *closure_of(f, direct).files))
    before = sum(size(f) for c in closures.values() for f in c.files)
    after = (sum(size(f) for c in closures.values() for f in c.files - covered)
             + sum(size(f) for f in covered))

    print(f"{len(sources)} sources, {len(sizes) or len(file_counts)} headers reached", file=sys.stderr)
    print(f"estimated header text parsed: {before / 1e6:.1f} MB -> {after / 1e6:.1f} MB "
          f"(rough; file bytes, before preprocessing)", file=sys.stderr)
    print("\ntop resolved headers by (sources x size):", file=sys.stderr)
    ranked = sorted(file_counts, key=lambda f: file_counts[f] * size(f), reverse=True)
    chosen = set(kept)
    for f in [f for f in ranked if f.suffix not in SOURCE_SUFFIXES][:args.top]:
        mark = "*" if f in chosen else ("~" if f in covered else " ")
        print(f"  {mark} {file_counts[f]:4d} src {kb(size(f))}  {spelling_for(f, incdirs) or f}",
              file=sys.stderr)
    print("  (* selected, ~ covered via a selected header)", file=sys.stderr)

    unresolved = [(i, n) for i, n in leaf_counts.most_common()
                  if n >= args.min_files and not is_std_like(i)]
    if unresolved:
        print("\nunresolved non-std includes (size unknown; add --include-dir to see cost):",
              file=sys.stderr)
        for inc, n in unresolved[:args.top]:
            print(f"    {n:4d} src  {inc.render()}", file=sys.stderr)

    block = render_block(args.target, entries)
    if not (args.write or args.check):
        print(block)
        return 0

    updated = splice(cmake_text, block)
    if updated is None:
        print(f"error: '{BEGIN}' / '{END}' markers not found in {args.cmake}", file=sys.stderr)
        return 2
    if args.check:
        if updated != cmake_text:
            print("PCH block is stale; run with --write", file=sys.stderr)
            return 1
        return 0
    if updated != cmake_text:
        args.cmake.write_text(updated, encoding="utf-8")
        print(f"updated {args.cmake}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
