#pragma once
#include "pipeline_cache.h"
namespace bvr::aa::dynamic_pipeline {
struct Stats{bool enabled{};uint64_t variants{},binds{},staticFallbacks{},blobFallbacks{};unsigned liveVariants{},liveTemplates{};};
bool install(ID3D12Device*,pipeline_cache::Factory);
HRESULT create(ID3D12Device*,const D3D12_GRAPHICS_PIPELINE_STATE_DESC*,REFIID,void**,pipeline_cache::Factory);
Stats stats();
bool fixture(ID3D12Device*,ID3D12CommandQueue*,pipeline_cache::Factory);
void clear_templates();
}
