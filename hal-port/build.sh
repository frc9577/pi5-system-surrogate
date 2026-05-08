#!/bin/bash
# Wraps the upstream allwpilib gradlew with the init script that overrides
# the opensdk toolchain tag — without it, v2025-1's missing aarch64-host
# bundle causes a 404 during installSystemCoreToolchain.
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$script_dir/upstream/allwpilib"
exec ./gradlew --init-script "$script_dir/gradle/toolchain-override.gradle" "$@"
