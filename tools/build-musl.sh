#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

PREFIX="${PREFIX:-${REPO_DIR}/build/musl}"
TARGET="${TARGET:-i386-linux-musl}"
MUSL_REPO="${MUSL_REPO:-nzmacgeek/musl-blueyos}"
MUSL_REF="${MUSL_REF:-main}"

while [[ $# -gt 0 ]]; do
  case "$1" in
    --prefix=*) PREFIX="${1#*=}"; shift ;;
    --target=*) TARGET="${1#*=}"; shift ;;
    --repo=*) MUSL_REPO="${1#*=}"; shift ;;
    --ref=*) MUSL_REF="${1#*=}"; shift ;;
    --help|-h)
      echo "Usage: tools/build-musl.sh [--prefix=PATH] [--target=TRIPLET] [--repo=OWNER/REPO] [--ref=REF]"
      exit 0
      ;;
    *)
      echo "Unknown argument: $1" >&2
      exit 1
      ;;
  esac
done

MUSL_CLONE_URL="https://github.com/${MUSL_REPO}.git"
BUILD_TMP="$(mktemp -d -t scout-musl-build.XXXXXX)"
trap 'rm -rf "${BUILD_TMP}"' EXIT

echo "Building musl-blueyos for ${TARGET}"
echo "  source : ${MUSL_CLONE_URL}"
echo "  prefix : ${PREFIX}"
echo "  workdir: ${BUILD_TMP}"

git clone --depth=1 --branch "${MUSL_REF}" "${MUSL_CLONE_URL}" "${BUILD_TMP}/musl-blueyos"
cd "${BUILD_TMP}/musl-blueyos"

CC="${CC:-gcc}"
./configure --prefix="${PREFIX}" --target="${TARGET}" CC="${CC}" CFLAGS="-m32 -O2" LDFLAGS="-m32"
make -j"$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 1)"
make install

echo "musl-blueyos installed to ${PREFIX}"
