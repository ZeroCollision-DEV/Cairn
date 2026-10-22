#pragma once
#include "Module.h"

class Fly : public Module
{
public:
    Fly() : Module("Fly", "Creative-style flight", Category::Movement)
    {
        addBool("AntiKick", true);
    }
    void onEnable() override;
    void onDisable() override;
    void onTick() override;
private:
    int m_tickCounter = 0;
};
