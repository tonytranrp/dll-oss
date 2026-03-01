#pragma once

#include "../Hook.hpp"
#include "../../../../SDK/Client/Actor/Gamemode.hpp"

class GameModeStartDestroyBlockHook : public Hook {
private:
    static bool callback(Gamemode* gamemode, const BlockPos& blockPos, uint8_t face, bool& hasDestroyedBlock);

public:
    using original = bool(__thiscall*)(Gamemode*, const BlockPos&, uint8_t, bool&);
    static inline original funcOriginal = nullptr;

    GameModeStartDestroyBlockHook();

    void enableHook() override;
};
