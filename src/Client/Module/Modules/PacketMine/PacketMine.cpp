#include "PacketMine.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <format>
#include <initializer_list>
#include <string>

#include "Events/EventManager.hpp"
#include "Events/Events.hpp"
#include "Hook/Hooks/Render/ForcedRenderRotationState.hpp"
#include "SDK/SDK.hpp"
#include "SDK/Client/Block/Block.hpp"
#include "SDK/Client/Block/BlockLegacy.hpp"
#include "SDK/Client/Container/Inventory.hpp"
#include "SDK/Client/Network/Packet/PlayerAuthInputPacket.hpp"
#include "Utils/Logger/Logger.hpp"
#include "Utils/Render/DrawUtil3D.hpp"
#include "Utils/Render/MaterialUtils.hpp"

namespace {
std::string toLower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](const unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return text;
}

bool containsAny(const std::string& text, std::initializer_list<const char*> needles) {
    for (const char* needle : needles) {
        if (text.find(needle) != std::string::npos) return true;
    }
    return false;
}

bool isPickaxe(const std::string& itemName) {
    return itemName.find("pickaxe") != std::string::npos;
}

bool isAxe(const std::string& itemName) {
    return !isPickaxe(itemName) && itemName.find("axe") != std::string::npos;
}

bool isShovel(const std::string& itemName) {
    return itemName.find("shovel") != std::string::npos || itemName.find("spade") != std::string::npos;
}

bool isHoe(const std::string& itemName) {
    return itemName.find("hoe") != std::string::npos;
}

bool isShears(const std::string& itemName) {
    return itemName.find("shears") != std::string::npos;
}

bool isPickaxeBlock(const std::string& blockName) {
    return blockName.find("_ore") != std::string::npos ||
           containsAny(
               blockName,
               {
                   "stone", "deepslate", "cobble", "obsidian", "netherrack", "basalt", "blackstone", "end_stone",
                   "quartz", "brick", "terracotta", "prismarine", "anvil", "amethyst", "calcite"
               }
           );
}

bool isAxeBlock(const std::string& blockName) {
    return containsAny(
        blockName,
        {
            "_log", "_wood", "_stem", "planks", "bookshelf", "chest", "barrel", "crafting_table",
            "pumpkin", "melon", "ladder", "_sign", "bee_nest", "beehive"
        }
    );
}

bool isShovelBlock(const std::string& blockName) {
    return containsAny(
        blockName,
        {
            "dirt", "grass", "sand", "gravel", "clay", "soul_sand", "soul_soil",
            "snow", "powder_snow", "mud", "mycelium", "podzol", "farmland", "path", "rooted_dirt"
        }
    );
}

bool isHoeBlock(const std::string& blockName) {
    return containsAny(blockName, {"leaves", "hay_block", "wart_block", "moss", "sponge", "dried_kelp_block"});
}

float normalizeYawDegrees(float yaw) {
    while (yaw <= -180.0f) yaw += 360.0f;
    while (yaw > 180.0f) yaw -= 360.0f;
    return yaw;
}

Vec2<float> calcRotationTo(const Vec3<float>& from, const Vec3<float>& to) {
    constexpr float radToDeg = 57.29577951308232f;
    const Vec3<float> delta{to.x - from.x, to.y - from.y, to.z - from.z};
    const float xz = std::sqrt(delta.x * delta.x + delta.z * delta.z);

    const float yaw = normalizeYawDegrees(std::atan2(delta.z, delta.x) * radToDeg - 90.0f);
    const float pitch = std::clamp(-std::atan2(delta.y, xz) * radToDeg, -90.0f, 90.0f);
    return Vec2<float>{pitch, yaw};
}

class ScopedBool {
public:
    explicit ScopedBool(bool& value) : mValue(value) {
        mValue = true;
    }

    ~ScopedBool() {
        mValue = false;
    }

private:
    bool& mValue;
};

bool isReadableAddress(const void* ptr, std::size_t bytes) {
    if (ptr == nullptr || bytes == 0) return false;

    MEMORY_BASIC_INFORMATION mbi{};
    if (VirtualQuery(ptr, &mbi, sizeof(mbi)) == 0) return false;
    if (mbi.State != MEM_COMMIT) return false;
    if ((mbi.Protect & PAGE_GUARD) != 0 || (mbi.Protect & PAGE_NOACCESS) != 0) return false;

    const auto start = reinterpret_cast<std::uintptr_t>(ptr);
    const auto end = start + bytes;
    const auto regionStart = reinterpret_cast<std::uintptr_t>(mbi.BaseAddress);
    const auto regionEnd = regionStart + mbi.RegionSize;
    return end <= regionEnd;
}

bool isGamemodeVftCallable(Gamemode* gamemode, int vftIndex) {
    if (gamemode == nullptr || vftIndex < 0) return false;
    if (!isReadableAddress(gamemode, sizeof(void*))) return false;

    auto** vft = *reinterpret_cast<void***>(gamemode);
    if (vft == nullptr) return false;
    if (!isReadableAddress(vft, static_cast<std::size_t>(vftIndex + 1) * sizeof(void*))) return false;
    return vft[vftIndex] != nullptr;
}

bool tryDestroyBlockSafe(Gamemode* gamemode, const BlockPos& pos, uint8_t face, bool& hadAccessViolation) {
    hadAccessViolation = false;
    if (gamemode == nullptr) return false;

    __try {
        return gamemode->destroyBlock(pos, face);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        hadAccessViolation = true;
        return false;
    }
}
} // namespace

void PacketMine::onEnable() {
    clearTarget();
    mRenderProgress = 0.0f;

    Listen(this, StartDestroyBlockEvent, &PacketMine::onStartDestroyBlock)
    Listen(this, StopDestroyBlockEvent, &PacketMine::onStopDestroyBlock)
    Listen(this, TickEvent, &PacketMine::onTick)
    Listen(this, Render3DEvent, &PacketMine::onRender3D)
    Listen(this, PacketSendEvent, &PacketMine::onPacketSend)

    debugTrace("enabled");
    Module::onEnable();
}

void PacketMine::onDisable() {
    Deafen(this, StartDestroyBlockEvent, &PacketMine::onStartDestroyBlock)
    Deafen(this, StopDestroyBlockEvent, &PacketMine::onStopDestroyBlock)
    Deafen(this, TickEvent, &PacketMine::onTick)
    Deafen(this, Render3DEvent, &PacketMine::onRender3D)
    Deafen(this, PacketSendEvent, &PacketMine::onPacketSend)

    clearTarget();
    mRenderProgress = 0.0f;

    debugTrace("disabled");
    Module::onDisable();
}

void PacketMine::defaultConfig() {
    Module::defaultConfig("core");
    setDef("range", 6.0f);
    setDef("speedMultiplier", 1.0f);
    setDef("autoTool", true);
    setDef("silentSwitch", true);
    setDef("switchBack", true);
    setDef("continueMining", false);
    setDef("continueProgress", 0.05f);
    setDef("rotateNearBreak", false);
    setDef("rotationTriggerProgress", 0.88f);

    setDef("render3d", true);
    setDef("renderFilled", true);
    setDef("renderOutline", true);
    setDef("filledAlpha", 0.20f);
    setDef("outlineAlpha", 0.90f);
    setDef("renderPadding", 0.02f);
    setDef("debugLogs", true);
    setDef("debugRenderLogs", true);
    setDef("debugRotationLogs", true);
    setDef("debugLogIntervalMs", 120.0f);
}

void PacketMine::settingsRender(float settingsOffset) {
    (void)settingsOffset;
    initSettingsPage();

    addHeader("Mining");
    addSlider("Range", "Maximum distance from the target block.", "range", 12.0f, 1.0f);
    addSlider("Speed Multiplier", "Multiplier applied to PacketMine progress per tick.", "speedMultiplier", 5.0f, 0.1f, false);
    addToggle("Auto Tool", "Finds best hotbar slot via safe tool heuristic.", "autoTool");
    addConditionalToggle(getOps<bool>("autoTool"), "Silent Switch", "Switches slot only while breaking.", "silentSwitch");
    addConditionalToggle(
        getOps<bool>("autoTool") && getOps<bool>("silentSwitch"),
        "Switch Back",
        "Restores old slot after silent break.",
        "switchBack"
    );
    addToggle("Continue Mining", "Keeps packet-mining state after a break attempt.", "continueMining");
    addConditionalSlider(
        getOps<bool>("continueMining"),
        "Continue Progress",
        "Progress reset value when continue mode is enabled.",
        "continueProgress",
        1.0f,
        0.0f
    );
    addToggle("Rotate Near Break", "Queues a server-side look rotation right before block break.", "rotateNearBreak");
    addConditionalSlider(
        getOps<bool>("rotateNearBreak"),
        "Rotation Trigger",
        "Mine progress threshold to queue rotation.",
        "rotationTriggerProgress",
        1.0f,
        0.50f
    );
    extraPadding();

    addHeader("Render");
    addToggle("Render 3D Progress", "Renders PacketMine progress inside target block.", "render3d");
    addConditionalToggle(getOps<bool>("render3d"), "Render Fill", "", "renderFilled");
    addConditionalToggle(getOps<bool>("render3d"), "Render Outline", "", "renderOutline");
    addConditionalSlider(
        getOps<bool>("render3d") && getOps<bool>("renderFilled"),
        "Fill Alpha",
        "",
        "filledAlpha",
        1.0f,
        0.0f
    );
    addConditionalSlider(
        getOps<bool>("render3d") && getOps<bool>("renderOutline"),
        "Outline Alpha",
        "",
        "outlineAlpha",
        1.0f,
        0.0f
    );
    addConditionalSlider(getOps<bool>("render3d"), "Padding", "Inset from block edges.", "renderPadding", 0.20f, 0.0f);
    extraPadding();

    addHeader("Debug");
    addToggle("Debug Logs", "Enable detailed PacketMine traces in latest.log / console.", "debugLogs");
    addConditionalToggle(getOps<bool>("debugLogs"), "Debug Render", "Logs render progress and colors.", "debugRenderLogs");
    addConditionalToggle(getOps<bool>("debugLogs"), "Debug Rotation", "Logs outgoing + forced render rotations.", "debugRotationLogs");
    addConditionalSlider(
        getOps<bool>("debugLogs"),
        "Debug Interval Ms",
        "Minimum delay between repeated traces.",
        "debugLogIntervalMs",
        1000.0f,
        10.0f,
        false
    );

    FlarialGUI::UnsetScrollView();
    resetPadding();
}

void PacketMine::onStartDestroyBlock(StartDestroyBlockEvent& event) {
    if (!this->isEnabled()) return;
    if (mSuppressMineEvents) return;
    if (!event.getStarted()) return;

    const BlockPos& newPos = event.getBlockPos();
    const uint8_t newFace = event.getFace();
    Gamemode* newGamemode = event.getGamemode();

    const bool samePos =
        newPos.x == mTargetPos.x &&
        newPos.y == mTargetPos.y &&
        newPos.z == mTargetPos.z;

    if (mHasTarget && samePos) {
        if (newGamemode != nullptr) {
            mTargetGamemode = newGamemode;
        }
        mTargetFace = newFace;
        mMiningPrimed = true;
        if (shouldDebugLog("tick", 70)) {
            debugTrace(std::format(
                "startDestroy duplicate keepalive pos=({}, {}, {}) face={} progress={:.3f}",
                mTargetPos.x,
                mTargetPos.y,
                mTargetPos.z,
                static_cast<int>(mTargetFace),
                mMineProgress
            ));
        }
        event.cancel();
        return;
    }

    if (mHasTarget && !samePos) {
        clearTarget("retarget from startDestroy");
    }

    mTargetPos = newPos;
    mTargetFace = newFace;
    mTargetGamemode = newGamemode;
    mMineProgress = 0.0f;
    mRenderProgress = 0.0f;
    mHasQueuedRotation = false;
    mQueuedRotationTicks = 0;
    mMiningPrimed = true;
    mFailedBreakTicks = 0;
    mHasTarget = true;

    debugTrace(std::format(
        "startDestroy pos=({}, {}, {}) face={} gm={} started={}",
        mTargetPos.x,
        mTargetPos.y,
        mTargetPos.z,
        static_cast<int>(mTargetFace),
        reinterpret_cast<void*>(mTargetGamemode),
        event.getStarted()
    ));

    event.cancel();
}

void PacketMine::onStopDestroyBlock(StopDestroyBlockEvent& event) {
    if (!this->isEnabled()) return;
    if (mSuppressMineEvents) return;
    if (!mHasTarget) return;
    (void)event;

    debugTrace("stopDestroy intercepted and cancelled");

    event.cancel();
}

void PacketMine::onTick(TickEvent& event) {
    if (!this->isEnabled()) return;
    if (!mHasTarget) return;
    if (!SDK::hasInstanced || SDK::clientInstance == nullptr) return;

    auto* localPlayer = SDK::clientInstance->getLocalPlayer();
    if (localPlayer == nullptr || event.getActor() != localPlayer) return;

    auto* gamemode = localPlayer->getGamemode();
    auto* supplies = localPlayer->getSupplies();
    auto* blockSource = SDK::clientInstance->getBlockSource();
    if (gamemode == nullptr || supplies == nullptr || blockSource == nullptr) {
        clearTarget("missing local context");
        return;
    }

    if (mTargetGamemode != nullptr && gamemode != mTargetGamemode) {
        clearTarget("gamemode changed");
        return;
    }

    if (gamemode->getPlayer() != localPlayer) {
        clearTarget("gamemode owner mismatch");
        return;
    }

    if (!isTargetInRange()) {
        clearTarget("target out of range");
        return;
    }

    Block* block = blockSource->getBlock(mTargetPos);
    if (isAirBlock(block)) {
        if (getOps<bool>("continueMining")) {
            mMineProgress = std::clamp(getOps<float>("continueProgress"), 0.0f, 1.0f);
            debugTrace("target is air, continueMining keeps progress");
        } else {
            clearTarget("target became air");
        }
        return;
    }

    auto [bestSlot, toolScore] = getBestHotbarSlot(block);
    const float progressStep = std::clamp(
        (0.004f + (toolScore * 0.006f)) * getOps<float>("speedMultiplier"),
        0.0015f,
        0.08f
    );

    mMineProgress = std::clamp(mMineProgress + progressStep, 0.0f, 1.0f);

    if (mHasTarget) {
        Vec2<float> targetRot{};
        if (buildRotationToTarget(targetRot)) {
            ForcedRenderRotationState::set(targetRot, targetRot.y);
            if (getOps<bool>("debugRotationLogs") && shouldDebugLog("rotation", 60)) {
                debugTrace(std::format("tick rotation target pitch={:.2f} yaw={:.2f}", targetRot.x, targetRot.y));
            }
        }
    }

    if (shouldDebugLog("tick", 80)) {
        debugTrace(std::format(
            "tick progress={:.3f} step={:.4f} slot={} toolScore={:.3f} target=({}, {}, {})",
            mMineProgress,
            progressStep,
            bestSlot,
            toolScore,
            mTargetPos.x,
            mTargetPos.y,
            mTargetPos.z
        ));
    }

    if (mMineProgress < 1.0f) {
        return;
    }

    const int previousSlot = supplies->getSelectedSlot();
    const bool useAutoTool = getOps<bool>("autoTool") && bestSlot >= 0 && bestSlot < 9;
    const bool silentSwitch = useAutoTool && getOps<bool>("silentSwitch");

    if (useAutoTool && !silentSwitch && bestSlot != previousSlot) {
        supplies->setSelectedSlot(bestSlot);
    }

    if (silentSwitch && bestSlot != previousSlot) {
        supplies->setSelectedSlot(bestSlot);
    }

    const uint8_t safeFace = static_cast<uint8_t>(std::clamp(static_cast<int>(mTargetFace), 0, 5));
    bool hasDestroyedBlock = false;
    bool startOk = true;
    bool continueOk = false;
    bool destroyOk = false;
    bool stopSent = false;
    Vec3<float> playerPos{};
    if (localPlayer->getPosition() != nullptr) {
        playerPos = *localPlayer->getPosition();
    }
    const int continueVft = GET_OFFSET("Gamemode::continueDestroyBlockVft");
    const int destroyVft = GET_OFFSET("Gamemode::destroyBlockVft");
    const int stopVft = GET_OFFSET("Gamemode::stopDestroyBlockVft");
    static bool disableDestroyFallback = false;

    {
        ScopedBool suppress(mSuppressMineEvents);
        if (!mMiningPrimed) {
            startOk = gamemode->startDestroyBlock(mTargetPos, safeFace, hasDestroyedBlock);
            mMiningPrimed = startOk || hasDestroyedBlock;
        }

        if (mMiningPrimed && !hasDestroyedBlock && isGamemodeVftCallable(gamemode, continueVft > 0 ? continueVft : 3)) {
            const int attempts = std::clamp(
                static_cast<int>(std::round(1.0f + getOps<float>("speedMultiplier") * 2.0f + toolScore * 0.8f)),
                1,
                14
            );
            for (int i = 0; i < attempts; i++) {
                continueOk = gamemode->continueDestroyBlock(mTargetPos, safeFace, playerPos, hasDestroyedBlock) || continueOk;
                if (hasDestroyedBlock) break;
            }
        }

        if (
            !hasDestroyedBlock &&
            !disableDestroyFallback &&
            mFailedBreakTicks >= 4 &&
            isGamemodeVftCallable(gamemode, destroyVft > 0 ? destroyVft : 2)
        ) {
            bool hadAccessViolation = false;
            destroyOk = tryDestroyBlockSafe(gamemode, mTargetPos, safeFace, hadAccessViolation);
            if (hadAccessViolation) {
                disableDestroyFallback = true;
                debugTrace("destroyBlock fallback threw AV, disabling fallback");
            }
        }

        if (!hasDestroyedBlock && !destroyOk && isGamemodeVftCallable(gamemode, stopVft > 0 ? stopVft : 4)) {
            gamemode->stopDestroyBlock(mTargetPos);
            stopSent = true;
        }
    }

    debugTrace(std::format(
        "break attempt primed={} startOk={} continueOk={} destroyOk={} stopSent={} hasDestroyed={} face={} gm={}",
        mMiningPrimed,
        startOk,
        continueOk,
        destroyOk,
        stopSent,
        hasDestroyedBlock,
        static_cast<int>(safeFace),
        reinterpret_cast<void*>(gamemode)
    ));

    if (!mMiningPrimed && !hasDestroyedBlock) {
        mMineProgress = std::min(mMineProgress, 0.95f);
        debugTrace("break rejected: not primed");
        return;
    }

    Block* postBlock = blockSource->getBlock(mTargetPos);
    const bool brokenNow = hasDestroyedBlock || destroyOk || isAirBlock(postBlock);
    if (!brokenNow) {
        mMineProgress = 1.0f;
        mFailedBreakTicks++;
        if (stopSent) {
            mMiningPrimed = false;
        }
        if (mFailedBreakTicks > 8) {
            mMiningPrimed = false;
            mFailedBreakTicks = 0;
            debugTrace("stuck while continuing, reprobe startDestroy");
        } else {
            debugTrace("continue sent but block not broken yet; keeping target");
        }
        return;
    }

    mFailedBreakTicks = 0;

    if (silentSwitch && getOps<bool>("switchBack")) {
        supplies->setSelectedSlot(previousSlot);
    }

    if (getOps<bool>("continueMining")) {
        mMineProgress = std::clamp(getOps<float>("continueProgress"), 0.0f, 1.0f);
    } else {
        clearTarget();
    }
}

void PacketMine::onPacketSend(PacketSendEvent& event) {
    if (!this->isEnabled()) return;

    Packet* packet = event.getPacket();
    if (packet == nullptr) return;
    if (packet->getId() != MinecraftPacketIds::PlayerAuthInputPacket) return;

    auto* authInput = reinterpret_cast<PlayerAuthInputPacket*>(packet);
    auto& rot = authInput->getRot();

    if (mHasTarget) {
        Vec2<float> targetRot{};
        if (buildRotationToTarget(targetRot)) {
            rot.x = std::clamp(targetRot.x, -90.0f, 90.0f);
            rot.y = normalizeYawDegrees(targetRot.y);
            authInput->getYHeadRot() = rot.y;
            if (getOps<bool>("debugRotationLogs") && shouldDebugLog("packet", 40)) {
                debugTrace(std::format("packet face-target pitch={:.2f} yaw={:.2f}", rot.x, rot.y));
            }
        }
    } else if (mHasQueuedRotation) {
        rot.x = std::clamp(mQueuedRotation.x, -90.0f, 90.0f);
        rot.y = normalizeYawDegrees(mQueuedRotation.y);
        authInput->getYHeadRot() = rot.y;
        mHasQueuedRotation = false;
        mQueuedRotationTicks = 0;
    }

    ForcedRenderRotationState::set(rot, authInput->getYHeadRot());
}

void PacketMine::onRender3D(Render3DEvent& event) {
    if (!this->isEnabled()) return;
    if (!getOps<bool>("render3d")) return;

    const float progressTarget = std::clamp(mHasTarget ? mMineProgress : 0.0f, 0.0f, 1.0f);
    FlarialGUI::lerp(mRenderProgress, progressTarget, 0.35f * FlarialGUI::frameFactor);
    mRenderProgress = std::clamp(mRenderProgress, 0.0f, 1.0f);

    if (!mHasTarget && mRenderProgress < 0.001f) {
        mRenderProgress = 0.0f;
        return;
    }

    const float pad = std::clamp(getOps<float>("renderPadding"), 0.0f, 0.49f);
    const float x0 = static_cast<float>(mTargetPos.x) + pad;
    const float y0 = static_cast<float>(mTargetPos.y) + pad;
    const float z0 = static_cast<float>(mTargetPos.z) + pad;
    const float x1 = static_cast<float>(mTargetPos.x + 1) - pad;
    const float y1 = static_cast<float>(mTargetPos.y + 1) - pad;
    const float z1 = static_cast<float>(mTargetPos.z + 1) - pad;
    const float fy = y0 + (y1 - y0) * mRenderProgress;

    const float t = mRenderProgress;
    const auto lerpFloat = [](float a, float b, float pct) {
        return a + (b - a) * pct;
    };

    const D2D_COLOR_F startBlue{0.15f, 0.62f, 1.0f, 1.0f};
    const D2D_COLOR_F midRed{1.0f, 0.18f, 0.18f, 1.0f};
    const D2D_COLOR_F endGreen{0.20f, 1.0f, 0.25f, 1.0f};

    D2D_COLOR_F rgb{startBlue.r, startBlue.g, startBlue.b, 1.0f};
    if (t < 0.5f) {
        const float pct = t * 2.0f;
        rgb.r = lerpFloat(startBlue.r, midRed.r, pct);
        rgb.g = lerpFloat(startBlue.g, midRed.g, pct);
        rgb.b = lerpFloat(startBlue.b, midRed.b, pct);
    } else {
        const float pct = (t - 0.5f) * 2.0f;
        rgb.r = lerpFloat(midRed.r, endGreen.r, pct);
        rgb.g = lerpFloat(midRed.g, endGreen.g, pct);
        rgb.b = lerpFloat(midRed.b, endGreen.b, pct);
    }

    const D2D_COLOR_F outlineColor{
        std::clamp(rgb.r, 0.0f, 1.0f),
        std::clamp(rgb.g, 0.0f, 1.0f),
        std::clamp(rgb.b, 0.0f, 1.0f),
        std::clamp(getOps<float>("outlineAlpha"), 0.0f, 1.0f)
    };
    const D2D_COLOR_F fillColor{
        outlineColor.r,
        outlineColor.g,
        outlineColor.b,
        std::clamp(getOps<float>("filledAlpha"), 0.0f, 1.0f)
    };

    bool usedFill = false;
    bool usedOutline = false;

    MCDrawUtil3D fillDrawer(event.LevelRenderer, event.screenContext, MaterialUtils::getUIFillColor());
    MCDrawUtil3D lineDrawer(event.LevelRenderer, event.screenContext, MaterialUtils::getUIFillColor());

    if (getOps<bool>("renderFilled") && fy > y0 + 0.0001f) {
        const Vec3<float> l000{x0, y0, z0};
        const Vec3<float> l001{x0, y0, z1};
        const Vec3<float> l100{x1, y0, z0};
        const Vec3<float> l101{x1, y0, z1};
        const Vec3<float> u000{x0, fy, z0};
        const Vec3<float> u001{x0, fy, z1};
        const Vec3<float> u100{x1, fy, z0};
        const Vec3<float> u101{x1, fy, z1};

        fillDrawer.fillQuad(l000, l100, l101, l001, fillColor);
        fillDrawer.fillQuad(u000, u100, u101, u001, fillColor);
        fillDrawer.fillQuad(l000, l100, u100, u000, fillColor);
        fillDrawer.fillQuad(l001, l101, u101, u001, fillColor);
        fillDrawer.fillQuad(l000, l001, u001, u000, fillColor);
        fillDrawer.fillQuad(l100, l101, u101, u100, fillColor);
        usedFill = true;
    }

    if (getOps<bool>("renderOutline")) {
        const Vec3<float> b000{x0, y0, z0};
        const Vec3<float> b001{x0, y0, z1};
        const Vec3<float> b010{x0, y1, z0};
        const Vec3<float> b011{x0, y1, z1};
        const Vec3<float> b100{x1, y0, z0};
        const Vec3<float> b101{x1, y0, z1};
        const Vec3<float> b110{x1, y1, z0};
        const Vec3<float> b111{x1, y1, z1};

        lineDrawer.drawLineList(b000, b100, outlineColor);
        lineDrawer.drawLineList(b100, b101, outlineColor);
        lineDrawer.drawLineList(b101, b001, outlineColor);
        lineDrawer.drawLineList(b001, b000, outlineColor);

        lineDrawer.drawLineList(b010, b110, outlineColor);
        lineDrawer.drawLineList(b110, b111, outlineColor);
        lineDrawer.drawLineList(b111, b011, outlineColor);
        lineDrawer.drawLineList(b011, b010, outlineColor);

        lineDrawer.drawLineList(b000, b010, outlineColor);
        lineDrawer.drawLineList(b100, b110, outlineColor);
        lineDrawer.drawLineList(b101, b111, outlineColor);
        lineDrawer.drawLineList(b001, b011, outlineColor);

        if (fy > y0 + 0.0001f && fy < y1 - 0.0001f) {
            const Vec3<float> p0{x0, fy, z0};
            const Vec3<float> p1{x1, fy, z0};
            const Vec3<float> p2{x1, fy, z1};
            const Vec3<float> p3{x0, fy, z1};
            lineDrawer.drawLineList(p0, p1, outlineColor);
            lineDrawer.drawLineList(p1, p2, outlineColor);
            lineDrawer.drawLineList(p2, p3, outlineColor);
            lineDrawer.drawLineList(p3, p0, outlineColor);
        }

        usedOutline = true;
    }

    if (usedFill) fillDrawer.flush();
    if (usedOutline) lineDrawer.flush();

    if (getOps<bool>("debugLogs") && getOps<bool>("debugRenderLogs") && shouldDebugLog("render", 120)) {
        debugTrace(std::format(
            "render progress={:.3f} fill={} outline={} color=({:.2f},{:.2f},{:.2f}) box=({}, {}, {})",
            mRenderProgress,
            getOps<bool>("renderFilled"),
            getOps<bool>("renderOutline"),
            rgb.r,
            rgb.g,
            rgb.b,
            mTargetPos.x,
            mTargetPos.y,
            mTargetPos.z
        ));
    }
}

bool PacketMine::isTargetInRange() {
    if (SDK::clientInstance == nullptr) return false;

    auto* localPlayer = SDK::clientInstance->getLocalPlayer();
    if (localPlayer == nullptr || localPlayer->getPosition() == nullptr) return false;

    Vec3<float> targetCenter{
        static_cast<float>(mTargetPos.x) + 0.5f,
        static_cast<float>(mTargetPos.y) + 0.5f,
        static_cast<float>(mTargetPos.z) + 0.5f
    };

    return localPlayer->getPosition()->dist(targetCenter) <= getOps<float>("range");
}

bool PacketMine::isAirBlock(const Block* block) {
    if (block == nullptr) return true;

    auto* legacy = const_cast<Block*>(block)->getBlockLegacy();
    if (legacy == nullptr) return true;

    std::string name = toLower(legacy->getName());
    return name == "air" || name == "cave_air" || name == "void_air" || name.ends_with("_air");
}

std::string PacketMine::normalizeBlockName(const Block* block) {
    if (block == nullptr) return {};
    auto* legacy = const_cast<Block*>(block)->getBlockLegacy();
    if (legacy == nullptr) return {};

    std::string name = toLower(legacy->getName());
    if (name.rfind("minecraft:", 0) == 0) name.erase(0, 10);
    if (name.rfind("tile.", 0) == 0) name.erase(0, 5);
    return name;
}

float PacketMine::getToolTierScore(const std::string& itemName) {
    if (itemName.empty()) return 0.20f;
    if (isShears(itemName)) return 3.50f;

    const bool tool = isPickaxe(itemName) || isAxe(itemName) || isShovel(itemName) || isHoe(itemName);
    if (!tool) return 0.20f;

    if (itemName.find("netherite_") != std::string::npos) return 6.00f;
    if (itemName.find("golden_") != std::string::npos) return 5.50f;
    if (itemName.find("diamond_") != std::string::npos) return 5.00f;
    if (itemName.find("iron_") != std::string::npos) return 4.00f;
    if (itemName.find("stone_") != std::string::npos) return 3.00f;
    if (itemName.find("wooden_") != std::string::npos) return 2.00f;
    return 2.50f;
}

float PacketMine::getToolBlockMatchScore(const std::string& itemName, const std::string& blockName) {
    if (itemName.empty()) return 0.20f;
    if (blockName.empty()) return 1.00f;

    const bool pickaxe = isPickaxe(itemName);
    const bool axe = isAxe(itemName);
    const bool shovel = isShovel(itemName);
    const bool hoe = isHoe(itemName);
    const bool shears = isShears(itemName);
    const bool tool = pickaxe || axe || shovel || hoe || shears;

    if (!tool) return 0.20f;

    if (isPickaxeBlock(blockName)) {
        if (pickaxe) return 1.00f;
        if (axe || shovel || hoe) return 0.45f;
        if (shears) return 0.20f;
        return 0.15f;
    }

    if (isAxeBlock(blockName)) {
        if (axe) return 1.00f;
        if (pickaxe || shovel || hoe) return 0.45f;
        if (shears) return 0.35f;
        return 0.15f;
    }

    if (isShovelBlock(blockName)) {
        if (shovel) return 1.00f;
        if (pickaxe || axe || hoe) return 0.45f;
        if (shears) return 0.20f;
        return 0.15f;
    }

    if (isHoeBlock(blockName)) {
        if (hoe) return 1.00f;
        if (pickaxe || axe || shovel) return 0.45f;
        if (shears) return 1.15f;
        return 0.15f;
    }

    if (blockName.find("leaves") != std::string::npos && shears) {
        return 1.20f;
    }

    return 0.50f;
}

bool PacketMine::buildRotationToTarget(Vec2<float>& outRotation) const {
    if (!mHasTarget) return false;
    if (SDK::clientInstance == nullptr) return false;

    auto* localPlayer = SDK::clientInstance->getLocalPlayer();
    if (localPlayer == nullptr || localPlayer->getPosition() == nullptr) return false;

    Vec3<float> eyePos = *localPlayer->getPosition();
    eyePos.y += 1.62f;

    Vec3<float> targetCenter{
        static_cast<float>(mTargetPos.x) + 0.5f,
        static_cast<float>(mTargetPos.y) + 0.5f,
        static_cast<float>(mTargetPos.z) + 0.5f
    };

    outRotation = calcRotationTo(eyePos, targetCenter);
    return true;
}

std::pair<int, float> PacketMine::getBestHotbarSlot(const Block* block) {
    if (SDK::clientInstance == nullptr) return {-1, 0.20f};

    auto* localPlayer = SDK::clientInstance->getLocalPlayer();
    if (localPlayer == nullptr) return {-1, 0.20f};

    auto* supplies = localPlayer->getSupplies();
    if (supplies == nullptr) return {-1, 0.20f};

    const int currentSlot = std::clamp(supplies->getSelectedSlot(), 0, 8);
    if (!getOps<bool>("autoTool")) {
        return {currentSlot, 1.00f};
    }

    auto* inventory = supplies->getInventory();
    if (inventory == nullptr) return {currentSlot, 1.00f};

    const std::string blockName = normalizeBlockName(block);

    int bestSlot = currentSlot;
    float bestScore = 0.20f;

    for (int slot = 0; slot < 9; slot++) {
        ItemStack* stack = inventory->getItem(slot);
        Item* item = stack != nullptr ? stack->getItem() : nullptr;
        const std::string itemName = item != nullptr ? toLower(item->name) : std::string{};

        const float score = getToolTierScore(itemName) * getToolBlockMatchScore(itemName, blockName);
        if (score > bestScore) {
            bestScore = score;
            bestSlot = slot;
        }
    }

    return {bestSlot, std::max(bestScore, 0.20f)};
}

bool PacketMine::shouldDebugLog(const char* key, uint64_t minDelayMs) {
    if (!getOps<bool>("debugLogs")) return false;

    const uint64_t now = Utils::getCurrentMs();
    const uint64_t delay = static_cast<uint64_t>(std::clamp(getOps<float>("debugLogIntervalMs"), 10.0f, 3000.0f));
    const uint64_t effectiveDelay = std::max(delay, minDelayMs);

    uint64_t* lastLog = &mLastDebugTickLogMs;
    if (std::strcmp(key, "render") == 0) lastLog = &mLastDebugRenderLogMs;
    else if (std::strcmp(key, "packet") == 0) lastLog = &mLastDebugPacketLogMs;
    else if (std::strcmp(key, "rotation") == 0) lastLog = &mLastDebugRotationLogMs;

    if (now - *lastLog < effectiveDelay) return false;
    *lastLog = now;
    return true;
}

void PacketMine::debugTrace(const std::string& message) {
    if (!getOps<bool>("debugLogs")) return;
    Logger::custom(fg(fmt::color::deep_sky_blue), "PacketMineDBG", "{}", message);
}

void PacketMine::clearTarget(const char* reason) {
    if (reason != nullptr && getOps<bool>("debugLogs")) {
        Logger::custom(fg(fmt::color::orange), "PacketMineDBG", "clearTarget reason={}", reason);
    }

    if (mMiningPrimed && SDK::clientInstance != nullptr) {
        auto* localPlayer = SDK::clientInstance->getLocalPlayer();
        auto* gm = localPlayer != nullptr ? localPlayer->getGamemode() : nullptr;
        if (gm != nullptr && (mTargetGamemode == nullptr || gm == mTargetGamemode)) {
            ScopedBool suppress(mSuppressMineEvents);
            gm->stopDestroyBlock(mTargetPos);
        }
    }

    mTargetPos = {};
    mTargetFace = 0;
    mHasTarget = false;
    mTargetGamemode = nullptr;
    mSuppressMineEvents = false;
    mMineProgress = 0.0f;
    mRenderProgress = 0.0f;
    mQueuedRotation = Vec2<float>(0.0f, 0.0f);
    mHasQueuedRotation = false;
    mQueuedRotationTicks = 0;
    mMiningPrimed = false;
    mFailedBreakTicks = 0;
    ForcedRenderRotationState::clear();
}
