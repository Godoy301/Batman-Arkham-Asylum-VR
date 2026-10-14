#pragma once
#include <d3d9.h>
namespace bvr::aa::managed9 {
// Owned classic facade over a native Ex device. The facade preserves managed
// CPU contents; Ex is kept private so the game retains its original API contract.
IDirect3DDevice9* wrap(IDirect3DDevice9Ex*,IDirect3D9* factory=nullptr);
IDirect3DDevice9Ex* native_device(IDirect3DDevice9*);
IDirect3DTexture9* depth_texture(IDirect3DSurface9*);
IDirect3DSurface9* native_surface(IDirect3DSurface9*);
// Read existing owned managed CPU bytes; never locks or reads GPU resources.
bool copy_buffer(IDirect3DDevice9*,IUnknown*,UINT offset,UINT bytes,void* destination);
}
