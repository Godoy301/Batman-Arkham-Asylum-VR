#pragma once
struct IDXGISwapChain;
namespace bvr::aa::scene {
bool install();
void on_present(IDXGISwapChain*);
void draw_ui();
void install_render_anchors();
void render_anchor_status();
bool ordinary_render_seen();
// The panel "Show test options" switch (off at every start): test recorders and diagnostics.
bool test_options();
}
