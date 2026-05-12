#pragma once

#include <cstdint>
#include <string>

struct GamepadState {
  float lx = 0.0f;
  float ly = 0.0f;
  float rx = 0.0f;
  float ry = 0.0f;
  float lt = 0.0f;
  float rt = 0.0f;

  bool a = false;
  bool b = false;
  bool x = false;
  bool y = false;
  bool lb = false;
  bool rb = false;
  bool back = false;
  bool start = false;
  bool guide = false;
  bool ls = false;
  bool rs = false;
  bool dpad_up = false;
  bool dpad_down = false;
  bool dpad_left = false;
  bool dpad_right = false;
};

class VirtualGamepadDevice {
 public:
  // 暴露给 UI 用作设备诊断行展示，与 .cpp 内 uinput_user_dev 同步。
  static constexpr std::uint16_t kVendorId = 0x045e;
  static constexpr std::uint16_t kProductId = 0x028e;

  VirtualGamepadDevice();
  ~VirtualGamepadDevice();

  bool Create();
  void Destroy();
  bool SendState(const GamepadState& state);

  bool ready() const;
  const std::string& error() const;

  // 工程级诊断信息，供状态屏渲染使用。
  const std::string& device_path() const;
  int fd() const;
  int last_errno() const;

 private:
  int fd_;
  std::string error_;
  std::string path_;
  int last_errno_;

  bool SendKey(unsigned short code, bool down);
  bool SendAbs(unsigned short code, int value);
  bool Sync();
  void SetError(const std::string& message);
};
