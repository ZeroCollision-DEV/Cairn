#pragma once
#include "Module.h"

class Speed : public Module
{
public:
    Speed() : Module("Speed", "Increases movement speed", Category::Movement)
    {
        addFloat("Multiplier", 1.5f, 1.0f, 5.0f);
    }
    void onTick() override;
};
