#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

BLUEYOS_SYSROOT="${BLUEYOS_SYSROOT:-/opt/blueyos-sysroot}"
BUILD_DIR="${BUILD_DIR:-${REPO_DIR}/build}"

abspath() {
  case "$1" in
    /*) printf '%s\n' "$1" ;;
    *) printf '%s/%s\n' "${REPO_DIR}" "$1" ;;
  esac
}

is_musl_prefix() {
  local prefix="$1"
  [ -d "${prefix}/include" ] && [ -f "${prefix}/lib/libc.a" ]
}

emit_candidates() {
  local candidate="${1%/}"
  [ -n "${candidate}" ] || return 0

  printf '%s\n' "${candidate}"
  case "${candidate}" in
    */usr) printf '%s\n' "${candidate%/usr}" ;;
    *) printf '%s/usr\n' "${candidate}" ;;
  esac
}

resolve_prefix() {
  local requested="$1"

  if [ -n "${requested}" ]; then
    requested="$(abspath "${requested}")"
    while IFS= read -r candidate; do
      if is_musl_prefix "${candidate}"; then
        printf '%s\n' "${candidate}"
        return 0
      fi
    done < <(emit_candidates "${requested}")

    printf '%s\n' "${requested}"
    return 0
  fi

  while IFS= read -r candidate; do
    if is_musl_prefix "${candidate}"; then
      printf '%s\n' "${candidate}"
      return 0
    fi
  done < <(
    emit_candidates "$(abspath "${BLUEYOS_SYSROOT}")"
    printf '%s\n' "$(abspath "${BUILD_DIR}/musl")"
  )

  printf '%s\n' "$(abspath "${BUILD_DIR}/musl")"
}

resolve_prefix "${1:-}"
