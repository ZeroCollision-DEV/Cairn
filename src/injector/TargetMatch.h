#pragma once
// Pure (no Windows, no I/O) logic that decides whether a Java process is a playable Minecraft client.
// InjectorCore gathers the evidence (windows of the process, its command line) and calls classifyProcess();
// tests/portable_tests.cpp exercises the same functions without a running game.
//
// Evidence used, strongest first:
//   - the process was started like Minecraft (main class / launch target / --assetIndex+--gameDir)
//   - it owns a visible GLFW game window (window class "GLFW30", which Minecraft 1.13+ creates through LWJGL 3)
//   - a window title that contains the configured text ("Minecraft")
// A title alone is never enough: server GUIs, launchers and tools also have "Minecraft" in their titles.
//
// Privacy: a Minecraft command line contains the session access token. parseCommandLine() keeps only derived
// facts (flags and a version), never the text itself.

#include <cctype>
#include <string>
#include <vector>

namespace inj
{
    enum class TargetKind
    {
        Game,       // a running game with its window: can be selected
        Starting,   // started like Minecraft but no game window yet
        Launcher,   // a launcher, not the game
        Other,      // some other Java process or window: never offered as an instance
    };

    struct WindowInfo
    {
        std::string title;                  // UTF-8
        std::string className;              // e.g. "GLFW30"
    };

    // What the command line says, reduced to facts.
    struct CmdInfo
    {
        bool known = false;                 // false when the command line could not be read
        bool minecraft = false;             // started like a Minecraft client
        bool launcher = false;              // started like the (legacy Java) Minecraft launcher
        std::string version;                // Minecraft version from "--version", e.g. "1.21.11"
        std::string launcherBrand;          // from -Dminecraft.launcher.brand=
    };

    struct Evidence
    {
        std::vector<WindowInfo> windows;    // visible top-level windows owned by the process
        CmdInfo cmd;
        std::string titleNeedle = "minecraft";   // text a title must contain to count as Minecraft evidence
    };

    struct Match
    {
        TargetKind kind = TargetKind::Other;
        std::string title;                  // window title to show (the game window's when there is one)
        std::string version;                // empty when not reliably known
        std::string versionSource;          // "command line" or "window title"
        std::string launcherBrand;
        std::string how;                    // what identified the game ("command line + game window", ...)
        std::string reason;                 // why the process is not a usable game (empty for Game)
    };

    namespace detail
    {
        inline std::string lower(std::string s)
        {
            for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return s;
        }
        inline bool contains(const std::string& hay, const std::string& needle)
        {
            return needle.empty() || hay.find(needle) != std::string::npos;
        }
        inline bool isDigit(char c) { return c >= '0' && c <= '9'; }

        // Splits a Windows command line on unquoted whitespace; quotes are removed.
        inline std::vector<std::string> tokens(const std::string& cmd)
        {
            std::vector<std::string> out;
            std::string cur;
            bool inQ = false, have = false;
            for (char c : cmd)
            {
                if (c == '"') { inQ = !inQ; have = true; continue; }
                if (!inQ && (c == ' ' || c == '\t')) { if (have) { out.push_back(cur); cur.clear(); have = false; } continue; }
                cur.push_back(c);
                have = true;
            }
            if (have) out.push_back(cur);
            return out;
        }

        // "fabric-loader-0.16.10-1.21.11" -> "1.21.11"; "1.21.11-forge-61.0.0" -> "1.21.11"; "" if no 1.x token.
        inline std::string mcVersionFromId(const std::string& id)
        {
            std::string best, cur;
            auto flush = [&]()
            {
                // dotted numeric token starting with "1." (Minecraft's 1.x numbering)
                if (cur.size() >= 3 && cur[0] == '1' && cur[1] == '.')
                {
                    bool ok = true, digitAfterDot = false;
                    for (size_t i = 0; i < cur.size(); i++)
                    {
                        if (cur[i] == '.') { if (i + 1 >= cur.size() || !isDigit(cur[i + 1])) ok = false; }
                        else if (!isDigit(cur[i])) ok = false;
                        else if (i > 0) digitAfterDot = true;
                    }
                    if (ok && digitAfterDot) best = cur;
                }
                cur.clear();
            };
            for (char c : id)
            {
                if (c == '-' || c == '_' || c == '+' || c == ' ') flush();
                else cur.push_back(c);
            }
            flush();
            return best;
        }
    }

    // Window title such as "Minecraft* 1.21.11 - Singleplayer" -> "1.21.11". Empty when the title has no version
    // right after the word "Minecraft". Titles are changed freely by launchers and mods, so treat as a hint.
    inline std::string versionFromTitle(const std::string& title)
    {
        const std::string low = detail::lower(title);
        size_t i = low.find("minecraft");
        if (i == std::string::npos) return {};
        i += 9;
        while (i < title.size() && (title[i] == '*' || title[i] == ' ')) i++;
        size_t j = i;
        bool dot = false;
        while (j < title.size() && (detail::isDigit(title[j]) || title[j] == '.'))
        {
            if (title[j] == '.') dot = true;
            j++;
        }
        if (j == i || !dot || !detail::isDigit(title[i])) return {};
        std::string v = title.substr(i, j - i);
        while (!v.empty() && v.back() == '.') v.pop_back();
        return v;
    }

    // Reads only what is needed from a process command line. `text` is not stored anywhere.
    inline CmdInfo parseCommandLine(const std::string& text)
    {
        CmdInfo r;
        if (text.empty()) return r;
        r.known = true;
        const std::string low = detail::lower(text);
        const std::vector<std::string> tok = detail::tokens(text);

        auto hasArg = [&](const char* a) { for (const auto& t : tok) if (detail::lower(t) == a) return true; return false; };
        auto argAfter = [&](const char* a) -> std::string
        {
            for (size_t i = 0; i + 1 < tok.size(); i++)
                if (detail::lower(tok[i]) == a) return tok[i + 1];
            return {};
        };

        r.launcher = detail::contains(low, "net.minecraft.launcher.");
        const bool mainClass =
            detail::contains(low, "net.minecraft.client.main.main") ||
            detail::contains(low, "knot.knotclient") ||                       // Fabric and Quilt
            detail::contains(low, "net.minecraftforge.bootstrap");            // Forge bootstrap
        const std::string target = detail::lower(argAfter("--launchtarget")); // Forge / NeoForge
        const bool forgeTarget = target.find("client") != std::string::npos;
        const bool mcArgs = hasArg("--assetindex") && hasArg("--gamedir");    // present in every vanilla-style launch
        r.minecraft = !r.launcher && (mainClass || forgeTarget || mcArgs);

        r.version = detail::mcVersionFromId(argAfter("--version"));
        static const std::string kBrand = "-dminecraft.launcher.brand=";
        for (const auto& t : tok)
            if (detail::lower(t).compare(0, kBrand.size(), kBrand) == 0) r.launcherBrand = t.substr(kBrand.size());
        return r;
    }

    inline Match classifyProcess(const Evidence& e)
    {
        Match m;
        const std::string needle = detail::lower(e.titleNeedle);
        const WindowInfo* game = nullptr;
        bool titleMc = false;
        for (const auto& w : e.windows)
        {
            if (!game && detail::lower(w.className) == "glfw30") game = &w;
            if (!needle.empty() && detail::contains(detail::lower(w.title), needle)) titleMc = true;
        }
        m.launcherBrand = e.cmd.launcherBrand;
        if (!e.windows.empty()) m.title = (game ? game->title : e.windows.front().title);

        auto setVersion = [&]()
        {
            if (!e.cmd.version.empty()) { m.version = e.cmd.version; m.versionSource = "command line"; }
            else
            {
                const std::string t = versionFromTitle(m.title);
                if (!t.empty()) { m.version = t; m.versionSource = "window title"; }
            }
        };

        if (e.cmd.launcher)
        {
            m.kind = TargetKind::Launcher;
            m.reason = "Launcher, not the game.";
            return m;
        }
        if (game)
        {
            if (e.cmd.minecraft || titleMc)
            {
                m.kind = TargetKind::Game;
                m.how = e.cmd.minecraft ? "command line and game window" : "game window and title only";
                setVersion();
                return m;
            }
            m.kind = TargetKind::Other;
            m.reason = e.cmd.known ? "A Java game window, but not identified as Minecraft."
                                   : "Cannot confirm this Java window is Minecraft.";
            return m;
        }
        if (e.cmd.minecraft)
        {
            m.kind = TargetKind::Starting;
            m.reason = "Minecraft is starting: no game window yet.";
            setVersion();
            return m;
        }
        if (titleMc && detail::contains(detail::lower(m.title), "launcher"))
        {
            m.kind = TargetKind::Launcher;
            m.reason = "Launcher, not the game.";
            return m;
        }
        m.kind = TargetKind::Other;
        m.reason = e.windows.empty() ? "Java process without a game window."
                                     : "Java window is not a Minecraft game window.";
        return m;
    }
}
