#include "virtual_gamepad.h"

#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_sdl2.h"

#include <SDL.h>
#include <SDL_opengl.h>

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {

// ---- 状态记忆 ------------------------------------------------------------

struct ButtonMemory { bool latched = false; };

struct StickMemory {
  bool locked = false;
  float lock_x = 0.0f, lock_y = 0.0f;
  float last_x = 0.0f, last_y = 0.0f;
};

struct UiMemory {
  bool hold_mode = false;
  ButtonMemory a, b, x, y, lb, rb, back, start, guide, ls, rs;
  ButtonMemory dpad_up, dpad_down, dpad_left, dpad_right;
  StickMemory left_stick, right_stick;
};

bool DigitalPressed(bool momentary, const ButtonMemory& m) {
  return momentary || m.latched;
}

// errno 数字 → 符号名，给状态屏的工程信息行用。
const char* ErrnoSymbol(int e) {
  switch (e) {
    case 0:       return "OK";
    case EPERM:   return "EPERM";
    case ENOENT:  return "ENOENT";
    case EINTR:   return "EINTR";
    case EIO:     return "EIO";
    case ENXIO:   return "ENXIO";
    case EBADF:   return "EBADF";
    case EAGAIN:  return "EAGAIN";
    case EACCES:  return "EACCES";
    case EBUSY:   return "EBUSY";
    case ENODEV:  return "ENODEV";
    case ENOTTY:  return "ENOTTY";
    case EPIPE:   return "EPIPE";
    case EINVAL:  return "EINVAL";
    case ENOSYS:  return "ENOSYS";
    default:      return "ERR";
  }
}

// ---- 配色（现代扁平 teal 强调）-----------------------------------------

constexpr ImU32 kPanel       = IM_COL32(0x16,0x1B,0x22,0xFF);
constexpr ImU32 kPanel2      = IM_COL32(0x1E,0x24,0x2E,0xFF);
constexpr ImU32 kPanelSink   = IM_COL32(0x10,0x14,0x1A,0xFF);
constexpr ImU32 kHairline    = IM_COL32(0x2A,0x31,0x40,0xFF);
constexpr ImU32 kHairlineDim = IM_COL32(0x22,0x29,0x35,0xFF);

constexpr ImU32 kTextPrimary = IM_COL32(0xE6,0xEA,0xF0,0xFF);
constexpr ImU32 kTextMute    = IM_COL32(0x9A,0xA3,0xB2,0xFF);
constexpr ImU32 kTextDim     = IM_COL32(0x6B,0x73,0x82,0xFF);
constexpr ImU32 kTextMicro   = IM_COL32(0x55,0x5C,0x6B,0xFF);
constexpr ImU32 kTextMono    = IM_COL32(0x6F,0xE3,0xC2,0xFF);

constexpr ImU32 kAccent      = IM_COL32(0x6F,0xE3,0xC2,0xFF);
constexpr ImU32 kAccentBd    = IM_COL32(0x8F,0xF0,0xD4,0xFF);
constexpr ImU32 kAccentSoft  = IM_COL32(0x6F,0xE3,0xC2,0x22);
constexpr ImU32 kAccentGlow  = IM_COL32(0x6F,0xE3,0xC2,0x55);

constexpr ImU32 kWarn        = IM_COL32(0xF2,0xB4,0x5A,0xFF);
constexpr ImU32 kError       = IM_COL32(0xFF,0x6E,0x6E,0xFF);
constexpr ImU32 kErrorSoft   = IM_COL32(0xFF,0x6E,0x6E,0x1A);

constexpr ImU32 kFaceY = IM_COL32(0xE8,0xC2,0x6B,0xFF);
constexpr ImU32 kFaceA = IM_COL32(0x5B,0xC0,0x89,0xFF);
constexpr ImU32 kFaceB = IM_COL32(0xE6,0x6B,0x6B,0xFF);
constexpr ImU32 kFaceX = IM_COL32(0x5C,0x8F,0xE8,0xFF);

// ---- 布局 ----------------------------------------------------------------

constexpr float kBaseW = 880.0f, kBaseH = 580.0f;
constexpr float kDefaultScale = 1.0f, kMinScale = 0.75f, kMaxScale = 1.30f, kScaleStep = 0.05f;
float g_ui_scale = kDefaultScale;

float S(float v) { return v * g_ui_scale; }

// ---- 几何 helper ---------------------------------------------------------

ImVec2 StickFromMouse(const ImVec2& c, const ImVec2& m, float r, float* x, float* y) {
  float dx = (m.x - c.x) / r, dy = (c.y - m.y) / r, len = std::sqrt(dx*dx + dy*dy);
  if (len > 1.0f) { dx /= len; dy /= len; }
  *x = dx; *y = dy;
  return ImVec2(c.x + dx * r, c.y - dy * r);
}

void ResizeWindowToScale(SDL_Window* window) {
  const int ww = static_cast<int>(S(kBaseW));
  const int wh = static_cast<int>(S(kBaseH));
  SDL_SetWindowSize(window, ww, wh);
}

// ---- 基础绘制 ------------------------------------------------------------

void DrawArrow(ImDrawList* d, const ImVec2& c, float s, int dir, ImU32 col) {
  float hw = s * 0.55f, hh = s * 0.7f; ImVec2 p1, p2, p3;
  if      (dir == 0) { p1 = ImVec2(c.x, c.y - s); p2 = ImVec2(c.x - hw, c.y + hh); p3 = ImVec2(c.x + hw, c.y + hh); }
  else if (dir == 1) { p1 = ImVec2(c.x, c.y + s); p2 = ImVec2(c.x - hw, c.y - hh); p3 = ImVec2(c.x + hw, c.y - hh); }
  else if (dir == 2) { p1 = ImVec2(c.x - s, c.y); p2 = ImVec2(c.x + hh, c.y - hw); p3 = ImVec2(c.x + hh, c.y + hw); }
  else               { p1 = ImVec2(c.x + s, c.y); p2 = ImVec2(c.x - hh, c.y - hw); p3 = ImVec2(c.x - hh, c.y + hw); }
  d->AddTriangleFilled(p1, p2, p3, col);
}

void DrawPanel(ImDrawList* d, const ImVec2& mn, const ImVec2& mx,
               ImU32 fill, ImU32 border, float rounding, float border_thickness) {
  d->AddRectFilled(mn, mx, fill, rounding);
  if (border_thickness > 0.0f) {
    d->AddRect(mn, mx, border, rounding, 0, border_thickness);
  }
}

// ---- 状态屏：工程级信息面板 ---------------------------------------------

void DrawConsole(ImDrawList* d, const ImVec2& mn, const ImVec2& mx,
                 const GamepadState& st, const VirtualGamepadDevice& dev) {
  const bool is_error = !dev.error().empty();

  // 面板底
  DrawPanel(d, mn, mx, kPanel2, kHairline, S(10), S(1.0f));

  // header band
  const float header_h = S(28);
  const ImVec2 hmn = mn;
  const ImVec2 hmx(mx.x, mn.y + header_h);
  d->AddRectFilled(hmn, hmx, kPanelSink, S(10), ImDrawFlags_RoundCornersTop);
  d->AddLine(ImVec2(mn.x + S(6), hmx.y), ImVec2(mx.x - S(6), hmx.y), kHairline, S(1));

  // 左侧：STATUS  ● READY/ERROR
  float lx = mn.x + S(14);
  float ly = (hmn.y + hmx.y) * 0.5f;
  d->AddText(ImVec2(lx, ly - ImGui::CalcTextSize("STATUS").y * 0.5f), kTextDim, "STATUS");
  float after_lbl = lx + ImGui::CalcTextSize("STATUS").x + S(10);
  const char* state_text = is_error ? "ERROR" : (dev.ready() ? "READY" : "DISCONNECTED");
  const ImU32 state_color = is_error ? kError : (dev.ready() ? kAccent : kWarn);
  d->AddCircleFilled(ImVec2(after_lbl + S(4), ly), S(4), state_color, 20);
  d->AddText(ImVec2(after_lbl + S(14), ly - ImGui::CalcTextSize(state_text).y * 0.5f),
             state_color, state_text);

  // 右侧：设备诊断行 — 从右往左排版避免长字符串挤压。
  char id_buf[20];
  std::snprintf(id_buf, sizeof(id_buf), "0x%04X:0x%04X",
                VirtualGamepadDevice::kVendorId, VirtualGamepadDevice::kProductId);
  char fd_buf[20];
  std::snprintf(fd_buf, sizeof(fd_buf), "fd=%d", dev.fd());
  const std::string path = dev.device_path().empty() ? "(none)" : dev.device_path();
  std::string dev_line = path;

  float rx = mx.x - S(14);
  auto right_text = [&](const char* s, ImU32 col) {
    float w = ImGui::CalcTextSize(s).x;
    rx -= w;
    d->AddText(ImVec2(rx, ly - ImGui::CalcTextSize(s).y * 0.5f), col, s);
  };
  right_text(id_buf, kTextMute);
  rx -= S(12);
  right_text(fd_buf, dev.ready() ? kTextMono : kTextDim);
  rx -= S(12);
  right_text(dev_line.c_str(), dev.ready() ? kTextMono : kTextDim);
  rx -= S(10);
  right_text("DEV", kTextDim);

  // body
  const ImVec2 bmn(mn.x, hmx.y);
  const ImVec2 bmx = mx;

  if (is_error) {
    // 错误模式：errno 行 + 原始错误文本（自动换行）
    char eb[64];
    std::snprintf(eb, sizeof(eb), "errno=%d  %s", dev.last_errno(), ErrnoSymbol(dev.last_errno()));
    d->AddText(ImVec2(bmn.x + S(14), bmn.y + S(10)), kError, eb);
    d->AddRectFilled(ImVec2(bmn.x + S(10), bmn.y + S(34)),
                     ImVec2(bmx.x - S(10), bmx.y - S(10)), kErrorSoft, S(6));
    // 错误原文：用 AddText 不带 wrap，逐行手动 wrap 一遍。
    const std::string& raw = dev.error();
    const float pad_l = S(18), pad_r = S(18), tx = bmn.x + pad_l;
    float ty = bmn.y + S(40);
    const float max_w = (bmx.x - pad_r) - tx;
    size_t cursor = 0;
    while (cursor < raw.size() && ty < bmx.y - S(8)) {
      // 估算这一行能塞多少字符
      size_t lo = 1, hi = raw.size() - cursor;
      while (lo < hi) {
        size_t mid = (lo + hi + 1) / 2;
        if (ImGui::CalcTextSize(raw.c_str() + cursor, raw.c_str() + cursor + mid).x <= max_w) lo = mid;
        else hi = mid - 1;
      }
      // 优先在空格处折行
      size_t take = lo;
      if (cursor + take < raw.size()) {
        size_t back = take;
        while (back > take / 2 && raw[cursor + back] != ' ' && raw[cursor + back] != ':' && raw[cursor + back] != ',') back--;
        if (back > take / 2) take = back + 1;
      }
      std::string line = raw.substr(cursor, take);
      d->AddText(ImVec2(tx, ty), kTextPrimary, line.c_str());
      ty += ImGui::GetTextLineHeight() + S(2);
      cursor += take;
    }
    return;
  }

  // 正常模式：6 路轴的等距三列 × 两行 + 进度条
  const float pad_l = S(18), pad_r = S(18);
  const float cell_w = (bmx.x - bmn.x - pad_l - pad_r) / 3.0f;
  const float row_y0 = bmn.y + S(12);
  const float row_y1 = bmn.y + S(52);

  auto Axis = [&](float cx, float cy, const char* name, float v, bool bipolar) {
    char buf[24];
    std::snprintf(buf, sizeof(buf), "%s %+6.3f", name, v);
    d->AddText(ImVec2(cx, cy), kTextMono, buf);
    // 进度条
    float bw = cell_w - S(20);
    float by = cy + ImGui::GetTextLineHeight() + S(6);
    float bh = S(5);
    d->AddRectFilled(ImVec2(cx, by), ImVec2(cx + bw, by + bh), kPanelSink, S(2));
    if (bipolar) {
      float mid = cx + bw * 0.5f;
      d->AddLine(ImVec2(mid, by - S(1)), ImVec2(mid, by + bh + S(1)), kHairline, S(1));
      float fill = std::min(1.0f, std::abs(v)) * bw * 0.5f;
      if (fill > 1.0f) {
        float fx = v > 0 ? mid : mid - fill;
        d->AddRectFilled(ImVec2(fx, by), ImVec2(fx + fill, by + bh), kAccent, S(2));
      }
    } else {
      float fill = std::max(0.0f, std::min(1.0f, v)) * bw;
      if (fill > 1.0f) {
        d->AddRectFilled(ImVec2(cx, by), ImVec2(cx + fill, by + bh), kAccent, S(2));
      }
    }
  };

  const float c0 = bmn.x + pad_l;
  const float c1 = c0 + cell_w;
  const float c2 = c1 + cell_w;
  Axis(c0, row_y0, "LX", st.lx, true);
  Axis(c1, row_y0, "LY", st.ly, true);
  Axis(c2, row_y0, "LT", st.lt, false);
  Axis(c0, row_y1, "RX", st.rx, true);
  Axis(c1, row_y1, "RY", st.ry, true);
  Axis(c2, row_y1, "RT", st.rt, false);
}

// ---- 控件 ----------------------------------------------------------------

void DrawBumper(const char* label, const ImVec2& pos, const ImVec2& sz,
                bool* pressed, ButtonMemory* mem, bool hold_mode) {
  ImGui::SetCursorPos(pos); ImGui::PushID(label);
  ImGui::InvisibleButton("bumper", sz);
  bool dn = ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Left);
  if (hold_mode && ImGui::IsItemClicked(ImGuiMouseButton_Left)) mem->latched = !mem->latched;
  bool hv = ImGui::IsItemHovered();
  *pressed = DigitalPressed(dn, *mem);
  const ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
  ImDrawList* d = ImGui::GetWindowDrawList();
  const float r = S(7);
  ImU32 fill = *pressed ? kAccent : (hv ? kPanel2 : kPanel);
  ImU32 bd = *pressed ? kAccentBd : kHairline;
  d->AddRectFilled(mn, mx, fill, r);
  d->AddRect(mn, mx, bd, r, 0, S(1.2f));
  ImU32 tc = *pressed ? IM_COL32(0x0B,0x0D,0x11,0xFF) : kTextPrimary;
  const ImVec2 ts = ImGui::CalcTextSize(label);
  d->AddText(ImVec2(mn.x + (sz.x - ts.x) * 0.5f, mn.y + (sz.y - ts.y) * 0.5f), tc, label);
  ImGui::PopID();
}

void DrawTriggerBar(const char* label, const ImVec2& pos, const ImVec2& sz, float* val) {
  ImGui::SetCursorPos(pos); ImGui::PushID(label);
  ImGui::InvisibleButton("trigger", sz);
  ImDrawList* d = ImGui::GetWindowDrawList();
  const ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
  if (ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
    float f = (ImGui::GetIO().MousePos.x - mn.x) / sz.x;
    *val = std::max(0.0f, std::min(1.0f, f));
  }
  const float r = S(5);
  d->AddRectFilled(mn, mx, kPanelSink, r);
  float fw = sz.x * (*val);
  if (fw > 2.0f) d->AddRectFilled(mn, ImVec2(mn.x + fw, mx.y), kAccent, r);
  // 50% 中位刻度
  float mid = mn.x + sz.x * 0.5f;
  d->AddLine(ImVec2(mid, mn.y + S(2)), ImVec2(mid, mx.y - S(2)), kHairline, S(1));
  d->AddRect(mn, mx, kHairline, r, 0, S(1));
  char buf[16];
  std::snprintf(buf, sizeof(buf), "%s %4.2f", label, *val);
  ImU32 tc = *val > 0.05f ? IM_COL32(0x0B,0x0D,0x11,0xFF) : kTextMute;
  const ImVec2 ts = ImGui::CalcTextSize(buf);
  d->AddText(ImVec2(mn.x + S(10), mn.y + (sz.y - ts.y) * 0.5f), tc, buf);
  ImGui::PopID();
}

void DrawCenterButton(const char* label, const ImVec2& pos, const ImVec2& sz,
                      bool* pressed, ButtonMemory* mem, bool hold_mode) {
  ImGui::SetCursorPos(pos); ImGui::PushID(label);
  ImGui::InvisibleButton("center", sz);
  bool dn = ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Left);
  bool hv = ImGui::IsItemHovered();
  if (hold_mode && ImGui::IsItemClicked(ImGuiMouseButton_Left)) mem->latched = !mem->latched;
  *pressed = DigitalPressed(dn, *mem);
  const ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
  ImDrawList* d = ImGui::GetWindowDrawList();
  const float r = sz.y * 0.5f;
  ImU32 fill = *pressed ? kAccent : (hv ? kPanel2 : kPanel);
  ImU32 bd = *pressed ? kAccentBd : kHairline;
  d->AddRectFilled(mn, mx, fill, r);
  d->AddRect(mn, mx, bd, r, 0, S(1));
  ImU32 tc = *pressed ? IM_COL32(0x0B,0x0D,0x11,0xFF) : kTextMute;
  const ImVec2 ts = ImGui::CalcTextSize(label);
  d->AddText(ImVec2(mn.x + (sz.x - ts.x) * 0.5f, mn.y + (sz.y - ts.y) * 0.5f), tc, label);
  ImGui::PopID();
}

bool DrawToolbarButton(const char* label, const ImVec2& pos, const ImVec2& sz) {
  ImGui::SetCursorPos(pos); ImGui::PushID(label);
  ImGui::InvisibleButton("toolbar_btn", sz);
  bool hv = ImGui::IsItemHovered();
  bool dn = ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Left);
  bool cl = ImGui::IsItemClicked(ImGuiMouseButton_Left);
  const ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
  ImDrawList* d = ImGui::GetWindowDrawList();
  const float r = S(7);
  ImU32 fill = dn ? kAccent : (hv ? kPanel2 : kPanel);
  ImU32 bd = dn ? kAccentBd : kHairline;
  d->AddRectFilled(mn, mx, fill, r);
  d->AddRect(mn, mx, bd, r, 0, S(1));
  ImU32 tc = dn ? IM_COL32(0x0B,0x0D,0x11,0xFF) : kTextPrimary;
  const ImVec2 ts = ImGui::CalcTextSize(label);
  d->AddText(ImVec2(mn.x + (sz.x - ts.x) * 0.5f, mn.y + (sz.y - ts.y) * 0.5f), tc, label);
  ImGui::PopID();
  return cl;
}

void DrawToolbarToggle(const char* label, const ImVec2& pos, const ImVec2& sz, bool* value) {
  ImGui::SetCursorPos(pos); ImGui::PushID(label);
  ImGui::InvisibleButton("toolbar_tg", sz);
  if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) *value = !*value;
  bool hv = ImGui::IsItemHovered();
  const ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
  ImDrawList* d = ImGui::GetWindowDrawList();
  const float r = S(7);
  ImU32 fill = *value ? kAccentSoft : (hv ? kPanel2 : kPanel);
  ImU32 bd = *value ? kAccent : kHairline;
  d->AddRectFilled(mn, mx, fill, r);
  d->AddRect(mn, mx, bd, r, 0, S(1));
  // 指示点
  ImVec2 dot(mn.x + S(13), (mn.y + mx.y) * 0.5f);
  d->AddCircleFilled(dot, S(4.5f), *value ? kAccent : kTextDim, 20);
  if (*value) d->AddCircle(dot, S(7), kAccentGlow, 24, S(1.5f));
  const ImVec2 ts = ImGui::CalcTextSize(label);
  d->AddText(ImVec2(mn.x + S(24), mn.y + (sz.y - ts.y) * 0.5f),
             *value ? kTextPrimary : kTextMute, label);
  ImGui::PopID();
}

// ---- 摇杆 ----------------------------------------------------------------

void DrawStick(const char* label, const char* click_name,
               const ImVec2& center, float radius,
               float* x, float* y, bool* clicked,
               StickMemory* stk, ButtonMemory* btn, bool hold_mode) {
  const float pad = S(14);
  ImGui::SetCursorPos(ImVec2(center.x - radius - pad, center.y - radius - pad));
  ImGui::PushID(label);
  ImVec2 bs(radius * 2 + pad * 2, radius * 2 + pad * 2);
  ImGui::InvisibleButton("stick_area", bs);
  bool active = ImGui::IsItemActive(), down = ImGui::IsMouseDown(ImGuiMouseButton_Left);
  bool dbl = ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);

  ImDrawList* d = ImGui::GetWindowDrawList();
  ImVec2 sc = center;  // 整个 ImGui 窗口位于 (0,0)，cursor 与 draw 坐标对齐

  // well：深底 + 1px hairline + 两条同心 guide
  d->AddCircleFilled(sc, radius, kPanelSink, 64);
  d->AddCircle(sc, radius, kHairline, 64, S(1.2f));
  d->AddCircle(sc, radius * 0.66f, kHairlineDim, 56, S(1));
  d->AddCircle(sc, radius * 0.33f, kHairlineDim, 48, S(1));

  // knob 位置
  ImVec2 knob = sc;
  bool moved = false;
  if (stk->locked) {
    *x = stk->lock_x; *y = stk->lock_y;
    knob = ImVec2(sc.x + (*x) * radius, sc.y - (*y) * radius);
    moved = true;
  } else if (active && down) {
    knob = StickFromMouse(sc, ImGui::GetIO().MousePos, radius, x, y);
    stk->last_x = *x; stk->last_y = *y;
    moved = (std::abs(*x) > 0.001f || std::abs(*y) > 0.001f);
  } else {
    *x = 0.0f; *y = 0.0f;
  }

  // 双击切换锁定
  if (dbl) {
    if (stk->locked) {
      stk->locked = false; *x = 0.0f; *y = 0.0f; knob = sc; moved = false;
    } else {
      stk->locked = true;
      stk->lock_x = (active && down) ? *x : stk->last_x;
      stk->lock_y = (active && down) ? *y : stk->last_y;
      *x = stk->lock_x; *y = stk->lock_y;
      knob = ImVec2(sc.x + (*x) * radius, sc.y - (*y) * radius);
      moved = true;
    }
  }

  // 旋钮：底下软阴影 + 主体 + 高光点；激活时改 teal
  d->AddCircleFilled(ImVec2(knob.x, knob.y + S(2)), S(15), IM_COL32(0,0,0,0x55), 24);
  d->AddCircleFilled(knob, S(15), moved ? kAccent : IM_COL32(0x4F,0x57,0x66,0xFF), 28);
  d->AddCircle(knob, S(15), moved ? kAccentBd : kHairline, 28, S(1));
  d->AddCircleFilled(ImVec2(knob.x - S(15) * 0.30f, knob.y - S(15) * 0.30f),
                     S(15) * 0.24f, IM_COL32(0xFF,0xFF,0xFF,0x22), 18);

  // 锁定指示
  if (stk->locked) {
    const float br = S(7);
    ImVec2 bc(sc.x + radius * 0.78f, sc.y - radius * 0.78f);
    d->AddCircleFilled(bc, br, kAccentSoft, 20);
    d->AddCircle(bc, br, kAccent, 20, S(1));
    d->AddText(ImVec2(bc.x - S(2.5f), bc.y - ImGui::CalcTextSize("L").y * 0.5f), kAccent, "L");
  }

  // 标签 + 坐标读数（在 well 下方）
  char coord[32];
  std::snprintf(coord, sizeof(coord), "%+5.2f, %+5.2f", *x, *y);
  const ImVec2 ls = ImGui::CalcTextSize(label);
  float label_y = sc.y + radius + S(10);
  d->AddText(ImVec2(sc.x - ls.x * 0.5f, label_y), kTextMute, label);
  const ImVec2 cs = ImGui::CalcTextSize(coord);
  d->AddText(ImVec2(sc.x - cs.x * 0.5f, label_y + ls.y + S(2)), kTextMono, coord);

  // 摇杆按下的小 toggle（LS/RS）
  const float cbw = S(38), cbh = S(18);
  float cbx = sc.x + ls.x * 0.5f + S(10);
  float cby = label_y + (ls.y - cbh) * 0.5f;
  ImGui::SetCursorPos(ImVec2(cbx, cby)); ImGui::PushID("click");
  ImGui::InvisibleButton("click_btn", ImVec2(cbw, cbh));
  bool cd = ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Left);
  if (hold_mode && ImGui::IsItemClicked(ImGuiMouseButton_Left)) btn->latched = !btn->latched;
  *clicked = DigitalPressed(cd, *btn);
  const ImVec2 cmn = ImGui::GetItemRectMin(), cmx = ImGui::GetItemRectMax();
  d->AddRectFilled(cmn, cmx, *clicked ? kAccent : kPanel, cbh * 0.5f);
  d->AddRect(cmn, cmx, *clicked ? kAccentBd : kHairline, cbh * 0.5f, 0, S(1));
  ImU32 tc = *clicked ? IM_COL32(0x0B,0x0D,0x11,0xFF) : kTextMute;
  const ImVec2 cts = ImGui::CalcTextSize(click_name);
  d->AddText(ImVec2(cmn.x + (cbw - cts.x) * 0.5f, cmn.y + (cbh - cts.y) * 0.5f), tc, click_name);
  ImGui::PopID();

  ImGui::PopID();
}

// ---- D-pad ---------------------------------------------------------------

void DrawDpad(const ImVec2& center, float arm,
              bool* up, bool* down, bool* left, bool* right,
              ButtonMemory* um, ButtonMemory* dm,
              ButtonMemory* lm, ButtonMemory* rm, bool hold_mode) {
  const float h = arm * 0.5f;
  ImDrawList* d = ImGui::GetWindowDrawList();
  struct DB { const char* id; ImVec2 p; int dir; bool* s; ButtonMemory* m; };
  DB btns[] = {
    {"dU", ImVec2(center.x - h, center.y - arm - h), 0, up,    um},
    {"dD", ImVec2(center.x - h, center.y + h),       1, down,  dm},
    {"dL", ImVec2(center.x - arm - h, center.y - h), 2, left,  lm},
    {"dR", ImVec2(center.x + h, center.y - h),       3, right, rm},
  };
  for (auto& b : btns) {
    ImGui::SetCursorPos(b.p); ImGui::PushID(b.id);
    ImGui::InvisibleButton("dbtn", ImVec2(arm, arm));
    bool dn = ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Left);
    bool hv = ImGui::IsItemHovered();
    if (hold_mode && ImGui::IsItemClicked(ImGuiMouseButton_Left)) b.m->latched = !b.m->latched;
    *b.s = DigitalPressed(dn, *b.m);
    const ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
    bool on = *b.s;
    ImU32 fill = on ? kAccent : (hv ? kPanel2 : kPanel);
    ImU32 bd = on ? kAccentBd : kHairline;
    d->AddRectFilled(mn, mx, fill, S(6));
    d->AddRect(mn, mx, bd, S(6), 0, S(1));
    ImVec2 ac((mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f);
    DrawArrow(d, ac, arm * 0.22f, b.dir, on ? IM_COL32(0x0B,0x0D,0x11,0xFF) : kTextMute);
    ImGui::PopID();
  }
  // 中心方块（视觉黏合）
  ImVec2 cm(center.x - h, center.y - h), cM(center.x + h, center.y + h);
  bool any = *up || *down || *left || *right;
  d->AddRectFilled(cm, cM, any ? kAccentSoft : kPanelSink, S(2));
  d->AddRect(cm, cM, any ? kAccent : kHairlineDim, S(2), 0, S(1));

  // 标签
  d->AddText(ImVec2(center.x - ImGui::CalcTextSize("D-PAD").x * 0.5f,
                    center.y + arm * 1.5f + S(8)),
             kTextMicro, "D-PAD");
}

// ---- ABXY ----------------------------------------------------------------

void DrawFaceButtons(const ImVec2& center, float radius, float spacing,
                     bool* a, bool* b, bool* x, bool* y,
                     ButtonMemory* am, ButtonMemory* bm,
                     ButtonMemory* xm, ButtonMemory* ym, bool hold_mode) {
  ImDrawList* d = ImGui::GetWindowDrawList();
  // 微弱十字 guide
  d->AddLine(ImVec2(center.x, center.y - spacing + radius), ImVec2(center.x, center.y + spacing - radius), kHairlineDim, S(1.5f));
  d->AddLine(ImVec2(center.x - spacing + radius, center.y), ImVec2(center.x + spacing - radius, center.y), kHairlineDim, S(1.5f));

  float dd = radius * 2;
  ImVec2 btn(dd + S(10), dd + S(10));
  struct FB { const char* l; ImVec2 p; ImU32 c; bool* s; ButtonMemory* m; };
  FB fbs[] = {
    {"Y", ImVec2(center.x - radius, center.y - spacing - radius), kFaceY, y, ym},
    {"A", ImVec2(center.x - radius, center.y + spacing - radius), kFaceA, a, am},
    {"X", ImVec2(center.x - spacing - radius, center.y - radius), kFaceX, x, xm},
    {"B", ImVec2(center.x + spacing - radius, center.y - radius), kFaceB, b, bm},
  };
  for (auto& fb : fbs) {
    ImGui::SetCursorPos(fb.p); ImGui::PushID(fb.l);
    ImGui::InvisibleButton("face", btn);
    bool dn = ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Left);
    bool hv = ImGui::IsItemHovered();
    if (hold_mode && ImGui::IsItemClicked(ImGuiMouseButton_Left)) fb.m->latched = !fb.m->latched;
    *fb.s = DigitalPressed(dn, *fb.m);
    const ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
    const ImVec2 bc((mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f);
    bool on = *fb.s;
    // 外圈 ring + 内填充
    if (on) {
      // 按下：accent 外圈光晕 + 主体填色 + thin 内描边
      d->AddCircleFilled(bc, radius + S(3), kAccentGlow, 32);
      d->AddCircleFilled(bc, radius, fb.c, 32);
      d->AddCircle(bc, radius, kAccentBd, 32, S(1.4f));
    } else {
      d->AddCircleFilled(bc, radius, hv ? IM_COL32(0x18,0x1D,0x26,0xFF) : kPanel, 32);
      d->AddCircle(bc, radius, fb.c, 32, S(1.6f));
    }
    const ImVec2 ts = ImGui::CalcTextSize(fb.l);
    d->AddText(ImVec2(bc.x - ts.x * 0.5f, bc.y - ts.y * 0.5f), on ? kTextPrimary : fb.c, fb.l);
    ImGui::PopID();
  }

  d->AddText(ImVec2(center.x - ImGui::CalcTextSize("ABXY").x * 0.5f,
                    center.y + spacing + radius + S(20)),
             kTextMicro, "ABXY");
}

// ---- 重置 ----------------------------------------------------------------

void ResetState(GamepadState* st) { *st = GamepadState{}; }

}  // namespace

int main(int, char**) {
  setenv("XDG_CACHE_HOME", "/tmp", 0);

  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_GAMECONTROLLER) != 0) {
    std::fprintf(stderr, "SDL init: %s\n", SDL_GetError());
    return 1;
  }
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
  SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
  SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

  SDL_Window* window = SDL_CreateWindow(
      "virtual_gamepad_linux",
      SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
      (int)S(kBaseW), (int)S(kBaseH),
      SDL_WINDOW_OPENGL | SDL_WINDOW_ALLOW_HIGHDPI);
  if (!window) {
    std::fprintf(stderr, "Window: %s\n", SDL_GetError());
    SDL_Quit();
    return 1;
  }
  SDL_GLContext gl = SDL_GL_CreateContext(window);
  if (!gl) {
    std::fprintf(stderr, "GL: %s\n", SDL_GetError());
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 1;
  }
  SDL_GL_MakeCurrent(window, gl);
  SDL_GL_SetSwapInterval(1);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.IniFilename = nullptr;
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  ImGui::StyleColorsDark();
  ImGuiStyle& stl = ImGui::GetStyle();
  stl.WindowRounding = 0;
  stl.FrameRounding = 6;
  stl.GrabRounding = 4;
  stl.ChildRounding = 0;
  stl.ScrollbarSize = 0;
  stl.WindowPadding = ImVec2(0, 0);
  stl.WindowBorderSize = 0;
  stl.Colors[ImGuiCol_WindowBg] = ImVec4(0, 0, 0, 0);

  ImGui_ImplSDL2_InitForOpenGL(window, gl);
  ImGui_ImplOpenGL3_Init("#version 130");

  VirtualGamepadDevice device;
  bool dev_ok = device.Create();
  GamepadState state;
  UiMemory ui;
  std::string last_error = dev_ok ? "" : device.error();

  bool done = false;
  while (!done) {
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
      ImGui_ImplSDL2_ProcessEvent(&ev);
      if (ev.type == SDL_QUIT) done = true;
      if (ev.type == SDL_WINDOWEVENT && ev.window.event == SDL_WINDOWEVENT_CLOSE) done = true;
      if (ev.type == SDL_KEYDOWN) {
        if (ev.key.keysym.sym == SDLK_ESCAPE) done = true;
        if (ev.key.keysym.sym == SDLK_q && (SDL_GetModState() & KMOD_CTRL)) done = true;
      }
    }
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame(window);
    ImGui::NewFrame();

    // SDL 已经是普通系统窗口；ImGui 只绘制客户区里的面板内容。
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("##main", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground |
                 ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImDrawList* draw = ImGui::GetWindowDrawList();

    const ImVec2 win_mn(0.0f, 0.0f);
    const ImVec2 win_mx(S(kBaseW), S(kBaseH));
    DrawPanel(draw, win_mn, win_mx, kPanel, kHairline, S(10), S(1.0f));

    // === 状态屏 =========================================================
    const ImVec2 cs_mn(S(14), S(14));
    const ImVec2 cs_mx(win_mx.x - S(14), S(14) + S(128));
    DrawConsole(draw, cs_mn, cs_mx, state, device);

    // === 肩键 + 扳机 + 中心按钮 =========================================
    const float band_y = S(168);
    DrawBumper("LB", ImVec2(S(20), band_y), ImVec2(S(176), S(26)),
               &state.lb, &ui.lb, ui.hold_mode);
    DrawBumper("RB", ImVec2(win_mx.x - S(20) - S(176), band_y), ImVec2(S(176), S(26)),
               &state.rb, &ui.rb, ui.hold_mode);
    DrawTriggerBar("LT", ImVec2(S(20), band_y + S(32)), ImVec2(S(176), S(22)), &state.lt);
    DrawTriggerBar("RT", ImVec2(win_mx.x - S(20) - S(176), band_y + S(32)),
                   ImVec2(S(176), S(22)), &state.rt);

    // BACK / GUIDE / START：居中三连
    {
      const float pill_y = band_y + S(6);
      const float pill_h = S(22);
      const float gap = S(8);
      const float w_back = S(60), w_guide = S(72), w_start = S(60);
      const float total = w_back + w_guide + w_start + gap * 2;
      const float cx0 = (win_mx.x - total) * 0.5f;
      DrawCenterButton("BACK", ImVec2(cx0, pill_y), ImVec2(w_back, pill_h),
                       &state.back, &ui.back, ui.hold_mode);
      DrawCenterButton("GUIDE", ImVec2(cx0 + w_back + gap, pill_y), ImVec2(w_guide, pill_h),
                       &state.guide, &ui.guide, ui.hold_mode);
      DrawCenterButton("START", ImVec2(cx0 + w_back + w_guide + gap * 2, pill_y),
                       ImVec2(w_start, pill_h), &state.start, &ui.start, ui.hold_mode);
      // 标签
      const char* lbl = "SYSTEM";
      const ImVec2 ts = ImGui::CalcTextSize(lbl);
      draw->AddText(ImVec2(win_mx.x * 0.5f - ts.x * 0.5f, pill_y + pill_h + S(4)),
                    kTextMicro, lbl);
    }

    // === 主控件网格 =====================================================
    const float stick_r = S(54);
    const float left_x  = S(190);
    const float right_x = win_mx.x - S(190);
    const float top_y   = S(276);
    const float bot_y   = S(460);

    DrawStick("LEFT STICK", "LS",
              ImVec2(left_x, top_y), stick_r,
              &state.lx, &state.ly, &state.ls,
              &ui.left_stick, &ui.ls, ui.hold_mode);
    DrawStick("RIGHT STICK", "RS",
              ImVec2(right_x, bot_y), stick_r,
              &state.rx, &state.ry, &state.rs,
              &ui.right_stick, &ui.rs, ui.hold_mode);

    DrawDpad(ImVec2(left_x, bot_y), S(34),
             &state.dpad_up, &state.dpad_down, &state.dpad_left, &state.dpad_right,
             &ui.dpad_up, &ui.dpad_down, &ui.dpad_left, &ui.dpad_right, ui.hold_mode);

    DrawFaceButtons(ImVec2(right_x, top_y), S(20), S(28),
                    &state.a, &state.b, &state.x, &state.y,
                    &ui.a, &ui.b, &ui.x, &ui.y, ui.hold_mode);

    // === 工具栏 =========================================================
    {
      const float tb_y = win_mx.y - S(40);
      const float tb_btn_h = S(26);
      float tx = S(20);
      if (DrawToolbarButton("Reconnect", ImVec2(tx, tb_y), ImVec2(S(96), tb_btn_h))) {
        dev_ok = device.Create();
        last_error = dev_ok ? "" : device.error();
      }
      tx += S(96) + S(8);
      if (DrawToolbarButton("Reset", ImVec2(tx, tb_y), ImVec2(S(64), tb_btn_h))) {
        ui = UiMemory{};
        ResetState(&state);
        if (device.ready()) device.SendState(state);
      }
      tx += S(64) + S(10);
      DrawToolbarToggle("Hold Mode", ImVec2(tx, tb_y), ImVec2(S(120), tb_btn_h), &ui.hold_mode);

      // 右侧：scale group
      const float scale_w = S(32);
      const float scale_label_w = S(56);
      const float scale_total = scale_w * 2 + scale_label_w + S(4) * 2;
      float sx = win_mx.x - S(20) - scale_total;
      if (DrawToolbarButton("-", ImVec2(sx, tb_y), ImVec2(scale_w, tb_btn_h))) {
        g_ui_scale = std::max(kMinScale, g_ui_scale - kScaleStep);
        ResizeWindowToScale(window);
      }
      sx += scale_w + S(4);
      char scale_buf[16];
      std::snprintf(scale_buf, sizeof(scale_buf), "%d%%",
                    static_cast<int>(std::lround(g_ui_scale * 100.0f)));
      // 比例数字背景小 chip
      draw->AddRectFilled(ImVec2(sx, tb_y), ImVec2(sx + scale_label_w, tb_y + tb_btn_h),
                          kPanelSink, S(7));
      draw->AddRect(ImVec2(sx, tb_y), ImVec2(sx + scale_label_w, tb_y + tb_btn_h),
                    kHairline, S(7), 0, S(1));
      ImVec2 sts = ImGui::CalcTextSize(scale_buf);
      draw->AddText(ImVec2(sx + (scale_label_w - sts.x) * 0.5f,
                           tb_y + (tb_btn_h - sts.y) * 0.5f),
                    kTextMono, scale_buf);
      sx += scale_label_w + S(4);
      if (DrawToolbarButton("+", ImVec2(sx, tb_y), ImVec2(scale_w, tb_btn_h))) {
        g_ui_scale = std::min(kMaxScale, g_ui_scale + kScaleStep);
        ResizeWindowToScale(window);
      }
    }

    ImGui::End();
    ImGui::Render();

    int dw = 0, dh = 0;
    SDL_GL_GetDrawableSize(window, &dw, &dh);
    glViewport(0, 0, dw, dh);
    // 普通窗口背景，面板外的额外客户区保持深色。
    glClearColor(0.063f, 0.075f, 0.094f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    SDL_GL_SwapWindow(window);

    if (device.ready() && !device.SendState(state)) last_error = device.error();
    else if (device.ready()) last_error.clear();
  }

  device.Destroy();
  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplSDL2_Shutdown();
  ImGui::DestroyContext();
  SDL_GL_DeleteContext(gl);
  SDL_DestroyWindow(window);
  SDL_Quit();
  return 0;
}
