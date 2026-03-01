#include "AutoTotem.hpp"

#include "Events/EventManager.hpp"
#include "Events/Events.hpp"
#include "SDK/SDK.hpp"
#include "SDK/Client/Network/Packet/ContainerClosePacket.hpp"
#include "SDK/Client/Network/Packet/ContainerOpenPacket.hpp"
#include "SDK/Client/Network/Packet/InteractPacket.hpp"
#include "Utils/Logger/Logger.hpp"

#include <algorithm>

namespace {
bool isTotem(const ItemStack* stack) {
    return stack != nullptr && stack->getItem() != nullptr && stack->getItem()->name == "totem_of_undying";
}

int findTotemSlot(Inventory* inventory, bool preferHotbar) {
    if (inventory == nullptr) return -1;

    auto scanRange = [inventory](int begin, int end) -> int {
        for (int i = begin; i < end; i++) {
            ItemStack* stack = inventory->getItem(i);
            if (isTotem(stack)) return i;
        }
        return -1;
    };

    if (preferHotbar) {
        int slot = scanRange(0, 9);
        return slot != -1 ? slot : scanRange(9, 36);
    }

    int slot = scanRange(9, 36);
    return slot != -1 ? slot : scanRange(0, 9);
}

bool isScreenSafeForContainerActions() {
    const std::string screen = SDK::getCurrentScreen();
    return screen != "hud_screen" && screen != "pause_screen";
}

bool isValidSourceCollection(std::string_view collectionName, int slot) {
    if (collectionName == "hotbar_items") return slot >= 0 && slot <= 8;
    if (collectionName == "inventory_items") return slot >= 0 && slot <= 26;
    return false;
}

bool openSilentInventoryContext(bool debug) {
    if (SDK::clientInstance == nullptr) return false;

    auto interactPacket = SDK::createPacket((int)MinecraftPacketIds::Interact);
    if (!interactPacket) return false;

    auto* ip = reinterpret_cast<InteractPacket*>(interactPacket.get());
    ip->Action = InteractPacket::Action::OpenInventory;
    ip->TargetId = 0;
    ip->Pos = Vec3<float>(0.f, 0.f, 0.f);
    SDK::clientInstance->getPacketSender()->sendToServer(ip);

    if (debug) {
        Logger::info("[AutoTotem] requested server inventory open for silent swap");
    }
    return true;
}

void closeSilentInventoryContext(bool debug) {
    if (SDK::clientInstance == nullptr) return;

    auto closePacket = SDK::createPacket((int)MinecraftPacketIds::ContainerClose);
    if (!closePacket) return;

    auto* ccp = reinterpret_cast<ContainerClosePacket*>(closePacket.get());
    ccp->ContainerId = ContainerID::Inventory;
    ccp->ServerInitiatedClose = false;
    SDK::clientInstance->getPacketSender()->sendToServer(ccp);
    SDK::clientInstance->grabMouse();

    if (debug) {
        Logger::info("[AutoTotem] sent silent inventory close");
    }
}
} // namespace

void AutoTotem::onEnable() {
    mDetectedOffhandCollection.clear();
    mSwapCooldown = 0;
    mSwapQueued = false;
    mPendingSrcCollection.clear();
    mPendingSrcSlot = -1;
    mPendingDstCollection.clear();
    mPendingDstSlot = 0;
    mSilentContextOpen = false;
    mSilentWaitTicks = 0;

    Listen(this, TickEvent, &AutoTotem::onTick)
    Listen(this, PacketEvent, &AutoTotem::onPacketReceive)
    Listen(this, ContainerScreenControllerTickEvent, &AutoTotem::onContainerTick)
    Listen(this, ContainerSlotHoveredEvent, &AutoTotem::onContainerSlotHovered)

    Module::onEnable();
}

void AutoTotem::onDisable() {
    Deafen(this, TickEvent, &AutoTotem::onTick)
    Deafen(this, PacketEvent, &AutoTotem::onPacketReceive)
    Deafen(this, ContainerScreenControllerTickEvent, &AutoTotem::onContainerTick)
    Deafen(this, ContainerSlotHoveredEvent, &AutoTotem::onContainerSlotHovered)

    mDetectedOffhandCollection.clear();
    mSwapCooldown = 0;
    mSwapQueued = false;
    mPendingSrcCollection.clear();
    mPendingSrcSlot = -1;
    mPendingDstCollection.clear();
    mPendingDstSlot = 0;
    if (mSilentContextOpen) {
        mSilentContextOpen = false;
        closeSilentInventoryContext(getOps<bool>("debug"));
    }
    mSilentWaitTicks = 0;

    Module::onDisable();
}

void AutoTotem::defaultConfig() {
    Module::defaultConfig("core");
    setDef("preferHotbar", true);
    setDef("replaceOffhandItem", true);
    setDef("mode", std::string("Legit"));
    setDef("offhandCollection", std::string("offhand_items"));
    setDef("offhandSlot", 0);
    setDef("actionDelayTicks", 2);
    setDef("hideSilentOpen", true);
    setDef("debug", false);
}

void AutoTotem::settingsRender(float settingsOffset) {
    initSettingsPage();

    addDropdown(
        "Mode",
        "Legit needs inventory context. Silent uses packet-driven open/swap/close.",
        {"Legit", "Silent"},
        "mode",
        false
    );
    addToggle("Prefer hotbar totems", "", "preferHotbar");
    addToggle("Replace offhand item", "If disabled, only fills when offhand is empty.", "replaceOffhandItem");
    addToggle("Hide silent open", "Hides inventory open/close packets while silent mode runs.", "hideSilentOpen");
    addSliderInt("Action delay (ticks)", "", "actionDelayTicks", 20, 0);
    addTextBox(
        "Offhand collection",
        "Optional manual override; leave empty to use auto-detected offhand collection.",
        64,
        "offhandCollection"
    );
    addSliderInt("Offhand slot", "Usually 0 or 1.", "offhandSlot", 2, 0);
    addToggle("Debug logs", "", "debug");

    FlarialGUI::UnsetScrollView();
    resetPadding();
}

void AutoTotem::onContainerTick(ContainerScreenControllerTickEvent& event) {
    if (!this->isEnabled()) return;
    const bool silentMode = SDK::containsIgnoreCase(getOps<std::string>("mode"), "silent");

    if (mSwapCooldown > 0) {
        mSwapCooldown--;
        return;
    }

    if (!mSwapQueued) return;

    auto* controller = event.getContainerScreenController();
    if (controller == nullptr) {
        if (!silentMode) {
            mSwapQueued = false;
        }
        return;
    }

    if (!isValidSourceCollection(mPendingSrcCollection, mPendingSrcSlot)) {
        mSwapQueued = false;
        if (silentMode && mSilentContextOpen) {
            mSilentContextOpen = false;
            closeSilentInventoryContext(getOps<bool>("debug"));
            mSilentWaitTicks = 0;
        }
        return;
    }

    if (!SDK::containsIgnoreCase(mPendingDstCollection, "offhand")) {
        mSwapQueued = false;
        if (silentMode && mSilentContextOpen) {
            mSilentContextOpen = false;
            closeSilentInventoryContext(getOps<bool>("debug"));
            mSilentWaitTicks = 0;
        }
        return;
    }

    if (!silentMode && !isScreenSafeForContainerActions()) return;

    const int safeDstSlot = std::clamp(mPendingDstSlot, 0, 1);

    controller->swap(mPendingSrcCollection, mPendingSrcSlot, mPendingDstCollection, safeDstSlot);
    mSwapQueued = false;
    mSwapCooldown = std::max(0, getOps<int>("actionDelayTicks"));

    if (silentMode && mSilentContextOpen) {
        mSilentContextOpen = false;
        closeSilentInventoryContext(getOps<bool>("debug"));
        mSilentWaitTicks = 0;
    }

    if (getOps<bool>("debug")) {
        if (silentMode) {
            Logger::info(
                "[AutoTotem] silent swap request src={}#{} -> dst={}#{}",
                mPendingSrcCollection,
                mPendingSrcSlot,
                mPendingDstCollection,
                safeDstSlot
            );
        } else {
            Logger::info(
                "[AutoTotem] swap request src={}#{} -> dst={}#{}",
                mPendingSrcCollection,
                mPendingSrcSlot,
                mPendingDstCollection,
                safeDstSlot
            );
        }
    }
}

void AutoTotem::onContainerSlotHovered(ContainerSlotHoveredEvent& event) {
    if (!this->isEnabled()) return;

    const std::string collectionName = event.getCollectionName();
    if (!SDK::containsIgnoreCase(collectionName, "offhand")) return;

    if (mDetectedOffhandCollection == collectionName) return;
    mDetectedOffhandCollection = collectionName;

    if (getOps<bool>("debug")) {
        Logger::info("[AutoTotem] detected offhand collection={}", mDetectedOffhandCollection);
    }
}

void AutoTotem::onPacketReceive(PacketEvent& event) {
    if (!this->isEnabled()) return;
    const bool silentMode = SDK::containsIgnoreCase(getOps<std::string>("mode"), "silent");
    if (!silentMode) return;
    if (!mSilentContextOpen) return;
    if (!getOps<bool>("hideSilentOpen")) return;

    Packet* packet = event.getPacket();
    if (packet == nullptr) return;

    // Keep this visual-only: do not cancel close packets, or the UI can get stuck open.
    if (packet->getId() == MinecraftPacketIds::ContainerOpen) {
        SDK::clientInstance->grabMouse();
    }
}

void AutoTotem::onTick(TickEvent& event) {
    if (!this->isEnabled()) return;
    if (!SDK::hasInstanced || SDK::clientInstance == nullptr) return;
    const bool silentMode = SDK::containsIgnoreCase(getOps<std::string>("mode"), "silent");

    auto* player = SDK::clientInstance->getLocalPlayer();
    if (player == nullptr) return;
    if (event.getActor() != player) return;

    if (silentMode && mSilentContextOpen && mSwapQueued) {
        if (mSilentWaitTicks > 0) {
            mSilentWaitTicks--;
        } else {
            mSilentContextOpen = false;
            closeSilentInventoryContext(getOps<bool>("debug"));
            mSwapQueued = false;
            if (getOps<bool>("debug")) {
                Logger::warn("[AutoTotem] silent open timed out waiting for container context");
            }
        }
    }

    if (mSwapCooldown > 0) return;

    auto* supplies = player->getSupplies();
    if (supplies == nullptr) return;

    auto* inventory = supplies->getInventory();
    if (inventory == nullptr) return;

    ItemStack* offhandStack = player->getOffhandSlot();
    if (isTotem(offhandStack)) {
        if (silentMode && mSilentContextOpen) {
            mSilentContextOpen = false;
            closeSilentInventoryContext(getOps<bool>("debug"));
            mSwapQueued = false;
            mSilentWaitTicks = 0;
        }
        return;
    }
    if (silentMode && (offhandStack != nullptr && offhandStack->getItem() != nullptr)) {
        if (mSilentContextOpen) {
            mSilentContextOpen = false;
            closeSilentInventoryContext(getOps<bool>("debug"));
            mSwapQueued = false;
            mSilentWaitTicks = 0;
        }
        return;
    }
    if (!getOps<bool>("replaceOffhandItem") && offhandStack != nullptr && offhandStack->getItem() != nullptr) return;

    const int totemSlot = findTotemSlot(inventory, getOps<bool>("preferHotbar"));
    if (totemSlot < 0) return;

    std::string srcCollection = "inventory_items";
    int srcSlot = totemSlot - 9;
    if (totemSlot < 9) {
        srcCollection = "hotbar_items";
        srcSlot = totemSlot;
    }

    const std::string& configuredCollection = getOps<std::string>("offhandCollection");
    const std::string offhandCollection = !mDetectedOffhandCollection.empty()
        ? mDetectedOffhandCollection
        : (configuredCollection.empty() ? std::string("offhand_items") : configuredCollection);

    if (offhandCollection.empty() || !SDK::containsIgnoreCase(offhandCollection, "offhand")) return;
    if (!isValidSourceCollection(srcCollection, srcSlot)) return;

    if (silentMode) {
        if (mSwapQueued || mSilentContextOpen) return;

        mPendingSrcCollection = srcCollection;
        mPendingSrcSlot = srcSlot;
        mPendingDstCollection = offhandCollection;
        mPendingDstSlot = std::clamp(getOps<int>("offhandSlot"), 0, 1);
        mSwapQueued = true;
        mSilentContextOpen = openSilentInventoryContext(getOps<bool>("debug"));
        mSilentWaitTicks = 20;
        if (!mSilentContextOpen) {
            mSwapQueued = false;
            mSilentWaitTicks = 0;
            if (getOps<bool>("debug")) {
                Logger::warn("[AutoTotem] failed to open silent inventory context");
            }
        }
        return;
    }

    if (mSwapQueued) return;

    mPendingSrcCollection = srcCollection;
    mPendingSrcSlot = srcSlot;
    mPendingDstCollection = offhandCollection;
    mPendingDstSlot = std::clamp(getOps<int>("offhandSlot"), 0, 1);
    mSwapQueued = true;
}
