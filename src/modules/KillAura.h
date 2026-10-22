#pragma once
#include "Module.h"

class KillAura : public Module
{
public:
    KillAura() : Module("KillAura", "Auto-attacks nearby entities", Category::Combat)
    {
        addFloat("Range", 4.5f, 1.0f, 8.0f);
        addInt("Delay", 2, 0, 20);
    }
    void onEnable() override;
    void onTick() override;
private:
    int m_delayCounter = 0;
};
