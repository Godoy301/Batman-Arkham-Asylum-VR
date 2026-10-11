#pragma once
struct IDXGISwapChain;
namespace bvr::aa::scene {
bool install();
void on_present(IDXGISwapChain*);
void draw_ui();
void install_render_anchors();
void render_anchor_status();
}
