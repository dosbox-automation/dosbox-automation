#!/bin/bash

# This file is part of the dosbox-automation Project.
# License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
#

# Extra arguments are forwarded to ruff check:
#
#   ./verify-python.sh --fix
#
# ruff reads the repo's ruff.toml. bandit reports medium severity and
# up; deliberate exceptions carry inline "# nosec" tags.

set -euo pipefail

# shellcheck disable=SC2034 # cmd_ref is a nameref, assignments land in the caller's array
find_tool () {
	local -n cmd_ref="$2"
	if command -v "$1" > /dev/null; then
		cmd_ref=("$1")
	elif python3 -c "import $1" 2> /dev/null; then
		cmd_ref=(python3 -m "$1")
	else
		echo "error: $1 not found - pip install $1" >&2
		exit 1
	fi
}

# Union of *.py by extension and file(1) content detection: extension
# alone misses extensionless scripts, content detection misses .py
# files short enough to read as plain text.
list_python_files () {
	{
		git ls-files --cached --others --exclude-standard -- '*.py'
		git ls-files --cached --others --exclude-standard \
			| xargs file \
			| grep "Python script" \
			| cut -d ':' -f 1
	} | sort -u
}

main () {
	local ruff bandit
	find_tool ruff ruff
	find_tool bandit bandit
	"${ruff[@]}" --version >&2
	"${bandit[@]}" --version 2>&1 | head -1 >&2
	echo "Checking files:" >&2
	list_python_files >&2
	list_python_files | xargs -L 1000 "${ruff[@]}" check "$@"
	list_python_files | xargs -L 1000 "${bandit[@]}" -q -ll
}

main "$@"
