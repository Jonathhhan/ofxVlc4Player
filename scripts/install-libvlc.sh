#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PS1_SCRIPT="${SCRIPT_DIR}/install-libvlc.ps1"
ADDON_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

if command -v cygpath >/dev/null 2>&1; then
	PS1_SCRIPT="$(cygpath -w "${PS1_SCRIPT}")"
	ADDON_ROOT="$(cygpath -w "${ADDON_ROOT}")"
fi

if ! command -v powershell.exe >/dev/null 2>&1; then
	echo "powershell.exe not found in PATH." >&2
	exit 1
fi

exec powershell.exe -ExecutionPolicy Bypass -File "${PS1_SCRIPT}" -AddonRoot "${ADDON_ROOT}" "$@"
