#include "game/igame_adapter.h"
#include "scene.h"
#include "d3d9_bridge.h"
#include "core/framework/command.h"
#include "core/input/dinput_block.h"
#include "core/util/log.h"
#include "core/vr/openxr_runtime.h"
#include <imgui.h>
#include <cstring>
#include <cwchar>
namespace bvr::aa::scene {
bool install() {
    command::enable_present_pump();
    input::dinput_block::install();
    install_render_anchors();
    vr::set_screen_room(true);
    const auto* x=wcsstr(GetCommandLineW(),L"-AAOutputX=");
    const auto* y=wcsstr(GetCommandLineW(),L"-AAOutputY=");
    if(x && y) {
        const auto w=wcstoul(x+11,nullptr,10),h=wcstoul(y+11,nullptr,10);
        if(w>=512 && h>=512 && w<=8192 && h<=8192)vr::set_output_size(w,h);
    }
    BVR_LOG("[aa] Origins O1H core active; Asylum native camera seams are not yet verified");
    return true;
}
void on_present(IDXGISwapChain*) {
    static ULONGLONG next=0;const auto now=GetTickCount64();
    if(now>=next) {next=now+5000;render_anchor_status();}
}
void draw_ui() {
    ImGui::TextUnformatted("Arkham Asylum VR — A0 graphics bridge");
    ImGui::TextWrapped("Game-frame output and controls are available. Native 3D camera integration is still being established.");
    bridge::draw_ui();
}
}
namespace bvr::aa {
class AsylumAdapter final : public game::IGameAdapter {
public:
    uint32_t capabilities() const override { return 0; }
    bool init(const pattern_scan::ProcessImage&) override { return scene::install(); }
    void setFov(float) override {}
    void drawDebugUi() override { scene::draw_ui(); }
    bool handleCommand(const char* cmd,const char* args) override {
        if(_stricmp(cmd,"aa") || _stricmp(args,"status")) return false;
        BVR_LOG("[aa] A0 graphics bridge; native camera/depth integration pending");scene::render_anchor_status();return true;
    }
};
game::IGameAdapter* create_adapter() {static AsylumAdapter a;return &a;}
}
