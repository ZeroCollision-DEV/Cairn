#pragma once
// Thin compatibility layer. The palette, fonts and style now live in the shared
// UI library (src/shared/UiTheme.h) so the client and the injector look alike.
#include "../shared/UiTheme.h"

namespace Theme
{
    // Applies the shared style at the current UI scale.
    void apply();
}
