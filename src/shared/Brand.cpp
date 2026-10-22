#include "Brand.h"
#include "UiTheme.h"

namespace
{
    struct Stone { float x, y, hw, hh; int colour; };

    // Mirrors STONES in tools/gen_assets.py (normalised to a 1x1 badge centred on 0,0).
    constexpr Stone kStones[3] = {
        { 0.000f,  0.200f, 0.300f, 0.102f, 0 },   // base  : slate
        { 0.028f, -0.022f, 0.220f, 0.091f, 1 },   // middle: teal accent
        {-0.013f, -0.222f, 0.131f, 0.080f, 2 },   // top   : amber accent2
    };

    const ImVec4& stoneColour(int i)
    {
        static const ImVec4 slate(0.361f, 0.451f, 0.439f, 1.0f); // #5C7370
        switch (i)
        {
        case 1:  return UI::Col::Accent;
        case 2:  return UI::Col::Accent2;
        default: return slate;
        }
    }
}

namespace Brand
{
    void drawMark(ImDrawList* dl, ImVec2 c, float size)
    {
        using namespace UI;
        for (const Stone& s : kStones)
        {
            const ImVec2 a(c.x + (s.x - s.hw) * size, c.y + (s.y - s.hh) * size);
            const ImVec2 b(c.x + (s.x + s.hw) * size, c.y + (s.y + s.hh) * size);
            dl->AddRectFilled(a, b, C(stoneColour(s.colour)), s.hh * size);
        }
    }

    void drawLogo(ImDrawList* dl, ImVec2 c, float size)
    {
        using namespace UI;

        const float half = size * 0.5f;
        const ImVec2 p0(c.x - half, c.y - half);
        const ImVec2 p1(c.x + half, c.y + half);
        const float rounding = size * 0.22f;

        dl->AddRectFilled(p0, p1, C(Col::Bg), rounding);
        dl->AddRect(p0, p1, C(Col::BorderStrong), rounding, 0, 1.0f);
        drawMark(dl, c, size);
    }
}
