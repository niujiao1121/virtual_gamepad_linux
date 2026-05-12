#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
UDEV_RULE="/etc/udev/rules.d/99-uinput.rules"
export XDG_CACHE_HOME="${XDG_CACHE_HOME:-/tmp}"

APT_PACKAGES=(
  cmake
  g++
  pkg-config
  libsdl2-dev
  libimgui-dev
  kmod
  udev
  acl
)

need_install=0
for pkg in "${APT_PACKAGES[@]}"; do
  if ! dpkg -s "$pkg" >/dev/null 2>&1; then
    need_install=1
    break
  fi
done

if [[ "$need_install" -eq 1 ]]; then
  sudo apt-get update
  sudo apt-get install -y "${APT_PACKAGES[@]}"
fi

sudo modprobe uinput

if [[ ! -f "$UDEV_RULE" ]] || ! grep -q 'TAG+="uaccess"' "$UDEV_RULE" 2>/dev/null; then
  echo 'KERNEL=="uinput", MODE="0660", GROUP="input", TAG+="uaccess"' | sudo tee "$UDEV_RULE" >/dev/null
  sudo udevadm control --reload-rules
  sudo udevadm trigger
fi

if getent group input >/dev/null 2>&1 && ! id -nG "$USER" | grep -qw input; then
  sudo usermod -aG input "$USER"
  echo "已将 $USER 加入 input 组，重新登录后长期生效。"
fi

for dev in /dev/uinput /dev/input/uinput; do
  if [[ -e "$dev" ]]; then
    sudo setfacl -m "u:${USER}:rw" "$dev" 2>/dev/null || true
  fi
done

chmod +x "$ROOT_DIR/build.sh"
"$ROOT_DIR/build.sh"

exec "$ROOT_DIR/build/virtual_gamepad_linux"
