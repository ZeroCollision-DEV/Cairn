#pragma once
// Edge detection for the in-game menu hotkey (Right Shift). No Windows or ImGui dependency, so it is unit-tested
// in tests/portable_tests.cpp.
//
// One call per rendered frame with the key state and whether the hotkey may act right now. The menu toggles
// only on the frame the key goes from up to down while allowed. Holding the key never repeats. A key that goes
// down while the hotkey is not allowed (another window has focus, a text field is being typed in) is
// remembered as "already down", so it does not fire later when the key is still held and focus returns.

struct MenuHotkey
{
    bool prevDown = false;

    bool update(bool keyDown, bool allowed)
    {
        const bool fire = keyDown && !prevDown && allowed;
        prevDown = keyDown;
        return fire;
    }
    void reset() { prevDown = false; }
};

// Whether the hotkey may act. `menuOpen`: the menu is visible. `textInputActive`: a menu text field wants
// keyboard input (only relevant while the menu is open, otherwise a stale flag could block reopening).
inline bool menuHotkeyAllowed(bool gameWindowFocused, bool menuOpen, bool textInputActive)
{
    if (!gameWindowFocused) return false;
    if (menuOpen && textInputActive) return false;
    return true;
}
