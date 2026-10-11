#pragma once
// Multi-game dispatch: which adapter runs is decided here, by host exe name.
// Host detection is split from adapter construction because framework init
// needs the per-game data-dir subfolder BEFORE the log exists, so
// detect_host_game()/host_data_subdir() must never log.
// (Forked from BioShock VR, MIT - see LICENSE-bioshock-vr.txt.)

namespace bvr::game {

enum class HostGame {
    ArkhamAsylum, // ShippingPC-BmGame.exe (Asylum GOTY, Rocksteady UE3, x86)
    Unknown,
};

// Cached exe-basename match. Safe to call before log::init - never logs.
HostGame detect_host_game();

// Data-dir subfolder under %LOCALAPPDATA%\Batman_Asylum for this host. Empty = flat
// layout. The exe-name knowledge lives only in this module - core stays
// game-agnostic.
const wchar_t* host_data_subdir();

} // namespace bvr::game
