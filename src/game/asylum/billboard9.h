#pragma once
#include <d3d9.h>
#include <cstdint>
#include "menu_state.h"
struct ID3D11DeviceContext;
namespace bvr::aa::billboard9 {
bool install(IDirect3DDevice9*);
void publish(uint32_t slot,const float* gameView,const float* eyeView,const float gameMinusEyeWorld[3],menu_state::Mode);
void begin_render(uint32_t slot,bool nativeView);
void invalidate_shader();
void reset();
bool enabled();
void set_enabled(bool,bool persist=true);
void draw_ui();
void status();
void status_line(char*,unsigned);
// Each draw temporarily substitutes only named particle-facing constants.
// The game's logical constants, projection and lighting are restored on exit.
struct Scope {
 IDirect3DDevice9* device{};int right{-1},up{-1},position{-1};
 float saved[3][4]{};bool changed{};
 explicit Scope(IDirect3DDevice9*,bool nativeDraw=true);
 ~Scope();
};
bool fixture(IDirect3DDevice9*,const wchar_t* originalShaderPath);
}
