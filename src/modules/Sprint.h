#pragma once
#include "Module.h"

class Sprint : public Module
{
public:
    Sprint() : Module("Sprint", "Automatically sprints", Category::Movement)
    {
        addKey("Keybind", 0);
    }
    void onTick() override;
};
