#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
UDEV_RULE="/etc/udev/rules.d/99-uinput.rules"

need_install=0
for pkg in cmake g++ pkg-config libsdl2-dev libimgui-dev; do
  if ! dpkg -s "$pkg" >/dev/null 2>&1; then
    need_install=1
    break
  fi
done

if [[ "$need_install" -eq 1 ]]; then
  sudo apt-get update
  sudo apt-get install -y cmake g++ pkg-config libsdl2-dev libimgui-dev
fi

sudo modprobe uinput

if [[ ! -f "$UDEV_RULE" ]] || ! grep -q 'KERNEL=="uinput"' "$UDEV_RULE" 2>/dev/null; then
  echo 'KERNEL=="uinput", MODE="0660", GROUP="input"' | sudo tee "$UDEV_RULE" >/dev/null
  sudo udevadm control --reload-rules
  sudo udevadm trigger
fi

chmod +x "$ROOT_DIR/build.sh"
"$ROOT_DIR/build.sh"

exec "$ROOT_DIR/build/virtual_gamepad_linux"

