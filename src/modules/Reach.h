#pragma once
#include "Module.h"

// PLACEHOLDER. Extending the interaction range needs the player's interaction-range attribute
// (EntityAttributes.ENTITY_INTERACTION_RANGE) resolved through JNI, which is not implemented.
// The module is registered so it shows up in the menu, but it refuses to enable.
class Reach : public Module
{
public:
    Reach() : Module("Reach", "Extends interaction range", Category::Player)
    {
        setMaturity(Maturity::Unfinished, "Placeholder - the range attribute is not implemented yet.");
        addFloat("Range", 5.0f, 3.0f, 8.0f);
    }
};
