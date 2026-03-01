#pragma once

#include "../Hook.hpp"
#include "SDK/Client/Actor/Actor.hpp"
#include "SDK/Client/Render/BaseActorRenderContext.hpp"

class ActorRenderDispatcherHook : public Hook {
private:
    static void renderCallback(
        void* dispatcher,
        BaseActorRenderContext* entityRenderContext,
        Actor* entity,
        Vec3<float>* cameraTargetPos,
        Vec3<float>* pos,
        Vec2<float>* rot,
        bool ignoreLighting
    );

public:
    using original = void(__fastcall*)(
        void*,
        BaseActorRenderContext*,
        Actor*,
        Vec3<float>*,
        Vec3<float>*,
        Vec2<float>*,
        bool
    );

    static inline original funcOriginal = nullptr;

    ActorRenderDispatcherHook();

    void enableHook() override;
};

