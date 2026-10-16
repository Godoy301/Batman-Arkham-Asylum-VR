#pragma once
#include "native_stereo.h"
#include <d3d11.h>
namespace bvr::aa::dlss {
void init_settings();
void set_mode(int,bool persist=true);int mode();bool active();
void status_line(char*,unsigned);
bool jitter(unsigned generation,unsigned w,unsigned h,float&,float&);
// The scene is already mapped to its actual XR lens, with no HUD.
bool process(ID3D11DeviceContext*,const native::Camera&,ID3D11Texture2D* scene,
             ID3D11ShaderResourceView* actualDepth,ID3D11Texture2D* output,bool deferLeftAllowed=true);
bool rollback_left();
bool finish_left(ID3D11DeviceContext*,ID3D11Texture2D*);
bool left_pending();bool overlap();void set_overlap(bool,bool persist=true);
void reset();void status();void draw_ui();
void prewarm(ID3D11Device*); // start the helper in the background as soon as the game's device exists (never blocks)
void shutdown();
bool fixture(ID3D11DeviceContext*,ID3D11Texture2D*,ID3D11ShaderResourceView*,const wchar_t* helper);
}
