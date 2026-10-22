#pragma once
#include "Module.h"

class NoFall : public Module
{
public:
    NoFall() : Module("NoFall", "Prevents fall damage", Category::Movement) {}
    void onTick() override;
};
