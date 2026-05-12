#include "virtual_gamepad.h"

#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_sdl2.h"

#include <SDL.h>
#include <SDL_opengl.h>
#include <SDL_syswm.h>

#include <X11/Xlib.h>
#include <X11/extensions/shape.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace {

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

constexpr ImU32 kBodyFill    = IM_COL32(0x28,0x2C,0x38,0xFF);
constexpr ImU32 kBodyBorder  = IM_COL32(0x3A,0x3F,0x4D,0xB0);
constexpr ImU32 kBodyShadow1 = IM_COL32(0x00,0x00,0x00,0x40);
constexpr ImU32 kBodyShadow2 = IM_COL32(0x00,0x00,0x00,0x1C);

constexpr ImU32 kStickWell   = IM_COL32(0x20,0x24,0x2E,0xFF);
constexpr ImU32 kStickRing   = IM_COL32(0x35,0x3A,0x48,0xFF);
constexpr ImU32 kStickKnob   = IM_COL32(0x55,0x5B,0x68,0xFF);
constexpr ImU32 kStickActive = IM_COL32(0x4A,0xDE,0x80,0xFF);
constexpr ImU32 kStickCross  = IM_COL32(0x2E,0x33,0x40,0x60);

constexpr ImU32 kBtnFill     = IM_COL32(0x33,0x38,0x45,0xFF);
constexpr ImU32 kBtnBorder   = IM_COL32(0x42,0x48,0x56,0xFF);
constexpr ImU32 kBtnActive   = IM_COL32(0x4A,0xDE,0x80,0xFF);
constexpr ImU32 kBtnActiveBd = IM_COL32(0x6E,0xF0,0x9A,0xFF);

constexpr ImU32 kDpadFill   = IM_COL32(0x2E,0x32,0x3E,0xFF);
constexpr ImU32 kDpadBorder = IM_COL32(0x3E,0x43,0x50,0xFF);
constexpr ImU32 kDpadArrow  = IM_COL32(0x88,0x8E,0x9C,0xFF);

constexpr ImU32 kTriggerBg   = IM_COL32(0x1E,0x22,0x2C,0xFF);
constexpr ImU32 kTriggerFill = IM_COL32(0x4A,0xDE,0x80,0xFF);
constexpr ImU32 kTriggerBdr  = IM_COL32(0x38,0x3D,0x4A,0xFF);
constexpr ImU32 kTriggerTick = IM_COL32(0x30,0x35,0x42,0x80);

constexpr ImU32 kFaceA = IM_COL32(0x44,0xE8,0x55,0xFF);
constexpr ImU32 kFaceB = IM_COL32(0xFF,0x4B,0x4B,0xFF);
constexpr ImU32 kFaceX = IM_COL32(0x4B,0x8B,0xFF,0xFF);
constexpr ImU32 kFaceY = IM_COL32(0xFF,0xD4,0x4B,0xFF);

constexpr ImU32 kScrBg    = IM_COL32(0x0E,0x10,0x16,0xFF);
constexpr ImU32 kScrBdr   = IM_COL32(0x2E,0x33,0x40,0xFF);
constexpr ImU32 kBarBg    = IM_COL32(0x18,0x1C,0x24,0xFF);
constexpr ImU32 kBarFill  = IM_COL32(0x4A,0xDE,0x80,0xFF);
constexpr ImU32 kBarCtr   = IM_COL32(0x34,0x39,0x46,0x80);
constexpr ImU32 kDotOn    = IM_COL32(0x4A,0xDE,0x80,0xFF);
constexpr ImU32 kDotOff   = IM_COL32(0x2A,0x2E,0x38,0xD0);
constexpr ImU32 kCloseBtn = IM_COL32(0x40,0x45,0x52,0xD0);
constexpr ImU32 kCloseHvr = IM_COL32(0xFF,0x4B,0x4B,0xFF);

constexpr ImU32 kTextW   = IM_COL32(0xFF,0xFF,0xFF,0xFF);
constexpr ImU32 kTextDim = IM_COL32(0x7C,0x82,0x90,0xFF);
constexpr ImU32 kTextAcc = IM_COL32(0x4A,0xDE,0x80,0xFF);
constexpr ImU32 kTextScr = IM_COL32(0x60,0xE8,0x80,0xFF);
constexpr ImU32 kTextScrD= IM_COL32(0x50,0x55,0x62,0xFF);

constexpr float kW=930, kH=740, kBW=850, kBH=640;
constexpr float kStR=68, kKnR=14, kDA=44, kDR=9, kFR=24, kFS=33;
constexpr float kLC=188, kRC=662, kUY=272, kLY=455;
constexpr float kSW=370, kSH=110;

void FillRoundRectMask(Display* dpy, Pixmap pm, GC gc, int x, int y, int w, int h, int r) {
  if (w <= 0 || h <= 0) return;
  r = std::max(0, std::min(r, std::min(w, h) / 2));
  XFillRectangle(dpy, pm, gc, x + r, y, w - 2 * r, h);
  XFillRectangle(dpy, pm, gc, x, y + r, w, h - 2 * r);
  XFillArc(dpy, pm, gc, x, y, r * 2, r * 2, 90 * 64, 90 * 64);
  XFillArc(dpy, pm, gc, x + w - r * 2, y, r * 2, r * 2, 0, 90 * 64);
  XFillArc(dpy, pm, gc, x, y + h - r * 2, r * 2, r * 2, 180 * 64, 90 * 64);
  XFillArc(dpy, pm, gc, x + w - r * 2, y + h - r * 2, r * 2, r * 2, 270 * 64, 90 * 64);
}

void SetWindowShape(SDL_Window* sdl_win, int mx, int my, int mw, int mh) {
  SDL_SysWMinfo info; SDL_VERSION(&info.version);
  if (!SDL_GetWindowWMInfo(sdl_win, &info)) return;
  if (info.subsystem != SDL_SYSWM_X11) return;
  Display* dpy = info.info.x11.display;
  Window   win = info.info.x11.window;
  int w, h; SDL_GetWindowSize(sdl_win, &w, &h);
  Pixmap pm = XCreatePixmap(dpy, win, w, h, 1);
  GC gc = XCreateGC(dpy, pm, 0, nullptr);
  XSetForeground(dpy, gc, 0);
  XFillRectangle(dpy, pm, gc, 0, 0, w, h);
  XSetForeground(dpy, gc, 1);

  // 原生窗口轮廓：顶部肩键区 + 中央主体 + 左右握把 + 底部控制条。
  FillRoundRectMask(dpy, pm, gc, mx + 24, my, 300, 108, 30);
  FillRoundRectMask(dpy, pm, gc, mx + mw - 324, my, 300, 108, 30);
  FillRoundRectMask(dpy, pm, gc, mx + 220, my + 32, mw - 440, 116, 38);
  FillRoundRectMask(dpy, pm, gc, mx + 54, my + 82, mw - 108, 374, 70);
  XFillArc(dpy, pm, gc, mx + 10, my + 278, 338, 338, 0, 360 * 64);
  XFillArc(dpy, pm, gc, mx + mw - 348, my + 278, 338, 338, 0, 360 * 64);
  FillRoundRectMask(dpy, pm, gc, mx + 112, my + 414, mw - 224, 176, 62);
  FillRoundRectMask(dpy, pm, gc, mx + 64, my + mh - 78, mw - 128, 66, 30);

  XShapeCombineMask(dpy, win, ShapeBounding, 0, 0, pm, ShapeSet);
  XShapeCombineMask(dpy, win, ShapeInput, 0, 0, pm, ShapeSet);
  XSync(dpy, False);
  XFreeGC(dpy, gc); XFreePixmap(dpy, pm);
}

bool InRect(const ImVec2& p, float x, float y, float w, float h) {
  return p.x >= x && p.x <= x + w && p.y >= y && p.y <= y + h;
}

bool InCircle(const ImVec2& p, const ImVec2& c, float r) {
  float dx = p.x - c.x, dy = p.y - c.y;
  return dx * dx + dy * dy <= r * r;
}

bool InGamepadShape(const ImVec2& p, const ImVec2& mn, const ImVec2& mx) {
  const float w = mx.x - mn.x, h = mx.y - mn.y;
  return InRect(p, mn.x + 24, mn.y, 300, 108) ||
         InRect(p, mn.x + w - 324, mn.y, 300, 108) ||
         InRect(p, mn.x + 220, mn.y + 32, w - 440, 116) ||
         InRect(p, mn.x + 54, mn.y + 82, w - 108, 374) ||
         InCircle(p, ImVec2(mn.x + 179, mn.y + 447), 169) ||
         InCircle(p, ImVec2(mn.x + w - 179, mn.y + 447), 169) ||
         InRect(p, mn.x + 112, mn.y + 414, w - 224, 176) ||
         InRect(p, mn.x + 64, mn.y + h - 78, w - 128, 66);
}

ImVec2 StickFromMouse(const ImVec2& c, const ImVec2& m, float r, float* x, float* y) {
  float dx=(m.x-c.x)/r, dy=(c.y-m.y)/r, len=std::sqrt(dx*dx+dy*dy);
  if(len>1.0f){dx/=len;dy/=len;}
  *x=dx;*y=dy; return ImVec2(c.x+dx*r, c.y-dy*r);
}

void DrawArrow(ImDrawList* d, const ImVec2& c, float s, int dir, ImU32 col) {
  float hw=s*0.55f, hh=s*0.7f; ImVec2 p1,p2,p3;
  if(dir==0){p1=ImVec2(c.x,c.y-s);p2=ImVec2(c.x-hw,c.y+hh);p3=ImVec2(c.x+hw,c.y+hh);}
  else if(dir==1){p1=ImVec2(c.x,c.y+s);p2=ImVec2(c.x-hw,c.y-hh);p3=ImVec2(c.x+hw,c.y-hh);}
  else if(dir==2){p1=ImVec2(c.x-s,c.y);p2=ImVec2(c.x+hh,c.y-hw);p3=ImVec2(c.x+hh,c.y+hw);}
  else{p1=ImVec2(c.x+s,c.y);p2=ImVec2(c.x-hh,c.y-hw);p3=ImVec2(c.x-hh,c.y+hw);}
  d->AddTriangleFilled(p1,p2,p3,col);
}

void DrawBodyLayer(ImDrawList* d, const ImVec2& mn, const ImVec2& mx, ImU32 col, float ox, float oy) {
  const float h = mx.y - mn.y;
  d->AddRectFilled(ImVec2(mn.x+24+ox,mn.y+oy),ImVec2(mn.x+324+ox,mn.y+108+oy),col,30.0f);
  d->AddRectFilled(ImVec2(mx.x-324+ox,mn.y+oy),ImVec2(mx.x-24+ox,mn.y+108+oy),col,30.0f);
  d->AddRectFilled(ImVec2(mn.x+220+ox,mn.y+32+oy),ImVec2(mx.x-220+ox,mn.y+148+oy),col,38.0f);
  d->AddRectFilled(ImVec2(mn.x+54+ox,mn.y+82+oy),ImVec2(mx.x-54+ox,mn.y+456+oy),col,70.0f);
  d->AddCircleFilled(ImVec2(mn.x+179+ox,mn.y+447+oy),169.0f,col,80);
  d->AddCircleFilled(ImVec2(mx.x-179+ox,mn.y+447+oy),169.0f,col,80);
  d->AddRectFilled(ImVec2(mn.x+112+ox,mn.y+414+oy),ImVec2(mx.x-112+ox,mn.y+590+oy),col,62.0f);
  d->AddRectFilled(ImVec2(mn.x+64+ox,mn.y+h-78+oy),ImVec2(mx.x-64+ox,mn.y+h-12+oy),col,30.0f);
}

void DrawBody(ImDrawList* d, const ImVec2& mn, const ImVec2& mx) {
  DrawBodyLayer(d,mn,mx,kBodyShadow1,4.0f,6.0f);
  DrawBodyLayer(d,mn,mx,kBodyShadow2,8.0f,10.0f);
  DrawBodyLayer(d,mn,mx,kBodyFill,0.0f,0.0f);
  d->AddRect(ImVec2(mn.x+24,mn.y),ImVec2(mn.x+324,mn.y+108),kBodyBorder,30.0f,0,2.0f);
  d->AddRect(ImVec2(mx.x-324,mn.y),ImVec2(mx.x-24,mn.y+108),kBodyBorder,30.0f,0,2.0f);
  d->AddRect(ImVec2(mn.x+54,mn.y+82),ImVec2(mx.x-54,mn.y+456),kBodyBorder,70.0f,0,2.2f);
  d->AddCircle(ImVec2(mn.x+179,mn.y+447),169.0f,kBodyBorder,80,2.2f);
  d->AddCircle(ImVec2(mx.x-179,mn.y+447),169.0f,kBodyBorder,80,2.2f);
  d->AddRect(ImVec2(mn.x+64,mx.y-78),ImVec2(mx.x-64,mx.y-12),kBodyBorder,30.0f,0,2.0f);
}

void DrawBumper(const char* label, const ImVec2& pos, const ImVec2& sz,
                bool* pressed, ButtonMemory* mem, bool hold_mode) {
  ImGui::SetCursorPos(pos); ImGui::PushID(label);
  ImGui::InvisibleButton("bumper",sz);
  bool dn=ImGui::IsItemActive()&&ImGui::IsMouseDown(ImGuiMouseButton_Left);
  if(hold_mode&&ImGui::IsItemClicked(ImGuiMouseButton_Left)) mem->latched=!mem->latched;
  *pressed=DigitalPressed(dn,*mem);
  const ImVec2 mn=ImGui::GetItemRectMin(),mx=ImGui::GetItemRectMax();
  ImDrawList* d=ImGui::GetWindowDrawList(); float r=9.0f;
  d->AddRectFilled(mn,mx,*pressed?kBtnActive:kBtnFill,r);
  d->AddRect(mn,mx,*pressed?kBtnActiveBd:kBtnBorder,r,0,2.0f);
  const ImVec2 ts=ImGui::CalcTextSize(label);
  d->AddText(ImVec2(mn.x+(sz.x-ts.x)*0.5f,mn.y+(sz.y-ts.y)*0.5f),kTextW,label);
  ImGui::PopID();
}

void DrawTriggerBar(const char* label, const ImVec2& pos, const ImVec2& sz, float* val) {
  ImGui::SetCursorPos(pos); ImGui::PushID(label);
  ImGui::InvisibleButton("trigger",sz);
  ImDrawList* d=ImGui::GetWindowDrawList();
  const ImVec2 mn=ImGui::GetItemRectMin(),mx=ImGui::GetItemRectMax();
  if(ImGui::IsItemActive()&&ImGui::IsMouseDown(ImGuiMouseButton_Left)){
    float f=(ImGui::GetIO().MousePos.x-mn.x)/sz.x;
    *val=std::max(0.0f,std::min(1.0f,f));
  }
  float r=7.0f; d->AddRectFilled(mn,mx,kTriggerBg,r);
  float fw=sz.x*(*val);
  if(fw>3.0f) d->AddRectFilled(mn,ImVec2(mn.x+fw,mx.y),kTriggerFill,r);
  for(float t:{0.25f,0.50f,0.75f}){
    float tx=mn.x+sz.x*t; d->AddLine(ImVec2(tx,mn.y+4),ImVec2(tx,mx.y-4),kTriggerTick,1.5f);
  }
  d->AddRect(mn,mx,kTriggerBdr,r,0,2.0f);
  char buf[32]; std::snprintf(buf,sizeof(buf),"%s  %.2f",label,*val);
  const ImVec2 ts=ImGui::CalcTextSize(buf);
  d->AddText(ImVec2(mn.x+(sz.x-ts.x)*0.5f,mn.y+(sz.y-ts.y)*0.5f),kTextW,buf);
  ImGui::PopID();
}

void DrawCenterButton(const char* label, const ImVec2& pos, const ImVec2& sz,
                      bool* pressed, ButtonMemory* mem, bool hold_mode) {
  ImGui::SetCursorPos(pos); ImGui::PushID(label);
  ImGui::InvisibleButton("center",sz);
  bool dn=ImGui::IsItemActive()&&ImGui::IsMouseDown(ImGuiMouseButton_Left);
  if(hold_mode&&ImGui::IsItemClicked(ImGuiMouseButton_Left)) mem->latched=!mem->latched;
  *pressed=DigitalPressed(dn,*mem);
  const ImVec2 mn=ImGui::GetItemRectMin(),mx=ImGui::GetItemRectMax();
  ImDrawList* d=ImGui::GetWindowDrawList(); float r=sz.y*0.5f;
  d->AddRectFilled(mn,mx,*pressed?kBtnActive:kBtnFill,r);
  d->AddRect(mn,mx,*pressed?kBtnActiveBd:kBtnBorder,r,0,1.8f);
  const ImVec2 ts=ImGui::CalcTextSize(label);
  d->AddText(ImVec2(mn.x+(sz.x-ts.x)*0.5f,mn.y+(sz.y-ts.y)*0.5f),kTextW,label);
  ImGui::PopID();
}

void DrawScreen(ImDrawList* d, const ImVec2& mn, const ImVec2& mx, const GamepadState& st) {
  d->AddRectFilled(mn,mx,kScrBg,10.0f); d->AddRect(mn,mx,kScrBdr,10.0f,0,2.0f);
  float pad=12.0f, bar_w=80.0f, bar_h=13.0f;
  float c1=mn.x+pad, c2=mn.x+(mx.x-mn.x)*0.5f+8.0f, row_h=bar_h+16.0f;
  auto AxisRow=[&](float y, bool left, const char* name, float val){
    char buf[16]; std::snprintf(buf,sizeof(buf),"%s %+6.2f",name,val);
    float cx=left?c1:c2; d->AddText(ImVec2(cx,y),kTextScr,buf);
    float by=y+14.0f; d->AddRectFilled(ImVec2(cx,by),ImVec2(cx+bar_w,by+bar_h),kBarBg,3.0f);
    float mid=cx+bar_w*0.5f; d->AddLine(ImVec2(mid,by+1),ImVec2(mid,by+bar_h-1),kBarCtr,1.0f);
    float fill=std::abs(val)*bar_w*0.5f;
    if(fill>1.0f){float fx=(val>0)?mid:mid-fill; d->AddRectFilled(ImVec2(fx,by+2),ImVec2(fx+fill,by+bar_h-2),kBarFill,3.0f);}
  };
  float y0=mn.y+6.0f;
  AxisRow(y0,true,"LX",st.lx); AxisRow(y0,false,"LY",st.ly);
  AxisRow(y0+row_h,true,"RX",st.rx); AxisRow(y0+row_h,false,"RY",st.ry);
  AxisRow(y0+row_h*2,true,"LT",st.lt); AxisRow(y0+row_h*2,false,"RT",st.rt);
  float by=mn.y+row_h*3+8.0f;
  struct{const char* n;bool v;}btns[]={{"A",st.a},{"B",st.b},{"X",st.x},{"Y",st.y},{"LB",st.lb},{"RB",st.rb},{"U",st.dpad_up},{"D",st.dpad_down},{"L",st.dpad_left},{"R",st.dpad_right}};
  float bx=mn.x+pad;
  for(auto& b:btns){const ImVec2 ns=ImGui::CalcTextSize(b.n); float nw=ns.x+4.0f; if(bx+nw+14>mx.x-pad)break;
    d->AddCircleFilled(ImVec2(bx+4,by+8),5.0f,b.v?kDotOn:kDotOff);
    d->AddText(ImVec2(bx+13,by+1),b.v?kTextScr:kTextScrD,b.n); bx+=nw+22.0f;
  }
}

void DrawStick(const char* label, const char* click_name,
               const ImVec2& center, float radius,
               float* x, float* y, bool* clicked,
               StickMemory* stk, ButtonMemory* btn, bool hold_mode) {
  float pad=12.0f;
  ImGui::SetCursorPos(ImVec2(center.x-radius-pad,center.y-radius-pad)); ImGui::PushID(label);
  ImVec2 bs(radius*2+pad*2,radius*2+pad*2); ImGui::InvisibleButton("stick_area",bs);
  bool active=ImGui::IsItemActive(),down=ImGui::IsMouseDown(ImGuiMouseButton_Left);
  bool dbl=ImGui::IsItemHovered()&&ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
  ImDrawList* d=ImGui::GetWindowDrawList(); ImVec2 wp=ImGui::GetWindowPos();
  ImVec2 sc(wp.x+center.x,wp.y+center.y);
  d->AddCircleFilled(sc,radius,kStickWell); d->AddCircle(sc,radius,kStickRing,54,2.0f);
  float cl=radius*0.82f,cg=radius*0.10f;
  d->AddLine(ImVec2(sc.x-cl,sc.y),ImVec2(sc.x-cg,sc.y),kStickCross,1.5f);
  d->AddLine(ImVec2(sc.x+cg,sc.y),ImVec2(sc.x+cl,sc.y),kStickCross,1.5f);
  d->AddLine(ImVec2(sc.x,sc.y-cl),ImVec2(sc.x,sc.y-cg),kStickCross,1.5f);
  d->AddLine(ImVec2(sc.x,sc.y+cg),ImVec2(sc.x,sc.y+cl),kStickCross,1.5f);
  d->AddCircleFilled(sc,3.0f,IM_COL32(0x50,0x58,0x65,0x50));
  ImVec2 knob(sc.x,sc.y); ImU32 kc=kStickKnob;
  if(stk->locked){*x=stk->lock_x;*y=stk->lock_y;knob=ImVec2(sc.x+(*x)*radius,sc.y-(*y)*radius);kc=kStickActive;}
  else if(active&&down){knob=StickFromMouse(sc,ImGui::GetIO().MousePos,radius,x,y);stk->last_x=*x;stk->last_y=*y;kc=kStickActive;}
  else{*x=0.0f;*y=0.0f;knob=sc;}
  if(dbl){if(stk->locked){stk->locked=false;*x=0.0f;*y=0.0f;knob=sc;kc=kStickKnob;}
    else{stk->locked=true;stk->lock_x=active&&down?*x:stk->last_x;stk->lock_y=active&&down?*y:stk->last_y;
      *x=stk->lock_x;*y=stk->lock_y;knob=ImVec2(sc.x+(*x)*radius,sc.y-(*y)*radius);kc=kStickActive;}}
  d->AddCircleFilled(ImVec2(knob.x+2,knob.y+2),kKnR,IM_COL32(0,0,0,0x50));
  d->AddCircleFilled(knob,kKnR,kc);
  d->AddCircleFilled(ImVec2(knob.x-kKnR*0.28f,knob.y-kKnR*0.28f),kKnR*0.28f,IM_COL32(0xFF,0xFF,0xFF,0x20));
  const ImVec2 ts=ImGui::CalcTextSize(label); float ly=sc.y+radius+9.0f;
  d->AddText(ImVec2(sc.x-ts.x*0.5f,ly),kTextDim,label);
  float cw=30.0f,ch=18.0f;
  float cbx=center.x+ts.x*0.5f+8.0f, cby=center.y+radius+9.0f+ts.y*0.5f-ch*0.5f;
  ImGui::SetCursorPos(ImVec2(cbx,cby)); ImGui::PushID("click");
  ImGui::InvisibleButton("click_btn",ImVec2(cw,ch));
  bool cd=ImGui::IsItemActive()&&ImGui::IsMouseDown(ImGuiMouseButton_Left);
  if(hold_mode&&ImGui::IsItemClicked(ImGuiMouseButton_Left)) btn->latched=!btn->latched;
  *clicked=DigitalPressed(cd,*btn);
  const ImVec2 cmn=ImGui::GetItemRectMin(),cmx=ImGui::GetItemRectMax();
  d->AddRectFilled(cmn,cmx,*clicked?kBtnActive:kDpadFill,ch*0.5f);
  d->AddRect(cmn,cmx,*clicked?kBtnActiveBd:kDpadBorder,ch*0.5f,0,1.2f);
  const ImVec2 cts=ImGui::CalcTextSize(click_name);
  d->AddText(ImVec2(cmn.x+(cw-cts.x)*0.5f,cmn.y+(ch-cts.y)*0.5f),kTextW,click_name);
  ImGui::PopID();
  const char* hint=stk->locked?"Dbl-click: unlock":"Dbl-click: lock axis";
  const ImVec2 hs=ImGui::CalcTextSize(hint);
  d->AddText(ImVec2(sc.x-hs.x*0.5f,ly+ts.y+2.0f),kTextDim,hint);
  if(stk->locked){const char* lt="LOCKED"; const ImVec2 ls=ImGui::CalcTextSize(lt);
    d->AddText(ImVec2(sc.x-ls.x*0.5f,sc.y-radius-24.0f),kTextAcc,lt);}
  ImGui::PopID();
}

void DrawDpad(const ImVec2& center, float arm,
              bool* up, bool* down, bool* left, bool* right,
              ButtonMemory* um, ButtonMemory* dm,
              ButtonMemory* lm, ButtonMemory* rm, bool hold_mode) {
  float h=arm*0.5f; ImDrawList* d=ImGui::GetWindowDrawList();
  struct DB{const char* id;ImVec2 p;int dir;bool* s;};
  DB btns[]={{"dU",ImVec2(center.x-h,center.y-arm-h),0,up},{"dD",ImVec2(center.x-h,center.y+h),1,down},{"dL",ImVec2(center.x-arm-h,center.y-h),2,left},{"dR",ImVec2(center.x+h,center.y-h),3,right}};
  ButtonMemory* mems[]={um,dm,lm,rm};
  for(int i=0;i<4;i++){auto& b=btns[i]; ImGui::SetCursorPos(b.p); ImGui::PushID(b.id);
    ImGui::InvisibleButton("dbtn",ImVec2(arm,arm));
    bool dn=ImGui::IsItemActive()&&ImGui::IsMouseDown(ImGuiMouseButton_Left);
    if(hold_mode&&ImGui::IsItemClicked(ImGuiMouseButton_Left)) mems[i]->latched=!mems[i]->latched;
    *b.s=DigitalPressed(dn,*mems[i]);
    const ImVec2 mn=ImGui::GetItemRectMin(),mx=ImGui::GetItemRectMax(); bool on=*b.s;
    d->AddRectFilled(mn,mx,on?kBtnActive:kDpadFill,kDR);
    d->AddRect(mn,mx,on?kBtnActiveBd:kDpadBorder,kDR,0,1.8f);
    ImVec2 ac((mn.x+mx.x)*0.5f,(mn.y+mx.y)*0.5f);
    DrawArrow(d,ac,arm*0.24f,b.dir,on?kTextW:kDpadArrow);
    ImGui::PopID();
  }
  ImVec2 wp=ImGui::GetWindowPos(); ImVec2 sc(wp.x+center.x,wp.y+center.y);
  ImVec2 cm(sc.x-h,sc.y-h),cM(sc.x+h,sc.y+h); bool any=*up||*down||*left||*right;
  d->AddRectFilled(cm,cM,any?kBtnActive:kDpadFill,kDR);
  d->AddRect(cm,cM,any?kBtnActiveBd:kDpadBorder,kDR,0,1.8f);
}

void DrawFaceButtons(const ImVec2& center, float radius, float spacing,
                     bool* a, bool* b, bool* x, bool* y,
                     ButtonMemory* am, ButtonMemory* bm,
                     ButtonMemory* xm, ButtonMemory* ym, bool hold_mode) {
  ImDrawList* d=ImGui::GetWindowDrawList(); ImVec2 wp=ImGui::GetWindowPos();
  ImVec2 sc(wp.x+center.x,wp.y+center.y);
  d->AddLine(ImVec2(sc.x,sc.y-spacing+radius),ImVec2(sc.x,sc.y+spacing-radius),kDpadBorder,3.5f);
  d->AddLine(ImVec2(sc.x-spacing+radius,sc.y),ImVec2(sc.x+spacing-radius,sc.y),kDpadBorder,3.5f);
  float dd=radius*2; ImVec2 btn(dd+10,dd+10);
  struct FB{const char* l;ImVec2 p;ImU32 c;bool* s;};
  FB fbs[]={{"Y",ImVec2(center.x-radius,center.y-spacing-radius),kFaceY,y},{"A",ImVec2(center.x-radius,center.y+spacing-radius),kFaceA,a},{"X",ImVec2(center.x-spacing-radius,center.y-radius),kFaceX,x},{"B",ImVec2(center.x+spacing-radius,center.y-radius),kFaceB,b}};
  for(auto& fb:fbs){ImGui::SetCursorPos(fb.p);ImGui::PushID(fb.l);ImGui::InvisibleButton("face",btn);
    bool dn=ImGui::IsItemActive()&&ImGui::IsMouseDown(ImGuiMouseButton_Left);
    ButtonMemory* mem=fb.s==a?am:fb.s==b?bm:fb.s==x?xm:ym;
    if(hold_mode&&ImGui::IsItemClicked(ImGuiMouseButton_Left)) mem->latched=!mem->latched;
    *fb.s=DigitalPressed(dn,*mem);
    const ImVec2 mn=ImGui::GetItemRectMin(),mx=ImGui::GetItemRectMax();
    const ImVec2 bc((mn.x+mx.x)*0.5f,(mn.y+mx.y)*0.5f); bool on=*fb.s;
    d->AddCircleFilled(ImVec2(bc.x+2,bc.y+2),radius,IM_COL32(0,0,0,0x40));
    if(on){d->AddCircleFilled(bc,radius,IM_COL32(0xFF,0xFF,0xFF,0xFF));d->AddCircleFilled(bc,radius,fb.c);d->AddCircleFilled(bc,radius*0.5f,IM_COL32(0xFF,0xFF,0xFF,0xFF));}
    else d->AddCircleFilled(bc,radius,fb.c);
    d->AddCircle(bc,radius,IM_COL32(0,0,0,0x30),36,1.5f);
    const ImVec2 ts=ImGui::CalcTextSize(fb.l);
    d->AddText(ImVec2(bc.x-ts.x*0.5f,bc.y-ts.y*0.5f),on?IM_COL32(0x18,0x18,0x18,0xFF):kTextW,fb.l);
    ImGui::PopID();
  }
}

void ResetState(GamepadState* st) { *st = GamepadState{}; }

}  // namespace

int main(int, char**) {
  if(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_TIMER|SDL_INIT_GAMECONTROLLER)!=0){std::fprintf(stderr,"SDL init: %s\n",SDL_GetError());return 1;}
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS,0);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_CORE);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,0);
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER,1);
  SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE,24);
  SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE,8);
  SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE,8);
  SDL_Window* window=SDL_CreateWindow("virtual_gamepad_linux",SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,(int)kW,(int)kH,SDL_WINDOW_OPENGL|SDL_WINDOW_BORDERLESS|SDL_WINDOW_ALWAYS_ON_TOP|SDL_WINDOW_ALLOW_HIGHDPI);
  if(!window){std::fprintf(stderr,"Window: %s\n",SDL_GetError());SDL_Quit();return 1;}
  float bx=((float)kW-kBW)*0.5f, by=((float)kH-kBH)*0.5f;
  SetWindowShape(window,(int)bx,(int)by,(int)kBW,(int)kBH);
  SDL_GLContext gl=SDL_GL_CreateContext(window);
  if(!gl){std::fprintf(stderr,"GL: %s\n",SDL_GetError());SDL_DestroyWindow(window);SDL_Quit();return 1;}
  SDL_GL_MakeCurrent(window,gl); SDL_GL_SetSwapInterval(1);
  glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
  IMGUI_CHECKVERSION(); ImGui::CreateContext();
  ImGuiIO& io=ImGui::GetIO(); io.ConfigFlags|=ImGuiConfigFlags_NavEnableKeyboard;
  ImGui::StyleColorsDark(); ImGuiStyle& stl=ImGui::GetStyle();
  stl.WindowRounding=0; stl.FrameRounding=4; stl.GrabRounding=4;
  stl.ChildRounding=0; stl.ScrollbarSize=0;
  stl.WindowPadding=ImVec2(0,0); stl.WindowBorderSize=0;
  stl.Colors[ImGuiCol_WindowBg]=ImVec4(0,0,0,0);
  ImGui_ImplSDL2_InitForOpenGL(window,gl); ImGui_ImplOpenGL3_Init("#version 130");
  VirtualGamepadDevice device; bool dev_ok=device.Create();
  GamepadState state; UiMemory ui;
  std::string last_error=dev_ok?"":device.error();
  bool done=false;
  while(!done){
    SDL_Event ev;
    while(SDL_PollEvent(&ev)){ImGui_ImplSDL2_ProcessEvent(&ev);
      if(ev.type==SDL_QUIT)done=true;
      if(ev.type==SDL_WINDOWEVENT&&ev.window.event==SDL_WINDOWEVENT_CLOSE)done=true;
      if(ev.type==SDL_KEYDOWN){if(ev.key.keysym.sym==SDLK_ESCAPE)done=true;
        if(ev.key.keysym.sym==SDLK_q&&(SDL_GetModState()&KMOD_CTRL))done=true;}}
    ImGui_ImplOpenGL3_NewFrame(); ImGui_ImplSDL2_NewFrame(window); ImGui::NewFrame();
    float bxx=(io.DisplaySize.x-kBW)*0.5f, byy=(io.DisplaySize.y-kBH)*0.5f;
    ImVec2 body_min(bxx,byy), body_max(bxx+kBW,byy+kBH);
    ImGui::SetNextWindowPos(ImVec2(0,0)); ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("##main",nullptr,ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoBackground|ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImDrawList* draw=ImGui::GetWindowDrawList();
    DrawBody(draw,body_min,body_max);
    {float cb_x=body_max.x-48.0f,cb_y=body_min.y+18.0f; ImGui::SetCursorPos(ImVec2(cb_x,cb_y));
      ImGui::PushID("close");ImGui::InvisibleButton("x",ImVec2(18,18));
      bool hv=ImGui::IsItemHovered(),cl=ImGui::IsItemClicked();
      const ImVec2 cmn=ImGui::GetItemRectMin(),cmx=ImGui::GetItemRectMax();
      float ccx=(cmn.x+cmx.x)*0.5f,ccy=(cmn.y+cmx.y)*0.5f,chh=5.5f;
      draw->AddCircleFilled(ImVec2(ccx,ccy),9.0f,hv?kCloseHvr:kCloseBtn);
      draw->AddLine(ImVec2(ccx-chh,ccy-chh),ImVec2(ccx+chh,ccy+chh),kTextW,2.0f);
      draw->AddLine(ImVec2(ccx+chh,ccy-chh),ImVec2(ccx-chh,ccy+chh),kTextW,2.0f);
      ImGui::PopID();if(cl)done=true;}
    float bw=152.0f,bh=36.0f;
    DrawBumper("LB",ImVec2(body_min.x+32,body_min.y+8),ImVec2(bw,bh),&state.lb,&ui.lb,ui.hold_mode);
    DrawBumper("RB",ImVec2(body_max.x-bw-32,body_min.y+8),ImVec2(bw,bh),&state.rb,&ui.rb,ui.hold_mode);
    float tw=152.0f,th=28.0f;
    DrawTriggerBar("LT",ImVec2(body_min.x+32,body_min.y+48),ImVec2(tw,th),&state.lt);
    DrawTriggerBar("RT",ImVec2(body_max.x-tw-32,body_min.y+48),ImVec2(tw,th),&state.rt);
    float ctx=body_min.x+kBW*0.5f;
    DrawCenterButton("Back",ImVec2(ctx-100,body_min.y+42),ImVec2(58,32),&state.back,&ui.back,ui.hold_mode);
    DrawCenterButton("Guide",ImVec2(ctx-35,body_min.y+42),ImVec2(70,32),&state.guide,&ui.guide,ui.hold_mode);
    DrawCenterButton("Start",ImVec2(ctx+42,body_min.y+42),ImVec2(58,32),&state.start,&ui.start,ui.hold_mode);
    {float sx=body_min.x+(kBW-kSW)*0.5f,sy=body_min.y+95.0f;
      DrawScreen(draw,ImVec2(sx,sy),ImVec2(sx+kSW,sy+kSH),state);}
    DrawStick("Left Stick","LS",ImVec2(body_min.x+kLC,body_min.y+kUY),kStR,&state.lx,&state.ly,&state.ls,&ui.left_stick,&ui.ls,ui.hold_mode);
    DrawStick("Right Stick","RS",ImVec2(body_min.x+kRC,body_min.y+kLY),kStR,&state.rx,&state.ry,&state.rs,&ui.right_stick,&ui.rs,ui.hold_mode);
    ImVec2 dc(body_min.x+kLC,body_min.y+kLY);
    DrawDpad(dc,kDA,&state.dpad_up,&state.dpad_down,&state.dpad_left,&state.dpad_right,&ui.dpad_up,&ui.dpad_down,&ui.dpad_left,&ui.dpad_right,ui.hold_mode);
    {const char* dl="D-Pad";ImVec2 dls=ImGui::CalcTextSize(dl);draw->AddText(ImVec2(dc.x-dls.x*0.5f,dc.y+kDA*1.5f+26),kTextDim,dl);}
    ImVec2 fc(body_min.x+kRC,body_min.y+kUY);
    DrawFaceButtons(fc,kFR,kFS,&state.a,&state.b,&state.x,&state.y,&ui.a,&ui.b,&ui.x,&ui.y,ui.hold_mode);
    {const char* al="ABXY";ImVec2 als=ImGui::CalcTextSize(al);draw->AddText(ImVec2(fc.x-als.x*0.5f,fc.y+kFS+kFR+22),kTextDim,al);}
    float sy=body_max.y-42.0f; ImGui::SetCursorPos(ImVec2(body_min.x+82,sy));
    if(ImGui::SmallButton("Reconnect")){dev_ok=device.Create();last_error=dev_ok?"":device.error();}
    ImGui::SameLine(); if(ImGui::SmallButton("Reset")){ui=UiMemory{};ResetState(&state);if(device.ready())device.SendState(state);}
    ImGui::SameLine(); ImGui::Checkbox("Hold Mode",&ui.hold_mode);
    ImGui::SameLine(0,16); ImGui::TextColored(ImVec4(0.55f,0.58f,0.65f,1.f),"Status:%s  L-Lock:%s  R-Lock:%s",device.ready()?"+":"-",ui.left_stick.locked?"ON":"OFF",ui.right_stick.locked?"ON":"OFF");
    if(!last_error.empty())ImGui::TextColored(ImVec4(1,0.4f,0.4f,1),"Error: %s",last_error.c_str());
    // 右键按住手柄空白区域拖动窗口，左键继续只用于虚拟手柄输入。
    const ImVec2 drag_origin=io.MouseClickedPos[ImGuiMouseButton_Right];
    if(ImGui::IsMouseDragging(ImGuiMouseButton_Right,0.0f)&&
       InGamepadShape(drag_origin,body_min,body_max)&&!ImGui::IsAnyItemHovered()){
      ImVec2 delta=ImGui::GetMouseDragDelta(ImGuiMouseButton_Right);
      int wx,wy;SDL_GetWindowPosition(window,&wx,&wy);
      SDL_SetWindowPosition(window,wx+(int)delta.x,wy+(int)delta.y);
      ImGui::ResetMouseDragDelta(ImGuiMouseButton_Right);
    }
    ImGui::End(); ImGui::Render();
    int dw=0,dh=0; SDL_GL_GetDrawableSize(window,&dw,&dh); glViewport(0,0,dw,dh);
    glClearColor(0,0,0,0); glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData()); SDL_GL_SwapWindow(window);
    if(device.ready()&&!device.SendState(state))last_error=device.error();
  }
  device.Destroy(); ImGui_ImplOpenGL3_Shutdown(); ImGui_ImplSDL2_Shutdown();
  ImGui::DestroyContext(); SDL_GL_DeleteContext(gl); SDL_DestroyWindow(window); SDL_Quit();
  return 0;
}
