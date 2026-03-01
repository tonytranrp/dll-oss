#include "ActorRenderDispatcherHook.hpp"

#include <format>

#include "ForcedRenderRotationState.hpp"
#include "SDK/SDK.hpp"
#include "Utils/Logger/Logger.hpp"
#include "Utils/Memory/Game/SignatureAndOffsetManager.hpp"

ActorRenderDispatcherHook::ActorRenderDispatcherHook()
    : Hook("ActorRenderDispatcher::render", GET_SIG_ADDRESS("ActorRenderDispatcher::render")) {}

void ActorRenderDispatcherHook::enableHook() {
    this->autoHook((void*)renderCallback, (void**)&funcOriginal);
}

void ActorRenderDispatcherHook::renderCallback(
    void* dispatcher,
    BaseActorRenderContext* entityRenderContext,
    Actor* entity,
    Vec3<float>* cameraTargetPos,
    Vec3<float>* pos,
    Vec2<float>* rot,
    bool ignoreLighting
) {
    static uint64_t lastDebugLogMs = 0;
    const auto debugLog = [&](const std::string& msg) {
        const uint64_t now = Utils::getCurrentMs();
        if (now - lastDebugLogMs < 140) return;
        lastDebugLogMs = now;
        Logger::custom(fg(fmt::color::deep_sky_blue), "PacketMineRotDBG", "{}", msg);
    };

    auto original = funcOriginal;
    if (original == nullptr) return;

    auto* localPlayer = SDK::clientInstance != nullptr ? SDK::clientInstance->getLocalPlayer() : nullptr;
    if (localPlayer == nullptr || entity == nullptr || entity != localPlayer) {
        return original(dispatcher, entityRenderContext, entity, cameraTargetPos, pos, rot, ignoreLighting);
    }

    Vec2<float> forcedRot{};
    float forcedHeadYaw = 0.0f;
    float forcedBodyYaw = 0.0f;
    if (!ForcedRenderRotationState::tryGet(forcedRot, forcedHeadYaw, forcedBodyYaw)) {
        return original(dispatcher, entityRenderContext, entity, cameraTargetPos, pos, rot, ignoreLighting);
    }

    debugLog(std::format(
        "apply forced rot pitch={:.2f} yaw={:.2f} head={:.2f} body={:.2f}",
        forcedRot.x,
        forcedRot.y,
        forcedHeadYaw,
        forcedBodyYaw
    ));

    auto* actorRotations = entity->getActorRotationComponent();
    auto* headRotations = entity->getActorHeadRotationComponent();
    auto* bodyRotations = entity->getMobBodyRotationComponent();

    Vec2<float> realRot{};
    Vec2<float> realRotPrev{};
    float realHeadRot = 0.0f;
    float realOldHeadRot = 0.0f;
    float realBodyYaw = 0.0f;
    float realOldBodyYaw = 0.0f;

    const bool hasActorRot = actorRotations != nullptr;
    const bool hasHeadRot = headRotations != nullptr;
    const bool hasBodyRot = bodyRotations != nullptr;

    if (!hasActorRot || !hasHeadRot || !hasBodyRot) {
        debugLog(std::format(
            "missing comps actor={} head={} body={}",
            hasActorRot,
            hasHeadRot,
            hasBodyRot
        ));
    }

    if (hasActorRot) {
        realRot = actorRotations->rot;
        realRotPrev = actorRotations->rotPrev;
        actorRotations->rot = forcedRot;
        actorRotations->rotPrev = forcedRot;
    }

    if (hasHeadRot) {
        realHeadRot = headRotations->mHeadRot;
        realOldHeadRot = headRotations->mOldHeadRot;
        headRotations->mHeadRot = forcedHeadYaw;
        headRotations->mOldHeadRot = forcedHeadYaw;
    }

    if (hasBodyRot) {
        realBodyYaw = bodyRotations->yBodyRot;
        realOldBodyYaw = bodyRotations->yOldBodyRot;
        bodyRotations->yBodyRot = forcedBodyYaw;
        bodyRotations->yOldBodyRot = forcedBodyYaw;
    }

    Vec2<float> forcedRenderRot = forcedRot;
    Vec2<float>* renderRotArg = rot != nullptr ? &forcedRenderRot : rot;

    original(dispatcher, entityRenderContext, entity, cameraTargetPos, pos, renderRotArg, ignoreLighting);

    if (hasBodyRot) {
        bodyRotations->yBodyRot = realBodyYaw;
        bodyRotations->yOldBodyRot = realOldBodyYaw;
    }

    if (hasHeadRot) {
        headRotations->mHeadRot = realHeadRot;
        headRotations->mOldHeadRot = realOldHeadRot;
    }

    if (hasActorRot) {
        actorRotations->rot = realRot;
        actorRotations->rotPrev = realRotPrev;
    }
}
