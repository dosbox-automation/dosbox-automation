---
description: Git commit rules
globs: "**/*"
---

# Commits

- All commits must compile individually
- Group related changes into one commit. A fix that touches formatting or
  docs around it is one commit, not three.
- No commit prefixes. Just say what changed.
- Write commit messages like the docs-style rules say: plain, direct, no
  AI patterns. First line is a short summary, body explains the why if
  it's not obvious.
- Never add Co-Authored-By lines to commits
- Verify all commits compile: `scripts/tools/compile-commits.sh`
