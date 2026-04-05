#!/usr/bin/env bash
# ─────────────────────────────────────────────
#  Smart Clock — build / flash / monitor helper
#  Usage: ./build.sh [build|flash|monitor|clean|web]
# ─────────────────────────────────────────────
set -e

CMD="${1:-build}"
ENV="esp32-c3-supermini"

case "$CMD" in
  build)
    echo "🔨 Building firmware..."
    pio run -e "$ENV"
    echo ""
    echo "✅ Build done."
    echo "   Firmware: .pio/build/${ENV}/firmware.bin"
    ;;

  flash)
    echo "⚡ Flashing to ESP32-C3..."
    pio run -e "$ENV" -t upload
    ;;

  monitor)
    echo "📡 Opening serial monitor (Ctrl+C to exit)..."
    pio device monitor -e "$ENV"
    ;;

  clean)
    echo "🧹 Cleaning build..."
    pio run -t clean
    ;;

  web)
    # Copy firmware binaries into web/ for the flasher
    echo "📦 Copying firmware files to web/..."
    BUILD=".pio/build/${ENV}"

    if [ ! -f "$BUILD/firmware.bin" ]; then
      echo "❌ Build first: ./build.sh build"
      exit 1
    fi

    cp "$BUILD/firmware.bin"    web/firmware.bin
    cp "$BUILD/bootloader.bin"  web/bootloader.bin
    cp "$BUILD/partitions.bin"  web/partitions.bin

    # boot_app0.bin comes from the framework
    FRAMEWORK_PATH=$(pio run -e "$ENV" --list-targets 2>/dev/null | grep -o 'packages.*' | head -1 || true)
    BOOT_APP=$(find ~/.platformio -name "boot_app0.bin" 2>/dev/null | head -1)
    if [ -n "$BOOT_APP" ]; then
      cp "$BOOT_APP" web/boot_app0.bin
    fi

    echo "✅ Web files ready in web/"
    echo "   Serve with: cd web && python3 -m http.server 8080"
    ;;

  all)
    bash "$0" build
    bash "$0" web
    bash "$0" flash
    ;;

  *)
    echo "Usage: $0 [build|flash|monitor|clean|web|all]"
    exit 1
    ;;
esac
