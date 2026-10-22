#pragma once
// Tiny INI reader shared by the client settings and the injector config.
// Keys are stored as "<section>.<key>" -- the section is lower-cased, the key
// keeps its case (the client stores module names as keys).

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>

namespace ini
{
    using Map = std::map<std::string, std::string>;

    inline std::string trim(const std::string& s)
    {
        size_t b = 0, e = s.size();
        while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) b++;
        while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) e--;
        return s.substr(b, e - b);
    }

    inline std::string lower(std::string s)
    {
        std::transform(s.begin(), s.end(), s.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return s;
    }

    inline Map parse(const std::string& text)
    {
        Map out;
        std::istringstream is(text);
        std::string line, section;
        while (std::getline(is, line))
        {
            line = trim(line);
            if (line.empty() || line[0] == ';' || line[0] == '#') continue;
            if (line.front() == '[' && line.back() == ']')
            {
                section = lower(trim(line.substr(1, line.size() - 2)));
                continue;
            }
            size_t eq = line.find('=');
            if (eq == std::string::npos) continue;
            std::string key = trim(line.substr(0, eq));
            std::string val = trim(line.substr(eq + 1));
            if (key.empty()) continue;
            out[section + "." + key] = val;
        }
        return out;
    }

    inline bool readFile(const std::filesystem::path& path, Map& out)
    {
        std::ifstream f(path, std::ios::in | std::ios::binary);
        if (!f) return false;
        std::stringstream ss;
        ss << f.rdbuf();
        std::string text = ss.str();
        // Skip a UTF-8 BOM if an editor added one.
        if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF &&
            static_cast<unsigned char>(text[1]) == 0xBB && static_cast<unsigned char>(text[2]) == 0xBF)
            text.erase(0, 3);
        out = parse(text);
        return true;
    }

    inline std::string get(const Map& m, const std::string& key, const std::string& def = "")
    {
        auto it = m.find(key);
        return it == m.end() ? def : it->second;
    }

    inline bool getBool(const Map& m, const std::string& key, bool def)
    {
        auto it = m.find(key);
        if (it == m.end()) return def;
        std::string v = lower(it->second);
        if (v == "1" || v == "true" || v == "yes" || v == "on") return true;
        if (v == "0" || v == "false" || v == "no" || v == "off") return false;
        return def;
    }

    inline float getFloat(const Map& m, const std::string& key, float def)
    {
        auto it = m.find(key);
        if (it == m.end()) return def;
        try { return std::stof(it->second); }
        catch (...) { return def; }
    }
}
