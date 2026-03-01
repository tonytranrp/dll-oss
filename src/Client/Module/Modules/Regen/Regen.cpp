#include "Regen.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <format>
#include <limits>
#include <string>

#include "Events/EventManager.hpp"
#include "Events/Events.hpp"
#include "SDK/SDK.hpp"
#include "SDK/Client/Actor/Player.hpp"
#include "SDK/Client/Block/BlockLegacy.hpp"
#include "SDK/Client/Block/BlockSource.hpp"
#include "SDK/Client/Container/Inventory.hpp"
#include "Utils/Logger/Logger.hpp"

namespace {
std::string toLower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return text;
}
} // namespace

void Regen::onEnable() {
    clearTarget(false);
    Listen(this, TickEvent, &Regen::onTick)
    Module::onEnable();
}

void Regen::onDisable() {
    Deafen(this, TickEvent, &Regen::onTick)
    clearTarget(true);
    Module::onDisable();
}

void Regen::defaultConfig() {
    Module::defaultConfig("core");
    setDef("radius", 6.0f);
    setDef("attemptsPerTick", 5);
    setDef("sendStopCycle", true);
    setDef("maxStuckTicks", 20);
    setDef("autoTool", true);
    setDef("switchBack", true);
    setDef("debugLogs", false);
}

void Regen::settingsRender(float settingsOffset) {
    (void)settingsOffset;
    initSettingsPage();

    addHeader("Mining");
    addSlider("Scan Radius", "How far around player to search for redstone ore.", "radius", 12.0f, 1.0f);
    addSliderInt("Continue Attempts", "continueDestroyBlock calls per tick.", "attemptsPerTick", 20, 1);
    addToggle("Send Stop Cycle", "Sends stopDestroyBlock after continue cycle for server sync.", "sendStopCycle");
    addSliderInt("Max Stuck Ticks", "Retarget after this many unsuccessful mining ticks.", "maxStuckTicks", 80, 5);
    addToggle("Auto Tool", "Auto switch to best pickaxe in hotbar.", "autoTool");
    addConditionalToggle(getOps<bool>("autoTool"), "Switch Back", "Restore previous slot when no target.", "switchBack");
    addToggle("Debug Logs", "", "debugLogs");

    FlarialGUI::UnsetScrollView();
    resetPadding();
}

void Regen::onTick(TickEvent& event) {
    if (!this->isEnabled()) return;
    if (!SDK::hasInstanced || SDK::clientInstance == nullptr) return;

    auto* player = SDK::clientInstance->getLocalPlayer();
    if (player == nullptr || event.getActor() != player) return;

    auto* gamemode = player->getGamemode();
    auto* supplies = player->getSupplies();
    auto* blockSource = SDK::clientInstance->getBlockSource();
    auto* playerPos = player->getPosition();
    if (gamemode == nullptr || supplies == nullptr || blockSource == nullptr || playerPos == nullptr) {
        clearTarget(true);
        return;
    }

    if (gamemode->getPlayer() != player) {
        clearTarget(true);
        return;
    }

    if (!mHasTarget && !acquireTarget(player, blockSource)) {
        if (mOriginalSlot != -1 && getOps<bool>("switchBack")) {
            supplies->setSelectedSlot(std::clamp(mOriginalSlot, 0, 8));
            mOriginalSlot = -1;
        }
        return;
    }

    Block* currentBlock = blockSource->getBlock(mTargetPos);
    if (isAirBlock(currentBlock) || !isRedstoneOre(currentBlock)) {
        clearTarget(false);
        if (!acquireTarget(player, blockSource)) return;
        currentBlock = blockSource->getBlock(mTargetPos);
        if (isAirBlock(currentBlock) || !isRedstoneOre(currentBlock)) {
            clearTarget(false);
            return;
        }
    }

    if (getOps<bool>("autoTool")) {
        const int currentSlot = std::clamp(supplies->getSelectedSlot(), 0, 8);
        const int bestSlot = getBestPickaxeSlot(supplies);
        if (bestSlot >= 0 && bestSlot < 9 && bestSlot != currentSlot) {
            if (mOriginalSlot == -1) {
                mOriginalSlot = currentSlot;
            }
            supplies->setSelectedSlot(bestSlot);
        }
    }

    bool hasDestroyedBlock = false;
    bool startOk = true;
    bool continueOk = false;
    bool stopSent = false;

    if (!mTargetPrimed) {
        startOk = gamemode->startDestroyBlock(mTargetPos, mTargetFace, hasDestroyedBlock);
        mTargetPrimed = startOk || hasDestroyedBlock;
    }

    if (mTargetPrimed && !hasDestroyedBlock) {
        const int attempts = std::clamp(getOps<int>("attemptsPerTick"), 1, 30);
        for (int i = 0; i < attempts; i++) {
            continueOk = gamemode->continueDestroyBlock(mTargetPos, mTargetFace, *playerPos, hasDestroyedBlock) || continueOk;
            if (hasDestroyedBlock) break;
        }
    }

    Block* postBlock = blockSource->getBlock(mTargetPos);
    const bool brokenNow = hasDestroyedBlock || isAirBlock(postBlock);
    if (brokenNow) {
        if (shouldDebugLog(40)) {
            debugTrace(std::format(
                "broke redstone at ({}, {}, {})",
                mTargetPos.x,
                mTargetPos.y,
                mTargetPos.z
            ));
        }
        clearTarget(false);
        (void)acquireTarget(player, blockSource);
        return;
    }

    if (getOps<bool>("sendStopCycle")) {
        gamemode->stopDestroyBlock(mTargetPos);
        stopSent = true;
        mTargetPrimed = false;
    }

    mStuckTicks++;
    if (mStuckTicks >= std::max(1, getOps<int>("maxStuckTicks"))) {
        if (shouldDebugLog(60)) {
            debugTrace("stuck while mining target, retargeting");
        }
        clearTarget(true);
        return;
    }

    if (shouldDebugLog(120)) {
        debugTrace(std::format(
            "tick target=({}, {}, {}) face={} primed={} startOk={} continueOk={} stopSent={} stuck={}",
            mTargetPos.x,
            mTargetPos.y,
            mTargetPos.z,
            static_cast<int>(mTargetFace),
            mTargetPrimed,
            startOk,
            continueOk,
            stopSent,
            mStuckTicks
        ));
    }
}

void Regen::clearTarget(bool sendStop) {
    if (mHasTarget && sendStop && mTargetPrimed && SDK::clientInstance != nullptr) {
        auto* player = SDK::clientInstance->getLocalPlayer();
        auto* gm = player != nullptr ? player->getGamemode() : nullptr;
        if (gm != nullptr && gm->getPlayer() == player) {
            gm->stopDestroyBlock(mTargetPos);
        }
    }

    if (mOriginalSlot != -1 && getOps<bool>("switchBack") && SDK::clientInstance != nullptr) {
        auto* player = SDK::clientInstance->getLocalPlayer();
        auto* supplies = player != nullptr ? player->getSupplies() : nullptr;
        if (supplies != nullptr) {
            supplies->setSelectedSlot(std::clamp(mOriginalSlot, 0, 8));
        }
    }

    mOriginalSlot = -1;
    mTargetPos = {};
    mTargetFace = 1;
    mHasTarget = false;
    mTargetPrimed = false;
    mStuckTicks = 0;
}

bool Regen::acquireTarget(Player* player, BlockSource* blockSource) {
    if (player == nullptr || blockSource == nullptr || player->getPosition() == nullptr) return false;

    const Vec3<float>& pos = *player->getPosition();
    const glm::ivec3 center{
        static_cast<int>(std::floor(pos.x)),
        static_cast<int>(std::floor(pos.y)),
        static_cast<int>(std::floor(pos.z))
    };
    const glm::vec3 eyePos{pos.x, pos.y + 1.62f, pos.z};

    auto blocks = getBlockList(center, getOps<float>("radius"), blockSource);
    float bestDist = std::numeric_limits<float>::max();
    BlockInfo* best = nullptr;

    for (auto& info : blocks) {
        if (!isRedstoneOre(info.mBlock)) continue;
        if (isAirBlock(info.mBlock)) continue;

        const float dist = info.getDistance(eyePos);
        if (dist < bestDist) {
            bestDist = dist;
            best = &info;
        }
    }

    if (best == nullptr) return false;

    mTargetPos = BlockPos{best->mPosition.x, best->mPosition.y, best->mPosition.z};
    mTargetFace = computeBestFace(Vec3<float>{eyePos.x, eyePos.y, eyePos.z}, best->mPosition);
    mHasTarget = true;
    mTargetPrimed = false;
    mStuckTicks = 0;

    if (shouldDebugLog(60)) {
        debugTrace(std::format(
            "new target=({}, {}, {}) face={} dist={:.2f}",
            mTargetPos.x,
            mTargetPos.y,
            mTargetPos.z,
            static_cast<int>(mTargetFace),
            bestDist
        ));
    }

    return true;
}

std::vector<Regen::BlockInfo> Regen::getBlockList(
    const glm::ivec3& position,
    float radius,
    BlockSource* blockSource
) {
    std::vector<BlockInfo> blocks{};
    if (blockSource == nullptr) return blocks;

    const int r = std::max(1, static_cast<int>(std::floor(radius)));
    const int diameter = (r * 2) + 1;
    blocks.reserve(diameter * diameter * diameter);

    for (int x = position.x - r; x <= position.x + r; x++) {
        for (int y = position.y - r; y <= position.y + r; y++) {
            for (int z = position.z - r; z <= position.z + r; z++) {
                Block* block = blockSource->getBlock(BlockPos{x, y, z});
                if (block != nullptr) {
                    blocks.emplace_back(block, glm::ivec3{x, y, z});
                }
            }
        }
    }

    return blocks;
}

std::string Regen::normalizeBlockName(const Block* block) {
    if (block == nullptr) return {};
    auto* legacy = const_cast<Block*>(block)->getBlockLegacy();
    if (legacy == nullptr) return {};

    std::string name = toLower(legacy->getName());
    if (name.rfind("minecraft:", 0) == 0) name.erase(0, 10);
    return name;
}

bool Regen::isAirBlock(const Block* block) {
    const std::string name = normalizeBlockName(block);
    return name.empty() || name == "air" || name == "cave_air" || name == "void_air" || name.ends_with("_air");
}

bool Regen::isRedstoneOre(const Block* block) {
    const std::string name = normalizeBlockName(block);
    return name == "redstone_ore" ||
           name == "lit_redstone_ore" ||
           name == "deepslate_redstone_ore" ||
           name == "lit_deepslate_redstone_ore";
}

uint8_t Regen::computeBestFace(const Vec3<float>& eyePos, const glm::ivec3& blockPos) {
    const float cx = static_cast<float>(blockPos.x) + 0.5f;
    const float cy = static_cast<float>(blockPos.y) + 0.5f;
    const float cz = static_cast<float>(blockPos.z) + 0.5f;

    const float dx = cx - eyePos.x;
    const float dy = cy - eyePos.y;
    const float dz = cz - eyePos.z;

    const float ax = std::fabs(dx);
    const float ay = std::fabs(dy);
    const float az = std::fabs(dz);

    if (ay >= ax && ay >= az) {
        return dy > 0.0f ? 0 : 1; // down/up
    }
    if (ax >= az) {
        return dx > 0.0f ? 4 : 5; // west/east
    }
    return dz > 0.0f ? 2 : 3; // north/south
}

float Regen::getPickaxeScore(const std::string& itemName) {
    if (itemName.find("pickaxe") == std::string::npos) return -1.0f;
    if (itemName.find("netherite_") != std::string::npos) return 6.0f;
    if (itemName.find("golden_") != std::string::npos) return 5.5f;
    if (itemName.find("diamond_") != std::string::npos) return 5.0f;
    if (itemName.find("iron_") != std::string::npos) return 4.0f;
    if (itemName.find("stone_") != std::string::npos) return 3.0f;
    if (itemName.find("wooden_") != std::string::npos) return 2.0f;
    return 1.5f;
}

int Regen::getBestPickaxeSlot(PlayerInventory* supplies) {
    if (supplies == nullptr) return -1;
    Inventory* inv = supplies->getInventory();
    if (inv == nullptr) return -1;

    int bestSlot = -1;
    float bestScore = -1.0f;
    for (int i = 0; i < 9; i++) {
        ItemStack* stack = inv->getItem(i);
        Item* item = stack != nullptr ? stack->getItem() : nullptr;
        const std::string itemName = item != nullptr ? toLower(item->name) : std::string{};
        const float score = getPickaxeScore(itemName);
        if (score > bestScore) {
            bestScore = score;
            bestSlot = i;
        }
    }

    return bestSlot;
}

bool Regen::shouldDebugLog(uint64_t minDelayMs) {
    if (!getOps<bool>("debugLogs")) return false;
    const uint64_t now = Utils::getCurrentMs();
    if (now - mLastDebugLogMs < minDelayMs) return false;
    mLastDebugLogMs = now;
    return true;
}

void Regen::debugTrace(const std::string& message) {
    if (!getOps<bool>("debugLogs")) return;
    Logger::custom(fg(fmt::color::orange), "Regen", "{}", message);
}

