#pragma once

#include "SDK/Client/Actor/EntityContext.hpp"

struct MobBodyRotationComponent : IEntityComponent {
    float yBodyRot;
    float yOldBodyRot;
};
static_assert(sizeof(MobBodyRotationComponent) == 0x8);
