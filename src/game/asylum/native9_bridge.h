#pragma once
#include <d3d9.h>
#include <d3d11.h>
namespace bvr::aa::native9 {
bool adopt(IDirect3DDevice9*);
ID3D11Device* device();
ID3D11DeviceContext* context();
// Shared images have no keyed mutex: transfer completes the 9 producer and
// waits for a previous 11 reader before reusing a slot. finish_reads marks
// actual consumer work, after all VR/core reads have been enqueued.
ID3D11Texture2D* colour(IDirect3DSurface9*,unsigned slot);
bool depth(IDirect3DSurface9*,ID3D11Texture2D**,ID3D11ShaderResourceView**);
bool finish_reads();
bool drain();
void reset();
void shutdown(); // Offline harness cleanup, before loader-lock teardown.
}
