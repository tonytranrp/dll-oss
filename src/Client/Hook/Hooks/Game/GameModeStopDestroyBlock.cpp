#include "GameModeStopDestroyBlock.hpp"

#include "Events/Game/StopDestroyBlockEvent.hpp"
#include "SDK/SDK.hpp"
#include "Utils/Memory/Game/SignatureAndOffsetManager.hpp"

GameModeStopDestroyBlockHook::GameModeStopDestroyBlockHook() : Hook("GameModeStopDestroyBlock", 0) {}

void GameModeStopDestroyBlockHook::enableHook() {
    static auto base = GET_SIG_ADDRESS("GameMode::vtable");
    if (base == 0) {
        return;
    }

    int offset = *reinterpret_cast<int*>(base + 3);
    auto** vft = reinterpret_cast<uintptr_t**>(base + offset + 7);

    static auto stopDestroyVftOffset = GET_OFFSET("Gamemode::stopDestroyBlockVft");
    if (stopDestroyVftOffset <= 0) stopDestroyVftOffset = 4;

    this->manualHook(vft[stopDestroyVftOffset], (void*)callback, (void**)&funcOriginal);
}

void GameModeStopDestroyBlockHook::callback(Gamemode* gamemode, const BlockPos& blockPos) {
    if (SDK::clientInstance != nullptr && gamemode != nullptr) {
        auto* localPlayer = SDK::clientInstance->getLocalPlayer();
        if (localPlayer != nullptr && localPlayer == gamemode->getPlayer()) {
            auto event = nes::make_holder<StopDestroyBlockEvent>(gamemode, blockPos);
            eventMgr.trigger(event);
            if (event->isCancelled()) {
                return;
            }
        }
    }

    funcOriginal(gamemode, blockPos);
}
