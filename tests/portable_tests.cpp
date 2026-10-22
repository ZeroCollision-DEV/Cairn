// Portable tests: no Windows, JNI or ImGui needed.
//   g++ -std=c++20 -Isrc tests/portable_tests.cpp -o portable_tests && ./portable_tests
// or enable -DCAIRN_BUILD_TESTS=ON in CMake and run the `portable_tests` target.
#include "shared/Ini.h"
#include "modules/Module.h"
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

    std::printf(g_fail ? "portable tests FAILED (%d)\n" : "portable tests passed\n", g_fail);
    return g_fail ? 1 : 0;
}
