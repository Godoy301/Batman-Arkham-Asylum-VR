#pragma once
#include <d3d9.h>
#include <cstdint>
// Debug toolkit: hide each kind of 3D scene draw in turn, then save the replay
// ("which shader draws this broken thing?"). Panel TESTS button or "aa fxstep".
namespace bvr::aa::fxstep9 {
void request();
bool running();
void begin_eye(int eye,uint32_t generation,UINT width,UINT height);
// True when this scene draw belongs to the kind hidden in the current step.
bool hide(IDirect3DDevice9*,bool sceneDraw,UINT primitives);
}
