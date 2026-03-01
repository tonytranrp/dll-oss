#pragma once

#include "../Module.hpp"
#include "Assets/Assets.hpp"
#include "Events/Network/PacketSendEvent.hpp"
#include "SDK/Client/Network/Packet/InventoryTransactionPacket.hpp"

class PacketTest : public Module {
public:
    PacketTest() : Module(
                       "Packet Test",
                       "Logs outgoing inventory transaction packets and transaction payload fields.",
                       IDR_SCRIPT_PNG,
                       "",
                       false,
                       {"packet logger", "inventory packet"}
                   ) {}

    void onEnable() override;
    void onDisable() override;
    void defaultConfig() override;
    void onPacketSend(PacketSendEvent& event);
};
