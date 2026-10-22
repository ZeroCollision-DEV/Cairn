// Portable tests: no Windows, JNI or ImGui needed.
//   g++ -std=c++20 -Isrc tests/portable_tests.cpp -o portable_tests && ./portable_tests
// or enable -DCAIRN_BUILD_TESTS=ON in CMake and run the `portable_tests` target.
#include "shared/Ini.h"
#include "modules/Module.h"
#include "injector/TargetMatch.h"
#include "core/MenuHotkey.h"
#include <cstdio>
#include <cstdlib>

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); g_fail++; } } while (0)

struct TestModule : Module
{
    TestModule() : Module("T", "d", Category::Misc)
    {
        addFloat("F", 1.0f, 0.0f, 2.0f);
        addChoice("C", {"a", "b", "c"}, 9);   // default index out of range -> clamped
        addKey("Keybind", 0);
    }
};
struct Unfinished : Module
{
    Unfinished() : Module("U", "d", Category::Misc) { setMaturity(Maturity::Unfinished, "x"); }
};

int main()
{
    // INI parsing: sections are lower-cased, key case kept, junk lines ignored.
    auto m = ini::parse("; c\n[UI]\nsounds = 0\nvolume=abc\n[modules]\nKillAura.Range=4.5\nbad line\n=x\n");
    CHECK(ini::get(m, "ui.sounds") == "0");
    CHECK(!ini::getBool(m, "ui.sounds", true));
    CHECK(ini::getFloat(m, "ui.volume", 0.6f) == 0.6f);          // malformed -> default
    CHECK(ini::get(m, "modules.KillAura.Range") == "4.5");
    CHECK(ini::get(m, "missing.key", "dflt") == "dflt");
    CHECK(ini::parse("").empty());

    // Module settings API
    TestModule t;
    CHECK(t.settings().size() == 3);
    CHECK(t.getSetting("C")->intVal == 2);                       // clamped to last option
    CHECK(t.getFloat("F") == 1.0f);
    CHECK(t.getSetting("nope") == nullptr);
    CHECK(!t.isEnabled());
    t.setEnabled(true);  CHECK(t.isEnabled());
    t.toggle();          CHECK(!t.isEnabled());

    // Unfinished modules refuse to enable
    Unfinished u;
    CHECK(!u.canEnable());
    u.setEnabled(true);  CHECK(!u.isEnabled());
    u.toggle();          CHECK(!u.isEnabled());


    // ── Minecraft process identification (src/injector/TargetMatch.h) ───────────────────────
    {
        using namespace inj;
        const std::string modrinth =
            "\"C:\\Users\\u\\AppData\\Roaming\\ModrinthApp\\meta\\java_versions\\zulu21\\bin\\javaw.exe\" -Xmx2G "
            "-Dminecraft.launcher.brand=modrinth -cp libs.jar net.fabricmc.loader.impl.launch.knot.KnotClient "
            "--username Steve --version fabric-loader-0.16.10-1.21.11 --gameDir \"C:\\Users\\u\\AppData\\Roaming\\ModrinthApp\\profiles\\My Pack\" "
            "--assetsDir assets --assetIndex 17 --accessToken SECRETTOKEN";
        const CmdInfo cm = parseCommandLine(modrinth);
        CHECK(cm.known && cm.minecraft && !cm.launcher);
        CHECK(cm.version == "1.21.11");
        CHECK(cm.launcherBrand == "modrinth");

        const CmdInfo off = parseCommandLine(
            "\"C:\\mc\\runtime\\bin\\javaw.exe\" -cp x net.minecraft.client.main.Main --version 1.21.11 --gameDir g --assetIndex 17");
        CHECK(off.minecraft && off.version == "1.21.11");

        const CmdInfo forge = parseCommandLine("javaw.exe cpw.mods.bootstraplauncher.BootstrapLauncher --launchTarget forgeclient --version 1.21.11-forge-61.0.0");
        CHECK(forge.minecraft && forge.version == "1.21.11");

        const CmdInfo oldLauncher = parseCommandLine("javaw.exe -cp launcher.jar net.minecraft.launcher.Main");
        CHECK(oldLauncher.launcher && !oldLauncher.minecraft);

        const CmdInfo ide = parseCommandLine("javaw.exe -jar SomeTool.jar --version 2.0");
        CHECK(ide.known && !ide.minecraft && !ide.launcher && ide.version.empty());
        CHECK(!parseCommandLine("").known);
        CHECK(detail::mcVersionFromId("fabric-loader-0.17.2-1.21.11") == "1.21.11");
        CHECK(detail::mcVersionFromId("quilt-loader-0.30.0-1.21.11") == "1.21.11");
        CHECK(detail::mcVersionFromId("0.16.10").empty());

        CHECK(versionFromTitle("Minecraft* 1.21.11 - Singleplayer") == "1.21.11");
        CHECK(versionFromTitle("Minecraft 1.21.11 - Multiplayer (3rd-party Server)") == "1.21.11");
        CHECK(versionFromTitle("Minecraft").empty());
        CHECK(versionFromTitle("Minecraft Launcher").empty());
        CHECK(versionFromTitle("My Pack").empty());

        auto ev = [](std::vector<WindowInfo> w, const CmdInfo& c) { Evidence e; e.windows = std::move(w); e.cmd = c; return e; };

        // Official launcher / Modrinth game: GLFW window + Minecraft command line.
        Match m = classifyProcess(ev({{"Minecraft* 1.21.11 - Singleplayer", "GLFW30"}}, cm));
        CHECK(m.kind == TargetKind::Game && m.version == "1.21.11" && m.versionSource == "command line");
        CHECK(m.how == "command line and game window");

        // Custom Modrinth instance / window title: still the game, title is kept as shown.
        m = classifyProcess(ev({{"My Pack | Survival", "GLFW30"}}, cm));
        CHECK(m.kind == TargetKind::Game && m.title == "My Pack | Survival");
        // A tool window next to the game window does not hide it.
        m = classifyProcess(ev({{"Some overlay", "SunAwtFrame"}, {"Custom Title", "GLFW30"}}, cm));
        CHECK(m.kind == TargetKind::Game && m.title == "Custom Title");

        // Custom title and no readable command line: cannot be confirmed -> not offered.
        m = classifyProcess(ev({{"My Pack | Survival", "GLFW30"}}, CmdInfo{}));
        CHECK(m.kind == TargetKind::Other && !m.reason.empty());
        // Same window but the title says Minecraft: accepted on window evidence only, and says so.
        m = classifyProcess(ev({{"Minecraft* 1.21.11", "GLFW30"}}, CmdInfo{}));
        CHECK(m.kind == TargetKind::Game && m.how == "game window and title only" && m.version == "1.21.11" && m.versionSource == "window title");

        // A "Minecraft" title without a game window (dedicated server GUI, tools) is not an instance.
        m = classifyProcess(ev({{"Minecraft server", "SunAwtFrame"}}, parseCommandLine("javaw.exe -jar server.jar nogui")));
        CHECK(m.kind == TargetKind::Other);
        // Legacy Java launcher window.
        m = classifyProcess(ev({{"Minecraft Launcher", "SunAwtFrame"}}, CmdInfo{}));
        CHECK(m.kind == TargetKind::Launcher);
        m = classifyProcess(ev({{"Minecraft* 1.21.11", "GLFW30"}}, oldLauncher));
        CHECK(m.kind == TargetKind::Launcher);
        // Started like Minecraft, no window yet.
        m = classifyProcess(ev({}, cm));
        CHECK(m.kind == TargetKind::Starting && m.version == "1.21.11");
        // Unrelated Java processes.
        m = classifyProcess(ev({}, ide));
        CHECK(m.kind == TargetKind::Other);
        m = classifyProcess(ev({{"Some Game", "GLFW30"}}, ide));
        CHECK(m.kind == TargetKind::Other);
        m = classifyProcess(ev({{"IntelliJ IDEA", "SunAwtFrame"}}, ide));
        CHECK(m.kind == TargetKind::Other);
        // Version falls back to the title only when the command line states none.
        CmdInfo noVer = cm; noVer.version.clear();
        m = classifyProcess(ev({{"Minecraft* 1.21.11 - Singleplayer", "GLFW30"}}, noVer));
        CHECK(m.version == "1.21.11" && m.versionSource == "window title");
        m = classifyProcess(ev({{"My Pack", "GLFW30"}}, noVer));
        CHECK(m.kind == TargetKind::Game && m.version.empty());
        // Configurable title text.
        Evidence custom = ev({{"Zeta Craft", "GLFW30"}}, CmdInfo{});
        custom.titleNeedle = "zeta";
        CHECK(classifyProcess(custom).kind == TargetKind::Game);
    }

    // ── Right Shift menu hotkey (src/core/MenuHotkey.h) ──────────────────────────────────────
    {
        MenuHotkey hk;
        CHECK(!hk.update(false, true));
        CHECK(hk.update(true, true));                 // key-down edge toggles
        CHECK(!hk.update(true, true));                // held: no repeat
        CHECK(!hk.update(true, true));
        CHECK(!hk.update(false, true));               // release does nothing
        CHECK(hk.update(true, true));                 // next press toggles again
        hk.update(false, true);

        // Pressed while another window has focus: ignored, and not fired later while still held.
        CHECK(!hk.update(true, false));
        CHECK(!hk.update(true, true));
        CHECK(!hk.update(false, true));
        CHECK(hk.update(true, true));

        CHECK(menuHotkeyAllowed(true, false, false));
        CHECK(menuHotkeyAllowed(true, true, false));
        CHECK(!menuHotkeyAllowed(false, false, false));   // game not in the foreground
        CHECK(!menuHotkeyAllowed(true, true, true));      // typing in a menu text field
        CHECK(menuHotkeyAllowed(true, false, true));      // stale text-input flag must not block opening the menu
    }

    std::printf(g_fail ? "portable tests FAILED (%d)\n" : "portable tests passed\n", g_fail);
    return g_fail ? 1 : 0;
}
