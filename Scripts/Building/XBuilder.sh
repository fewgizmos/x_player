#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT_DIR"

run_after=0

usage() {
    echo "Usage: $0 [options]"
    echo
    echo "Builds the X-Player project for Linux."
    echo
    echo "Options:"
    echo "  -r, --run   run the executable after a successful build"
    echo "  -h, --help  show this help and exit"
    echo
    echo "Short options can be combined, e.g. -rh"
}

bad_arg() {
    echo "Unknown argument: $1" >&2
    echo "Run '$0 --help' for usage." >&2
    exit 1
}

for arg in "$@"; do
    case "$arg" in
        --run) run_after=1 ;;
        --help) usage; exit 0 ;;
        --*) bad_arg "$arg" ;;
        -?*)
            for ((i = 1; i < ${#arg}; i++)); do
                case "${arg:i:1}" in
                    r) run_after=1 ;;
                    h) usage; exit 0 ;;
                    *) bad_arg "-${arg:i:1}" ;;
                esac
            done
            ;;
        *) bad_arg "$arg" ;;
    esac
done

for env_var in X_BUILD_ID X_BUILD_TYPE X_TARGET_BIN_NAME; do
    if [[ -z "${!env_var:-}" ]]; then
        echo "$env_var is not set in the environment" >&2
        exit 1
    fi
done

case "$X_BUILD_TYPE" in
    RELEASE) cmake_build_type="Release" ;;
    DEBUG)   cmake_build_type="Debug" ;;
    *)
        echo "X_BUILD_TYPE must be RELEASE or DEBUG, got: '$X_BUILD_TYPE'" >&2
        exit 1
        ;;
esac

cmake_build_dir=".CmakeBuild/$cmake_build_type"

cmake -B "$cmake_build_dir" -S .
cmake --build "$cmake_build_dir"

out_dir="Build/$X_BUILD_ID/Linux/$cmake_build_type"
mkdir -p "$out_dir"

cp "$cmake_build_dir/bin/XProjectExecutable" "$out_dir/$X_TARGET_BIN_NAME"

ln -sfn "$(realpath --relative-to="$out_dir" "$ROOT_DIR/Project/Resources")" "$out_dir/Resources"

echo "Compiled Executable: '$out_dir/$X_TARGET_BIN_NAME'"

if [[ "$run_after" -eq 1 ]]; then
    (cd "$out_dir" && "./$X_TARGET_BIN_NAME")
fi
