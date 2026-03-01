#pragma once

#include "SDK/Client/Actor/EntityContext.hpp"

struct ActorHeadRotationComponent : IEntityComponent {
    float mHeadRot;
    float mOldHeadRot;
};
static_assert(sizeof(ActorHeadRotationComponent) == 0x8);
