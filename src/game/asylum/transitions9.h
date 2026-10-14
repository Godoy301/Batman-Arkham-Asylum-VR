#pragma once
#include <d3d9.h>
#include <cstdint>
#include <vector>
namespace bvr::aa::transitions9 {
struct Draw {D3DPRIMITIVETYPE type{};UINT count{},start{},min{},num{},indexStart{},stride{};INT base{};const void* vertices{};const void* indices{};D3DFORMAT format{D3DFMT_INDEX16};bool indexed{};};
struct UiVertex {float x{},y{},colour[4]{};};
bool inspect_ui(IDirect3DDevice9*,const Draw&,std::vector<UiVertex>&,std::vector<unsigned>&);
bool install(IDirect3DDevice9*);
void vertex_created(IDirect3DVertexShader9*);
void begin_eye(int,uint32_t);
void reset();
IDirect3DSurface9* sheet();
uint32_t generation();
// Actual authored fade envelope for this render pair; zero for stale slots.
float fade_alpha(uint32_t generation);
void fallback(IDirect3DSurface9*);
void status();void status_line(char*,size_t);void draw_ui();
bool fixture(IDirect3DDevice9*,const wchar_t* directory,bool (*completed)(IDirect3DSurface9*)=nullptr);
struct Scope {
 IDirect3DDevice9* device{};IDirect3DSurface9* rt{},*ds{};D3DVIEWPORT9 viewport{};RECT scissor{};
 DWORD states[8]{};float transform[16]{};int reg{-1};bool changed{},skip{};
 Scope(IDirect3DDevice9*,const Draw&,bool uiLane,bool skippedRight);
 ~Scope();
};
}
