#pragma once
#include <d3d9.h>
#include <cstdint>
// Debug toolkit: eye-same draw finder (scene draws whose vertex constants are
// identical in both eyes = no stereo depth). Armed by F2 or "aa eyesame".
namespace bvr::aa::eyesame9 {
void request();
void begin_eye(int eye,uint32_t generation);
bool active();
void scene_draw(IDirect3DDevice9*,UINT primitives);
}
