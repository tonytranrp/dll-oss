#pragma once

#include "../Hook.hpp"
#include "../../../../SDK/Client/Actor/Gamemode.hpp"

class GameModeStopDestroyBlockHook : public Hook {
private:
    static void callback(Gamemode* gamemode, const BlockPos& blockPos);

public:
    using original = void(__thiscall*)(Gamemode*, const BlockPos&);
    static inline original funcOriginal = nullptr;

    GameModeStopDestroyBlockHook();

    void enableHook() override;
};
