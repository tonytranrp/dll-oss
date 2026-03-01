#pragma once

#include "../Module.hpp"
#include "Assets/Assets.hpp"
#include "Events/Game/StartDestroyBlockEvent.hpp"
#include "Events/Game/StopDestroyBlockEvent.hpp"
#include "Events/Game/TickEvent.hpp"
#include "Events/Network/PacketSendEvent.hpp"
#include "Events/Render/Render3DEvent.hpp"
#include "Utils/Utils.hpp"
#include <cstdint>
#include <string>
#include <utility>

class PacketMine : public Module {
private:
    BlockPos mTargetPos{};
    uint8_t mTargetFace = 0;
    bool mHasTarget = false;
    Gamemode* mTargetGamemode = nullptr;
    bool mSuppressMineEvents = false;
    float mMineProgress = 0.0f;
    float mRenderProgress = 0.0f;
    Vec2<float> mQueuedRotation{0.0f, 0.0f};
    bool mHasQueuedRotation = false;
    int mQueuedRotationTicks = 0;
    bool mMiningPrimed = false;
    int mFailedBreakTicks = 0;
    uint64_t mLastDebugTickLogMs = 0;
    uint64_t mLastDebugRenderLogMs = 0;
    uint64_t mLastDebugPacketLogMs = 0;
    uint64_t mLastDebugRotationLogMs = 0;

    [[nodiscard]] bool isTargetInRange();
    [[nodiscard]] static bool isAirBlock(const Block* block);
    [[nodiscard]] static std::string normalizeBlockName(const Block* block);
    [[nodiscard]] static float getToolTierScore(const std::string& itemName);
    [[nodiscard]] static float getToolBlockMatchScore(const std::string& itemName, const std::string& blockName);
    [[nodiscard]] bool buildRotationToTarget(Vec2<float>& outRotation) const;
    [[nodiscard]] std::pair<int, float> getBestHotbarSlot(const Block* block);
    [[nodiscard]] bool shouldDebugLog(const char* key, uint64_t minDelayMs);
    void debugTrace(const std::string& message);

    void clearTarget(const char* reason = nullptr);

public:
    PacketMine()
        : Module(
              "Packet Mine",
              "Intercepts start/stop mining and performs accelerated block breaking.",
              IDR_BLOCK_PNG,
              "",
              false,
              {"packetmine", "speedmine", "instamine"}
          ) {}

    void onEnable() override;
    void onDisable() override;
    void defaultConfig() override;
    void settingsRender(float settingsOffset) override;

    void onTick(TickEvent& event);
    void onRender3D(Render3DEvent& event);
    void onStartDestroyBlock(StartDestroyBlockEvent& event);
    void onStopDestroyBlock(StopDestroyBlockEvent& event);
    void onPacketSend(PacketSendEvent& event);
};
