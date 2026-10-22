#pragma once
#include "imgui.h"
#include <string>

// Optional profile shown in the bottom-left of the menu.
//
// Current provider: a LOCAL profile read from %APPDATA%\Cairn\profile.ini
// (a display name and an optional avatar image the user chooses). Nothing is
// fetched from the network and nothing is faked: if the file has no display
// name the menu shows an honest "Not connected" placeholder.
//
// A Discord provider (local IPC / OAuth) is NOT implemented. When one exists it
// only needs to fill Profile::Info from refresh(); the menu code does not change.
namespace Profile
{
    struct Info
    {
        bool        configured = false;   // true when a display name is set
        std::string displayName;
        std::string source;               // e.g. "Local profile"
        bool        hasAvatar = false;    // avatar decoded successfully
    };

    // Reads the profile file and decodes the avatar. Call off the render thread
    // (it uses COM/WIC); safe to call again later to pick up edits.
    void refresh();

    Info info();

    // Render thread only. Uploads the decoded avatar to a GL texture on first use.
    // Returns 0 when there is no avatar.
    ImTextureID avatarTexture();

    std::string filePath();
}
