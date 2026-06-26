#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${TERRAIN_BUILD_DIR:-$ROOT_DIR/build}"
CONFIG="${TERRAIN_BUILD_TYPE:-Release}"
TARGET="${TERRAIN_TARGET:-terrain_app}"
EXTRA_CMAKE_ARGS="${TERRAIN_CMAKE_ARGS:-}"
CLEAN_BUILD=false

show_help() {
  cat <<'EOF'
Usage: ./scripts/rebuild-and-launch.sh [--clean] [--debug|--release] [--build-dir DIR] [--target NAME]
  --clean       Delete and recreate the build directory
  --debug       Configure for Debug
  --release     Configure for Release (default)
  --unity       Enable unity/jumbo builds (faster full rebuilds)
  --build-dir   Override build directory (defaults to ./build)
  --target      Override executable target name (defaults to terrain_app)
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --clean)
      CLEAN_BUILD=true
      shift
      ;;
    --debug)
      CONFIG="Debug"
      shift
      ;;
    --release)
      CONFIG="Release"
      shift
      ;;
    --build-dir)
      if [[ $# -lt 2 ]]; then
        echo "Missing value for --build-dir" >&2
        show_help
        exit 1
      fi
      BUILD_DIR="$2"
      shift 2
      ;;
    --unity)
      EXTRA_CMAKE_ARGS="${EXTRA_CMAKE_ARGS} -DUSE_UNITY_BUILD=ON"
      shift
      ;;
    --target)
      if [[ $# -lt 2 ]]; then
        echo "Missing value for --target" >&2
        show_help
        exit 1
      fi
      TARGET="$2"
      shift 2
      ;;
    -h|--help)
      show_help
      exit 0
      ;;
    *)
      echo "Unknown option: $1" >&2
      show_help
      exit 1
      ;;
  esac
done

if $CLEAN_BUILD; then
  echo "[1/3] Cleaning build directory: $BUILD_DIR"
  rm -rf "$BUILD_DIR"
fi

mkdir -p "$BUILD_DIR"

echo "[1/3] Configuring with CMake ($CONFIG)"
cmake -S "$ROOT_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE="$CONFIG" $EXTRA_CMAKE_ARGS

CPU_COUNT=1
if command -v nproc >/dev/null 2>&1; then
  CPU_COUNT=$(nproc)
elif command -v sysctl >/dev/null 2>&1; then
  CPU_COUNT=$(sysctl -n hw.ncpu 2>/dev/null || echo 1)
fi

echo "[2/3] Building target: $TARGET"
cmake --build "$BUILD_DIR" --config "$CONFIG" --parallel "$CPU_COUNT"

resolve_executable() {
  if [[ -x "$1/$TARGET" ]]; then
    echo "$1/$TARGET"
    return 0
  fi
  if [[ -x "$1/$TARGET.exe" ]]; then
    echo "$1/$TARGET.exe"
    return 0
  fi
  if [[ -x "$1/$CONFIG/$TARGET" ]]; then
    echo "$1/$CONFIG/$TARGET"
    return 0
  fi
  if [[ -x "$1/$CONFIG/${TARGET}.exe" ]]; then
    echo "$1/$CONFIG/${TARGET}.exe"
    return 0
  fi
  return 1
}

EXECUTABLE="$(resolve_executable "$BUILD_DIR" || true)"
if [[ -z "${EXECUTABLE:-}" ]]; then
  echo "Could not find executable '$TARGET' in '$BUILD_DIR'" >&2
  exit 1
fi

echo "[3/3] Launching: $EXECUTABLE"
exec "$EXECUTABLE"
