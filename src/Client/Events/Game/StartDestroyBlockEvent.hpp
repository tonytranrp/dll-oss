#pragma once

#include "../Cancellable.hpp"
#include "../Event.hpp"
#include "../../../SDK/Client/Actor/Gamemode.hpp"
#include "../../../SDK/Client/Level/HitResult.hpp"

class StartDestroyBlockEvent : public Event, public Cancellable {
private:
    Gamemode* mGamemode;
    BlockPos mBlockPos;
    uint8_t mFace;
    bool mStarted;
    bool* mHasDestroyedBlock;

public:
    StartDestroyBlockEvent(
        Gamemode* gamemode,
        const BlockPos& blockPos,
        const uint8_t face,
        const bool started,
        bool& hasDestroyedBlock
    ) : mGamemode(gamemode),
        mBlockPos(blockPos),
        mFace(face),
        mStarted(started),
        mHasDestroyedBlock(&hasDestroyedBlock) {}

    [[nodiscard]] Gamemode* getGamemode() const { return mGamemode; }
    [[nodiscard]] const BlockPos& getBlockPos() const { return mBlockPos; }
    [[nodiscard]] uint8_t getFace() const { return mFace; }
    [[nodiscard]] bool getStarted() const { return mStarted; }
    [[nodiscard]] bool* getHasDestroyedBlockPtr() const { return mHasDestroyedBlock; }
};
