#pragma once
#include <d3d9.h>
#include <cstdint>
// Eye-relative object positions the engine computed ONCE (first/left view) and
// reused for the right eye. UE3 stores LocalToWorld translated by the view
// origin; a right-eye draw whose translation still equals a left-eye one gets
// the eye shift added for that draw only (Asylum shotgun shells, A2n).
namespace bvr::aa::xform9 {
void begin_eye(int eye,uint32_t generation,UINT width,UINT height);
void vertex_created(IDirect3DVertexShader9*);
struct Scope {
 IDirect3DDevice9* device{};int reg{-1};float saved[4]{};bool changed{};
 Scope(IDirect3DDevice9*,bool sceneDraw);
 ~Scope();
};
bool command(const char* args);
void status_line(char*,size_t);
}
