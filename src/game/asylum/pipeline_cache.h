#pragma once
#include <d3d12.h>
#include <cstdint>
namespace bvr::aa::pipeline_cache {
using Factory=HRESULT(WINAPI*)(ID3D12Device*,const D3D12_GRAPHICS_PIPELINE_STATE_DESC*,REFIID,void**);
struct Stats {uint64_t requests{},hits{},created{},bypassed{},evicted{},keyBytes{},nearSamples{},changed[8]{},depthBiasChanges{},slopeBiasChanges{},clampChanges{};unsigned live{};};
HRESULT create(ID3D12Device*,const D3D12_GRAPHICS_PIPELINE_STATE_DESC*,REFIID,void**,Factory);
Stats stats();
void clear();
bool fixture(ID3D12Device*,ID3D12CommandQueue*,Factory);
}
