#pragma once

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
  VirtualGamepadDevice();
  ~VirtualGamepadDevice();

  bool Create();
  void Destroy();
  bool SendState(const GamepadState& state);

  bool ready() const;
  const std::string& error() const;

 private:
  int fd_;
  std::string error_;

  bool SendKey(unsigned short code, bool down);
  bool SendAbs(unsigned short code, int value);
  bool Sync();
  void SetError(const std::string& message);
};

