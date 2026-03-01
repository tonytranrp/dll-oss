#pragma once

#include <cstdint>

#include "../Block/Block.hpp"
#include "../Level/HitResult.hpp"
#include <Utils/Memory/Memory.hpp>
#include <Utils/Memory/Game/SignatureAndOffsetManager.hpp>

FK(Player)

class Gamemode {
public:
    Player* getPlayer() {
        return hat::member_at<Player*>(this, GET_OFFSET("Gamemode::player"));
    };
    float getLastBreakProgress() {
        return hat::member_at<float>(this, GET_OFFSET("Gamemode::lastBreakProgress"));
    };

    float getDestroyRate(const Block* block) {
        if (block == nullptr) {
            return 0.0f;
        }

        using FuncT = float(__thiscall*)(Gamemode*, const Block*);
        static auto func = reinterpret_cast<FuncT>(GET_SIG_ADDRESS("GameMode::getDestroyRate"));
        if (func == nullptr) {
            return 0.0f;
        }

        return func(this, block);
    }

    bool startDestroyBlock(const BlockPos& pos, const uint8_t face, bool& hasDestroyedBlock) {
        static int off = GET_OFFSET("Gamemode::startDestroyBlockVft");
        if (off <= 0) off = 1;
        return Memory::CallVFuncI<bool, const BlockPos&, uint8_t, bool&>(off, this, pos, face, hasDestroyedBlock);
    }

    bool destroyBlock(const BlockPos& pos, const uint8_t face) {
        static int off = GET_OFFSET("Gamemode::destroyBlockVft");
        if (off <= 0) off = 2;
        return Memory::CallVFuncI<bool, const BlockPos&, uint8_t>(off, this, pos, face);
    }

    bool continueDestroyBlock(const BlockPos& pos, const uint8_t face, const Vec3<float>& playerPos, bool& hasDestroyedBlock) {
        static int off = GET_OFFSET("Gamemode::continueDestroyBlockVft");
        if (off <= 0) off = 3;
        return Memory::CallVFuncI<bool, const BlockPos&, uint8_t, const Vec3<float>&, bool&>(
            off,
            this,
            pos,
            face,
            playerPos,
            hasDestroyedBlock
        );
    }

    void stopDestroyBlock(const BlockPos& pos) {
        static int off = GET_OFFSET("Gamemode::stopDestroyBlockVft");
        if (off <= 0) off = 4;
        Memory::CallVFuncI<void, const BlockPos&>(off, this, pos);
    }
};
