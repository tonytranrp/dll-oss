#pragma once

#include "../Module.hpp"
#include "Events/Game/TickEvent.hpp"

#include "SDK/Client/Actor/Gamemode.hpp"
#include "SDK/Client/Block/Block.hpp"
#include "SDK/Client/Container/PlayerInventory.hpp"

class Regen : public Module {
private:
    struct BlockInfo {
        Block* mBlock;
        glm::ivec3 mPosition;

        AABB getAABB() const {
            const Vec3<float> lower{
                static_cast<float>(mPosition.x),
                static_cast<float>(mPosition.y),
                static_cast<float>(mPosition.z)
            };
            const Vec3<float> upper{
                static_cast<float>(mPosition.x) + 1.0f,
                static_cast<float>(mPosition.y) + 1.0f,
                static_cast<float>(mPosition.z) + 1.0f
            };
            return AABB(lower, upper);
        }

        float getDistance(const glm::vec3& pos) const {
            const AABB box = getAABB();
            const glm::vec3 closest{
                std::clamp(pos.x, box.lower.x, box.upper.x),
                std::clamp(pos.y, box.lower.y, box.upper.y),
                std::clamp(pos.z, box.lower.z, box.upper.z)
            };
            return glm::distance(closest, pos);
        }

        BlockInfo(Block* block, glm::ivec3 position) : mBlock(block), mPosition(position) {}
    };

    BlockPos mTargetPos{};
    uint8_t mTargetFace = 1;
    bool mHasTarget = false;
    bool mTargetPrimed = false;
    int mStuckTicks = 0;
    int mOriginalSlot = -1;
    uint64_t mLastDebugLogMs = 0;

    void clearTarget(bool sendStop = true);
    bool acquireTarget(class Player* player, class BlockSource* blockSource);

    static std::vector<BlockInfo> getBlockList(const glm::ivec3& position, float radius, class BlockSource* blockSource);
    static std::string normalizeBlockName(const Block* block);
    static bool isAirBlock(const Block* block);
    static bool isRedstoneOre(const Block* block);
    static uint8_t computeBestFace(const Vec3<float>& eyePos, const glm::ivec3& blockPos);
    static int getBestPickaxeSlot(PlayerInventory* supplies);
    static float getPickaxeScore(const std::string& itemName);

    bool shouldDebugLog(uint64_t minDelayMs);
    void debugTrace(const std::string& message);

public:
    Regen()
        : Module(
              "Regen",
              "Automatically mines nearby redstone ore by directly using gamemode break calls.",
              IDR_BLOCK_PNG,
              "",
              false,
              {"automine", "redstone", "ore_miner"}
          ) {}

    void onEnable() override;
    void onDisable() override;
    void defaultConfig() override;
    void settingsRender(float settingsOffset) override;
    void onTick(TickEvent& event);
};
