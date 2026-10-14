#pragma once
#include <d3d9.h>
namespace bvr::aa::detail9 {
bool install(IDirect3DDevice9*);
void begin_scene(unsigned sourceW,unsigned inputW);
void input_width(unsigned);
void end_scene();
void draw_ui();
bool fixture(IDirect3DDevice9*);
}
