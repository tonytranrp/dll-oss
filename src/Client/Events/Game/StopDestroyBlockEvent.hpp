#pragma once

#include "../Cancellable.hpp"
#include "../Event.hpp"
#include "../../../SDK/Client/Actor/Gamemode.hpp"
#include "../../../SDK/Client/Level/HitResult.hpp"

class StopDestroyBlockEvent : public Event, public Cancellable {
private:
    Gamemode* mGamemode;
    BlockPos mBlockPos;

public:
    StopDestroyBlockEvent(Gamemode* gamemode, const BlockPos& blockPos) : mGamemode(gamemode), mBlockPos(blockPos) {}

    [[nodiscard]] Gamemode* getGamemode() const { return mGamemode; }
    [[nodiscard]] const BlockPos& getBlockPos() const { return mBlockPos; }
};
