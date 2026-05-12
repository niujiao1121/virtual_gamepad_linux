# virtual_gamepad_linux

Linux 桌面上的虚拟手柄控制面板。

当前版本：`0.3.0`

## 功能

- 使用 SDL2 + Dear ImGui 显示 UI
- 通过 `uinput` 创建虚拟手柄
- 支持标准 Xbox 风格输入：
  - A/B/X/Y
  - LB/RB
  - Back/Start/Guide
  - LS/RS
  - 十字键
  - 双摇杆
  - 双扳机
- 支持摇杆锁定
- 支持全局 `Hold Mode`

## 依赖

- `libsdl2-dev`
- `libimgui-dev`
- `cmake`
- `g++`
- `/dev/uinput` 可访问

## 构建

```bash
./build.sh
```

`build.sh` 只检查构建依赖并编译程序，不会修改系统配置。

## 一键启动

```bash
./run.sh
```

首次运行会自动：

- 检查并安装依赖
- 加载 `uinput`
- 写入 `udev` 规则和当前用户访问权限
- 编译程序
- 启动应用

脚本会调用 `sudo`，首次执行时可能需要输入密码。
如果脚本提示已将当前用户加入 `input` 组，重新登录后权限会长期生效；本次运行会临时给当前用户写入 `/dev/uinput` 访问权限。

## 运行

```bash
./build/virtual_gamepad_linux
```

## UI 操作

- 使用系统窗口栏移动和关闭窗口；底部工具栏的 `-` / `+` 按固定比例缩放窗口。
- 状态屏顶部显示 `STATUS`、设备路径、`fd` 和 `VID:PID` 诊断信息。
- 如果虚拟手柄创建或写入失败，状态屏会显示红色 `ERROR`、`errno` 符号和完整原始错误文本。

## 权限

如果启动时提示无法打开 `/dev/uinput`，需要给当前用户访问权限，或临时使用 `sudo` 运行。

## 验证

可用 `jstest` 或 `evtest` 检查系统是否识别到虚拟手柄。
