#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

MUSL_PREFIX_INPUT="${MUSL_PREFIX:-}"
BUILD_DIR="${BUILD_DIR:-${REPO_DIR}/build}"

abspath() {
  case "$1" in
    /*) printf '%s\n' "$1" ;;
    *) printf '%s/%s\n' "${REPO_DIR}" "$1" ;;
  esac
}

BUILD_DIR="$(abspath "${BUILD_DIR}")"
MUSL_PREFIX="$(BLUEYOS_SYSROOT="${BLUEYOS_SYSROOT:-/opt/blueyos-sysroot}" BUILD_DIR="${BUILD_DIR}" bash "${SCRIPT_DIR}/resolve-musl-prefix.sh" "${MUSL_PREFIX_INPUT}")"

MUSL_SPECS="${MUSL_PREFIX}/lib/musl-gcc.specs"
MUSL_GCC="${MUSL_PREFIX}/bin/musl-gcc"

if [ -f "${MUSL_SPECS}" ]; then
  TOOLCHAIN_DIR="${BUILD_DIR}/toolchain"
  WRAPPER="${TOOLCHAIN_DIR}/musl-gcc"
  REWRITTEN_SPECS="${TOOLCHAIN_DIR}/musl-gcc.specs"
  if command -v i686-linux-gnu-gcc >/dev/null 2>&1; then
    REALGCC_DEFAULT="$(command -v i686-linux-gnu-gcc)"
  else
    REALGCC_DEFAULT="gcc"
  fi

  mkdir -p "${TOOLCHAIN_DIR}"

  old_root="$(grep -m1 -oE '/[^ ]+/include' "${MUSL_SPECS}" | sed 's#/include$##' || true)"
  if [ -n "${old_root}" ] && [ "${old_root}" != "${MUSL_PREFIX}" ]; then
    sed "s#${old_root}#${MUSL_PREFIX}#g" "${MUSL_SPECS}" > "${REWRITTEN_SPECS}"
  else
    cp "${MUSL_SPECS}" "${REWRITTEN_SPECS}"
  fi

  cat > "${WRAPPER}" <<EOF
#!/bin/sh
exec "\${REALGCC:-${REALGCC_DEFAULT}}" "\$@" -specs "${REWRITTEN_SPECS}"
EOF
  chmod +x "${WRAPPER}"
  printf '%s\n' "${WRAPPER}"
  exit 0
fi

if [ -x "${MUSL_GCC}" ]; then
  printf '%s\n' "${MUSL_GCC}"
  exit 0
fi

printf '%s\n' "gcc"
