#pragma once
#include <d3d9.h>
struct ID3D11ShaderResourceView;
namespace bvr::aa::bridge {
bool install();
void draw_ui();
void status();
void init_hud_settings();
void draw_hud_ui();
void recenter_hud();
bool save_hud(const wchar_t* path);
bool save_depth(const wchar_t* path);
bool depth_required();
bool depth_ready();
void request_depth_capture(bool enabled);
bool capture_scene_depth(IDirect3DSurface9*,int eye,unsigned generation);
void invalidate_depth();
ID3D11ShaderResourceView* afw_depth();
}
