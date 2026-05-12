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

VirtualGamepadDevice::VirtualGamepadDevice() : fd_(-1), last_errno_(0) {}

VirtualGamepadDevice::~VirtualGamepadDevice() {
  Destroy();
}

bool VirtualGamepadDevice::ready() const {
  return fd_ >= 0;
}

const std::string& VirtualGamepadDevice::error() const {
  return error_;
}

const std::string& VirtualGamepadDevice::device_path() const {
  return path_;
}

int VirtualGamepadDevice::fd() const {
  return fd_;
}

int VirtualGamepadDevice::last_errno() const {
  return last_errno_;
}

void VirtualGamepadDevice::SetError(const std::string& message) {
  error_ = message;
}

bool VirtualGamepadDevice::Create() {
  Destroy();
  error_.clear();
  path_.clear();
  last_errno_ = 0;

  // 依次尝试两个候选路径，命中即记录到 path_ 供 UI 展示。
  int fd = -1;
  int open_errno = 0;
  for (const char* path : kUinputPaths) {
    fd = ::open(path, O_WRONLY | O_NONBLOCK);
    if (fd >= 0) {
      path_ = path;
      break;
    }
    if (open_errno == 0) {
      open_errno = errno;  // 记下首次失败的 errno
    }
  }
  if (fd < 0) {
    errno = open_errno;
    last_errno_ = open_errno;
    SetError(ErrnoMessage("open /dev/uinput, /dev/input/uinput"));
    return false;
  }

  auto fail = [&](const std::string& message) {
    last_errno_ = errno;
    SetError(ErrnoMessage(message));
    ::close(fd);
    fd_ = -1;
    path_.clear();
    return false;
  };

  if (ioctl(fd, UI_SET_EVBIT, EV_KEY) < 0) return fail("UI_SET_EVBIT EV_KEY");
  if (ioctl(fd, UI_SET_EVBIT, EV_ABS) < 0) return fail("UI_SET_EVBIT EV_ABS");
  if (ioctl(fd, UI_SET_EVBIT, EV_SYN) < 0) return fail("UI_SET_EVBIT EV_SYN");

  const unsigned short keys[] = {
      BTN_SOUTH, BTN_EAST, BTN_NORTH, BTN_WEST,
      BTN_TL, BTN_TR, BTN_SELECT, BTN_START, BTN_MODE,
      BTN_THUMBL, BTN_THUMBR,
  };
  for (unsigned short key : keys) {
    if (ioctl(fd, UI_SET_KEYBIT, key) < 0) {
      return fail("UI_SET_KEYBIT");
    }
  }

  const unsigned short abs_axes[] = {
      ABS_X, ABS_Y, ABS_RX, ABS_RY, ABS_Z, ABS_RZ, ABS_HAT0X, ABS_HAT0Y,
  };
  for (unsigned short abs_code : abs_axes) {
    if (ioctl(fd, UI_SET_ABSBIT, abs_code) < 0) {
      return fail("UI_SET_ABSBIT");
    }
  }

  struct uinput_user_dev uidev;
  std::memset(&uidev, 0, sizeof(uidev));
  std::snprintf(uidev.name, UINPUT_MAX_NAME_SIZE, "%s", kDeviceName);
  uidev.id.bustype = BUS_USB;
  uidev.id.vendor = kVendorId;
  uidev.id.product = kProductId;
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
    return fail("write uinput_user_dev");
  }
  if (ioctl(fd, UI_DEV_CREATE) < 0) {
    return fail("UI_DEV_CREATE");
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
  path_.clear();
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
    SetError("device not ready");
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
    last_errno_ = errno;
    SetError(ErrnoMessage("write input_event"));
  }
  return ok;
}
