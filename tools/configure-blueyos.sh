#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

LIBC="${LIBC:-musl}"
SYSROOT="${SYSROOT:-/opt/blueyos-sysroot}"
BUILD_DIR="${BUILD_DIR:-${REPO_DIR}/build/${LIBC}}"
HOST="${HOST:-}"
CC_INPUT="${CC:-}"
PACKAGE_BUILD_NUMBER="${PACKAGE_BUILD_NUMBER:-1}"

while [[ $# -gt 0 ]]; do
  case "$1" in
    --libc=*) LIBC="${1#*=}"; shift ;;
    --sysroot=*) SYSROOT="${1#*=}"; shift ;;
    --build-dir=*) BUILD_DIR="${1#*=}"; shift ;;
    --host=*) HOST="${1#*=}"; shift ;;
    --cc=*) CC_INPUT="${1#*=}"; shift ;;
    --help|-h)
      echo "Usage: tools/configure-blueyos.sh [--libc=musl|glibc] [--sysroot=PATH] [--build-dir=PATH] [--host=TRIPLET] [--cc=COMPILER] [extra configure args...]"
      exit 0
      ;;
    *)
      break
      ;;
  esac
done

if [ ! -x "${REPO_DIR}/configure" ]; then
  "${REPO_DIR}/autogen.sh"
fi

mkdir -p "${BUILD_DIR}"

if [ -z "${HOST}" ]; then
  if [ -n "${CC_INPUT}" ]; then
    HOST="$("${CC_INPUT}" -dumpmachine 2>/dev/null || true)"
  fi
fi
if [ -z "${HOST}" ]; then
  HOST="i686-linux-gnu"
fi

if [ -z "${CC_INPUT}" ]; then
  if [ "${LIBC}" = "musl" ]; then
    CC_INPUT="$(BLUEYOS_SYSROOT="${SYSROOT}" BUILD_DIR="${BUILD_DIR}" bash "${SCRIPT_DIR}/resolve-musl-cc.sh")"
  elif command -v "${HOST}-gcc" >/dev/null 2>&1; then
    CC_INPUT="$(command -v "${HOST}-gcc")"
  else
    echo "[configure-blueyos] error: no cross-compiler for host '${HOST}' found; install it or pass CC= or --cc= explicitly" >&2
    exit 1
  fi
fi

cd "${BUILD_DIR}"

host_args=()
if [ -n "${HOST}" ]; then
  if "${REPO_DIR}/config.sub" "${HOST}" >/dev/null 2>&1; then
    host_args+=(--host="${HOST}")
  else
    echo "[configure-blueyos] warning: config.sub does not recognize '${HOST}', continuing without --host" >&2
  fi
fi

# The BlueyOS ELF loader only supports ET_EXEC (static non-PIE).
# Force static, non-PIE output for all musl (BlueyOS) builds.
if [ "${LIBC}" = "musl" ]; then
    CFLAGS="${CFLAGS-} -fno-pic -fno-pie"
    LDFLAGS="${LDFLAGS-} -static -no-pie"
fi

# Build env args, only passing CFLAGS/LDFLAGS when set.
env_args=(
  PACKAGE_BUILD_NUMBER="${PACKAGE_BUILD_NUMBER}"
  CC="${CC_INPUT}"
)
if [ -n "${CFLAGS+set}" ]; then
  env_args+=(CFLAGS="${CFLAGS-}")
fi
if [ -n "${LDFLAGS+set}" ]; then
  env_args+=(LDFLAGS="${LDFLAGS-}")
fi

exec env \
  "${env_args[@]}" \
  "${REPO_DIR}/configure" \
    --prefix=/usr \
    --sysconfdir=/etc \
    --localstatedir=/var \
    --with-libc="${LIBC}" \
    --with-blueyos-sysroot="${SYSROOT}" \
    --enable-blueyos-netctl \
    "${host_args[@]}" \
    "$@"
