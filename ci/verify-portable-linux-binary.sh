#!/usr/bin/env bash
set -euo pipefail

build_folder="${1:?usage: $0 build-folder binary}"
binary="${2:?usage: $0 build-folder binary}"
compile_commands="$build_folder/compile_commands.json"
cmake_cache="$build_folder/CMakeCache.txt"
oqs_config="$build_folder/cryptonote/external/liboqs/include/oqs/oqsconfig.h"

for required_file in "$binary" "$compile_commands" "$cmake_cache" "$oqs_config"; do
  if [[ ! -f "$required_file" ]]; then
    echo "Portable-build verification input is missing: $required_file" >&2
    exit 1
  fi
done

if ! grep -Eq '^OQS_DIST_BUILD:BOOL=ON$' "$cmake_cache"; then
  echo "Portable-build verification failed: OQS_DIST_BUILD is not ON" >&2
  exit 1
fi

if ! grep -Eq '^OQS_OPT_TARGET:STRING=generic$' "$cmake_cache"; then
  echo "Portable-build verification failed: OQS_OPT_TARGET is not generic" >&2
  exit 1
fi

if ! grep -Eq '^#define OQS_DIST_BUILD 1$' "$oqs_config"; then
  echo "Portable-build verification failed: liboqs runtime dispatch is not compiled in" >&2
  exit 1
fi

if grep -En -- '-(march|mtune|mcpu)=native|-mavx512' "$compile_commands"; then
  echo "Portable-build verification failed: host-specific or AVX-512 compiler flags detected" >&2
  exit 1
fi

disassembly_file="$(mktemp)"
trap 'rm -f "$disassembly_file"' EXIT
objdump -d -M intel "$binary" > "$disassembly_file"

if grep -Eq '(^|[^[:alnum:]_])zmm[0-9]+([^[:alnum:]_]|$)' "$disassembly_file"; then
  echo "Portable-build verification failed: AVX-512 ZMM instructions detected" >&2
  exit 1
fi

echo "Portable-build verification passed: generic liboqs dispatch, no native flags, no AVX-512 ZMM instructions"
