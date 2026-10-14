#pragma once
#include <d3d11.h>
#include <cstdint>
namespace bvr::stereo_history {
// Render thread, while the final eye image is acquired. Native pairs share a
// generation; AFW copies both eyes before either image is released.
void eye(ID3D11DeviceContext*,int,ID3D11Texture2D*,uint32_t generation,bool afw,bool warped=true);
void replace_left(ID3D11DeviceContext*,ID3D11Texture2D*,uint32_t generation);
void poll(ID3D11DeviceContext*);
void reset_gpu();
void request_save();
// Called on every save request (panel button, F2, command), e.g. to arm extra recorders.
void set_save_listener(void(*listener)());
void draw_ui();
bool fixture(ID3D11DeviceContext*);
}
