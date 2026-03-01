#pragma once

#include "../Module.hpp"
#include "Events/Game/ContainerScreenControllerTickEvent.hpp"
#include "Events/Game/ContainerSlotHoveredEvent.hpp"
#include "Events/Game/TickEvent.hpp"
#include "Events/Network/PacketEvent.hpp"

class AutoTotem : public Module {
private:
    std::string mDetectedOffhandCollection;
    int mSwapCooldown = 0;
    bool mSwapQueued = false;
    std::string mPendingSrcCollection;
    int mPendingSrcSlot = -1;
    std::string mPendingDstCollection;
    int mPendingDstSlot = 0;
    bool mSilentContextOpen = false;
    int mSilentWaitTicks = 0;

public:
    AutoTotem()
        : Module(
              "Auto Totem",
              "Automatically moves a totem to your offhand.",
              IDR_TOTEM_PNG,
              "",
              false,
              {"offhand", "totem", "inventory"}
          ) {}

    void onEnable() override;
    void onDisable() override;
    void defaultConfig() override;
    void settingsRender(float settingsOffset) override;

    void onTick(TickEvent& event);
    void onPacketReceive(PacketEvent& event);
    void onContainerTick(ContainerScreenControllerTickEvent& event);
    void onContainerSlotHovered(ContainerSlotHoveredEvent& event);
};
