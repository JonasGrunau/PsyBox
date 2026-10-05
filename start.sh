#!/usr/bin/env bash
# Build (incrementally) and launch the PsyBox standalone app.
# Usage: ./start.sh            build if needed, then launch
#        ./start.sh --no-build launch the last build as-is
set -euo pipefail

cd "$(dirname "$0")"

BUILD_DIR=build
APP="$BUILD_DIR/PsyBox_artefacts/Release/Standalone/PsyBox.app"

if [[ "${1:-}" != "--no-build" ]]; then
    if [[ ! -f "$BUILD_DIR/build.ninja" ]]; then
        cmake -B "$BUILD_DIR" -G Ninja -DCMAKE_BUILD_TYPE=Release
    fi
    cmake --build "$BUILD_DIR" --target PsyBox_Standalone
fi

if [[ ! -d "$APP" ]]; then
    echo "Standalone app not found at $APP. Run ./start.sh without --no-build." >&2
    exit 1
fi

open "$APP"
