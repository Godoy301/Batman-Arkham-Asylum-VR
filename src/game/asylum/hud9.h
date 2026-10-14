#pragma once
#include <d3d9.h>
#include <cstdint>
namespace bvr::aa::hud9 {
bool install(IDirect3DDevice9*);
void begin_eye(int eye,uint32_t generation);
void end_present();
void end_render(); // stop scene timing BEFORE colour/XR/DLSS transport
void mirror(); // restore the captured HUD to the desktop after XR capture
void reset();
IDirect3DSurface9* sheet(); // borrowed, completed LEFT sheet only
uint32_t generation();
int eye();
void status();
void draw_ui();
bool fast_tracking();void set_fast_tracking(bool,bool persist=true);
bool fixture_tracking(IDirect3DDevice9*,D3DPRIMITIVETYPE,const void*,UINT stride,IDirect3DSurface9* privateTarget);
bool fixture_budget();
bool fixture_alpha(IDirect3DDevice9*,const wchar_t* directory);
}
