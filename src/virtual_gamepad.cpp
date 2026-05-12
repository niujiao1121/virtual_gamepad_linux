#include "virtual_gamepad.h"

#include <linux/uinput.h>
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <string>

namespace {

constexpr const char* kUinputPaths[] = {"/dev/uinput", "/dev/input/uinput"};
constexpr const char* kDeviceName = "Virtual Xbox Gamepad";

int AxisToShort(float value) {
  value = std::max(-1.0f, std::min(1.0f, value));
  return static_cast<int>(std::lround(value * 32767.0f));
}

int TriggerToByte(float value) {
  value = std::max(0.0f, std::min(1.0f, value));
  return static_cast<int>(std::lround(value * 255.0f));
}

std::string ErrnoMessage(const std::string& action) {
  return action + ": " + std::strerror(errno);
}

}  // namespace

VirtualGamepadDevice::VirtualGamepadDevice() : fd_(-1) {}

VirtualGamepadDevice::~VirtualGamepadDevice() {
  Destroy();
}

bool VirtualGamepadDevice::ready() const {
  return fd_ >= 0;
}

const std::string& VirtualGamepadDevice::error() const {
  return error_;
}

void VirtualGamepadDevice::SetError(const std::string& message) {
  error_ = message;
}

bool VirtualGamepadDevice::Create() {
  Destroy();
  error_.clear();

  int fd = -1;
  for (const char* path : kUinputPaths) {
    fd = ::open(path, O_WRONLY | O_NONBLOCK);
    if (fd >= 0) {
      break;
    }
  }
  if (fd < 0) {
    SetError(ErrnoMessage("无法打开 /dev/uinput 或 /dev/input/uinput"));
    return false;
  }

  auto fail = [&](const std::string& message) {
    SetError(ErrnoMessage(message));
    ::close(fd);
    fd_ = -1;
    return false;
  };

  if (ioctl(fd, UI_SET_EVBIT, EV_KEY) < 0) return fail("设置 EV_KEY 失败");
  if (ioctl(fd, UI_SET_EVBIT, EV_ABS) < 0) return fail("设置 EV_ABS 失败");
  if (ioctl(fd, UI_SET_EVBIT, EV_SYN) < 0) return fail("设置 EV_SYN 失败");

  const unsigned short keys[] = {
      BTN_SOUTH, BTN_EAST, BTN_NORTH, BTN_WEST,
      BTN_TL, BTN_TR, BTN_SELECT, BTN_START, BTN_MODE,
      BTN_THUMBL, BTN_THUMBR,
  };
  for (unsigned short key : keys) {
    if (ioctl(fd, UI_SET_KEYBIT, key) < 0) {
      return fail("设置按键能力失败");
    }
  }

  const unsigned short abs_axes[] = {
      ABS_X, ABS_Y, ABS_RX, ABS_RY, ABS_Z, ABS_RZ, ABS_HAT0X, ABS_HAT0Y,
  };
  for (unsigned short abs_code : abs_axes) {
    if (ioctl(fd, UI_SET_ABSBIT, abs_code) < 0) {
      return fail("设置轴能力失败");
    }
  }

  struct uinput_user_dev uidev;
  std::memset(&uidev, 0, sizeof(uidev));
  std::snprintf(uidev.name, UINPUT_MAX_NAME_SIZE, "%s", kDeviceName);
  uidev.id.bustype = BUS_USB;
  uidev.id.vendor = 0x045e;
  uidev.id.product = 0x028e;
  uidev.id.version = 1;

  auto set_abs = [&](unsigned short code, int min, int max, int flat = 0) {
    uidev.absmin[code] = min;
    uidev.absmax[code] = max;
    uidev.absflat[code] = flat;
  };

  set_abs(ABS_X, -32768, 32767, 4096);
  set_abs(ABS_Y, -32768, 32767, 4096);
  set_abs(ABS_RX, -32768, 32767, 4096);
  set_abs(ABS_RY, -32768, 32767, 4096);
  set_abs(ABS_Z, 0, 255, 0);
  set_abs(ABS_RZ, 0, 255, 0);
  set_abs(ABS_HAT0X, -1, 1, 0);
  set_abs(ABS_HAT0Y, -1, 1, 0);

  if (write(fd, &uidev, sizeof(uidev)) < 0) {
    return fail("写入 uinput_user_dev 失败");
  }
  if (ioctl(fd, UI_DEV_CREATE) < 0) {
    return fail("创建虚拟手柄失败");
  }

  fd_ = fd;
  return true;
}

void VirtualGamepadDevice::Destroy() {
  if (fd_ >= 0) {
    ioctl(fd_, UI_DEV_DESTROY);
    ::close(fd_);
    fd_ = -1;
  }
}

bool VirtualGamepadDevice::SendKey(unsigned short code, bool down) {
  struct input_event ev;
  std::memset(&ev, 0, sizeof(ev));
  ev.type = EV_KEY;
  ev.code = code;
  ev.value = down ? 1 : 0;
  return write(fd_, &ev, sizeof(ev)) == static_cast<ssize_t>(sizeof(ev));
}

bool VirtualGamepadDevice::SendAbs(unsigned short code, int value) {
  struct input_event ev;
  std::memset(&ev, 0, sizeof(ev));
  ev.type = EV_ABS;
  ev.code = code;
  ev.value = value;
  return write(fd_, &ev, sizeof(ev)) == static_cast<ssize_t>(sizeof(ev));
}

bool VirtualGamepadDevice::Sync() {
  struct input_event ev;
  std::memset(&ev, 0, sizeof(ev));
  ev.type = EV_SYN;
  ev.code = SYN_REPORT;
  ev.value = 0;
  return write(fd_, &ev, sizeof(ev)) == static_cast<ssize_t>(sizeof(ev));
}

bool VirtualGamepadDevice::SendState(const GamepadState& state) {
  if (!ready()) {
    SetError("虚拟手柄尚未创建");
    return false;
  }

  const bool ok =
      SendAbs(ABS_X, AxisToShort(state.lx)) &&
      SendAbs(ABS_Y, -AxisToShort(state.ly)) &&
      SendAbs(ABS_RX, AxisToShort(state.rx)) &&
      SendAbs(ABS_RY, -AxisToShort(state.ry)) &&
      SendAbs(ABS_Z, TriggerToByte(state.lt)) &&
      SendAbs(ABS_RZ, TriggerToByte(state.rt)) &&
      SendAbs(ABS_HAT0X, state.dpad_left ? -1 : (state.dpad_right ? 1 : 0)) &&
      SendAbs(ABS_HAT0Y, state.dpad_up ? -1 : (state.dpad_down ? 1 : 0)) &&
      SendKey(BTN_SOUTH, state.a) &&
      SendKey(BTN_EAST, state.b) &&
      SendKey(BTN_WEST, state.x) &&
      SendKey(BTN_NORTH, state.y) &&
      SendKey(BTN_TL, state.lb) &&
      SendKey(BTN_TR, state.rb) &&
      SendKey(BTN_SELECT, state.back) &&
      SendKey(BTN_START, state.start) &&
      SendKey(BTN_MODE, state.guide) &&
      SendKey(BTN_THUMBL, state.ls) &&
      SendKey(BTN_THUMBR, state.rs) &&
      Sync();

  if (!ok) {
    SetError("发送虚拟手柄事件失败");
  }
  return ok;
}
