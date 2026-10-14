#pragma once
#include <windows.h>
namespace bvr::aa::window {
bool install();
void bind(HWND);
void draw_ui();
void status();
void observe_source(unsigned,unsigned);
void observe_crop(float);
bool save_scale(float);
bool fixture();
}
