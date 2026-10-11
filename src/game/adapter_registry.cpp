#include "game/adapter_registry.h"
#include "game/igame_adapter.h"
#include "core/hooks/pattern_scan.h"
#include "core/util/log.h"
#include <windows.h>
namespace bvr::aa { bvr::game::IGameAdapter* create_adapter(); }
namespace bvr::game {
namespace { IGameAdapter* current=nullptr; }
HostGame detect_host_game() {
    wchar_t path[MAX_PATH]{};GetModuleFileNameW(nullptr,path,MAX_PATH);
    const auto* name=wcsrchr(path,L'\\');
    return _wcsicmp(name ? name+1:path,L"ShippingPC-BmGame.exe") ? HostGame::Unknown:HostGame::ArkhamAsylum;
}
const wchar_t* host_data_subdir() {return L"";}
void init_adapter() {
    if(detect_host_game()==HostGame::Unknown) return;
    current=aa::create_adapter();
    pattern_scan::ProcessImage image{};
    if(pattern_scan::capture_main_module(image)) current->init(image);
}
IGameAdapter* adapter() { return current; }
}
