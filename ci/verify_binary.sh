#!/bin/bash
# Copyright 2026 The Abseil Authors.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      https://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# Checks ELF header architecture, dynamic needed libraries, or binary symbol tables.
set -euo pipefail

BINARY="$1"
MODE="$2"
PATTERN="$3"

echo "Inspecting BINARY: $BINARY" >&2
if [[ ! -f "$BINARY" ]]; then
  echo "ERROR: binary $BINARY not found" >&2
  exit 1
fi

case "$MODE" in
  arch)
    echo "Verifying ELF architecture header matches '$PATTERN'..."
    readelf -h "$BINARY" | grep "$PATTERN" >/dev/null || {
      echo "ERROR: ELF header for $BINARY does not match expected architecture '$PATTERN'" >&2
      readelf -h "$BINARY" >&2
      exit 1
    }
    ;;
  comment)
    echo "Verifying ELF .comment section matches '$PATTERN'..."
    readelf -p .comment "$BINARY" | grep "$PATTERN" >/dev/null || {
      echo "ERROR: ELF .comment section for $BINARY does not match '$PATTERN'" >&2
      readelf -p .comment "$BINARY" >&2
      exit 1
    }
    ;;
  symbol)
    echo "Verifying binary symbol table contains '$PATTERN'..."
    nm "$BINARY" | grep "$PATTERN" >/dev/null || {
      echo "ERROR: Symbol '$PATTERN' not found in $BINARY" >&2
      exit 1
    }
    ;;
  needed)
    echo "Verifying ELF NEEDED dynamic dependencies contain '$PATTERN'..."
    readelf -d "$BINARY" | grep "$PATTERN" >/dev/null || {
      echo "ERROR: Dynamic dependency '$PATTERN' not found in $BINARY" >&2
      readelf -d "$BINARY" >&2
      exit 1
    }
    ;;
  not_needed)
    echo "Verifying ELF NEEDED dynamic dependencies do NOT contain '$PATTERN'..."
    if readelf -d "$BINARY" | grep "$PATTERN" >/dev/null; then
      echo "ERROR: Unexpected dynamic dependency '$PATTERN' found in $BINARY" >&2
      readelf -d "$BINARY" >&2
      exit 1
    fi
    ;;
  *)
    echo "ERROR: Unknown verification mode '$MODE'" >&2
    exit 1
    ;;
esac

echo "SUCCESS: Verified $MODE '$PATTERN' in $BINARY."
