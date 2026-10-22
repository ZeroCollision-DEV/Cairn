#pragma once
#include "Module.h"

class FullBright : public Module
{
public:
    FullBright() : Module("FullBright", "Maximum brightness", Category::Render)
    {
    }
    void onEnable() override;
    void onDisable() override;
private:
    double m_originalGamma = 0.5;
    bool   m_hasOriginal   = false;
};
