#!/bin/sh
# Build the File Manager extension for Linux (32-bit, for srcds_linux).
#
# Requirements:
#   - g++ with 32-bit support (gcc-multilib / g++-multilib on Debian/Ubuntu)
#   - Python 3.6+ with AMBuild installed:  pip3 install <path-to-ambuild>
#   - A SourceMod source tree (default: ../sourcemod)
#
# Usage:
#   ./build_linux.sh [--sm-path=...] [--enable-debug]
#
# The binary is linked with -static-libstdc++ and -static-libgcc and avoids
# glibc symbols newer than GLIBC_2.3.4, so it runs on old server distributions.

set -e

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
SM_PATH="${SM_PATH:-$ROOT/../sourcemod}"

# Prefer GCC. Both GCC and Clang work, but GCC produces a predictable static
# libstdc++ that does not add extra runtime dependencies.
export CC="${CC:-gcc}"
export CXX="${CXX:-g++}"

if ! printf 'int main(void){return 0;}' | g++ -m32 -x c++ - -o /tmp/.fm32test.$$ >/dev/null 2>&1; then
  echo "error: g++ cannot build 32-bit binaries. Install gcc-multilib and g++-multilib." >&2
  exit 1
fi
rm -f /tmp/.fm32test.$$

mkdir -p "$ROOT/build"
cd "$ROOT/build"

python3 "$ROOT/configure.py" --sm-path="$SM_PATH" --enable-optimize "$@"
python3 -c 'from ambuild2.run import cli_run; cli_run()'

echo
echo "Build succeeded. Output is in $ROOT/build/package"
