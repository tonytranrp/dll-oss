#include "GameModeStartDestroyBlock.hpp"

#include "Events/Game/StartDestroyBlockEvent.hpp"
#include "SDK/SDK.hpp"
#include "Utils/Memory/Game/SignatureAndOffsetManager.hpp"

GameModeStartDestroyBlockHook::GameModeStartDestroyBlockHook() : Hook("GameModeStartDestroyBlock", 0) {}

void GameModeStartDestroyBlockHook::enableHook() {
    static auto base = GET_SIG_ADDRESS("GameMode::vtable");
    if (base == 0) {
        return;
    }

    int offset = *reinterpret_cast<int*>(base + 3);
    auto** vft = reinterpret_cast<uintptr_t**>(base + offset + 7);

    static auto startDestroyVftOffset = GET_OFFSET("Gamemode::startDestroyBlockVft");
    if (startDestroyVftOffset <= 0) startDestroyVftOffset = 1;

    this->manualHook(vft[startDestroyVftOffset], (void*)callback, (void**)&funcOriginal);
}

bool GameModeStartDestroyBlockHook::callback(
    Gamemode* gamemode,
    const BlockPos& blockPos,
    const uint8_t face,
    bool& hasDestroyedBlock
) {
    bool result = funcOriginal(gamemode, blockPos, face, hasDestroyedBlock);

    if (SDK::clientInstance != nullptr && gamemode != nullptr) {
        auto* localPlayer = SDK::clientInstance->getLocalPlayer();
        if (localPlayer != nullptr && localPlayer == gamemode->getPlayer()) {
            auto event = nes::make_holder<StartDestroyBlockEvent>(gamemode, blockPos, face, result, hasDestroyedBlock);
            eventMgr.trigger(event);
            if (result && event->isCancelled()) {
                return false;
            }
        }
    }

    return result;
}
