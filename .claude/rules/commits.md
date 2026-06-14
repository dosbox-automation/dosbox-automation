---
description: Git commit rules
globs: "**/*"
---

# Commits
- do not add co-authored-by or similar in the commits
- group related changes into one commit. a fix that touches formatting or documentation around it,
  that is one commit, not multiple.
- no commit prefixes.
- commit messages are short and terse. 
- commit messages are written in natural language, plain direct.
- do not include typographical characters, that look good on layouts but are a pain otherwise.
- verify all commits compile: `scripts/tools/compile-commits.sh`.
- vou are not allowed to do commits at will, they have to be accept by a human.

