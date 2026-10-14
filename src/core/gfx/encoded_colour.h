#pragma once
#include <dxgiformat.h>
namespace bvr::gfx {
struct EncodedViewFormats {DXGI_FORMAT resource,view;};
// A finished desktop/movie image already contains display-encoded bytes.
// Decode while sampling if the destination RTV will encode again. A typeless
// copy permits that view for both RGBA and the native9 bridge's BGRA families.
inline EncodedViewFormats encoded_screen_formats(DXGI_FORMAT source,DXGI_FORMAT target){
 const bool srgb=target==DXGI_FORMAT_R8G8B8A8_UNORM_SRGB||target==DXGI_FORMAT_B8G8R8A8_UNORM_SRGB||target==DXGI_FORMAT_B8G8R8X8_UNORM_SRGB;
 switch(source){
 case DXGI_FORMAT_R8G8B8A8_TYPELESS:case DXGI_FORMAT_R8G8B8A8_UNORM:case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
  return {DXGI_FORMAT_R8G8B8A8_TYPELESS,srgb?DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:DXGI_FORMAT_R8G8B8A8_UNORM};
 case DXGI_FORMAT_B8G8R8A8_TYPELESS:case DXGI_FORMAT_B8G8R8A8_UNORM:case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
  return {DXGI_FORMAT_B8G8R8A8_TYPELESS,srgb?DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:DXGI_FORMAT_B8G8R8A8_UNORM};
 case DXGI_FORMAT_B8G8R8X8_TYPELESS:case DXGI_FORMAT_B8G8R8X8_UNORM:case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
  return {DXGI_FORMAT_B8G8R8X8_TYPELESS,srgb?DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:DXGI_FORMAT_B8G8R8X8_UNORM};
 default:return {source,source};
 }
}
}
