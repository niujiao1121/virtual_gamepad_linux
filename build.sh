#!/usr/bin/env bash
set -euo pipefail

missing=()
for pkg in cmake g++ pkg-config libsdl2-dev libimgui-dev; do
  if ! dpkg -s "$pkg" >/dev/null 2>&1; then
    missing+=("$pkg")
  fi
done

if [[ "${#missing[@]}" -gt 0 ]]; then
  echo "缺少构建依赖: ${missing[*]}"
  echo "请执行 ./run.sh 自动安装，或手动安装：sudo apt-get install -y ${missing[*]}"
  exit 1
fi

cmake -S . -B build
cmake --build build -j"$(nproc)"
