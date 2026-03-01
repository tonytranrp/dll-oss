#include "PacketTest.hpp"

#include "Utils/Logger/Logger.hpp"

namespace {
void logPosition(std::string_view label, const Vec3<float>& pos) {
    Logger::info("[Packet Test] {}=({}, {}, {})", label, pos.x, pos.y, pos.z);
}

void logBlockPosition(std::string_view label, const BlockPos& pos) {
    Logger::info("[Packet Test] {}=({}, {}, {})", label, pos.x, pos.y, pos.z);
}

void logTransactionFields(const ComplexInventoryTransactionLayout* transaction) {
    if (transaction == nullptr) {
        Logger::info("[Packet Test] complexTransaction=null");
        return;
    }

    Logger::info(
        "[Packet Test] complexType={}",
        InventoryTransactionPacketUtils::toString(transaction->type)
    );

    switch (transaction->type) {
        case ComplexInventoryTransactionType::ItemUseTransaction: {
            const auto* typed = reinterpret_cast<const ItemUseInventoryTransactionLayout*>(transaction);
            Logger::info(
                "[Packet Test] itemUse actionType={} triggerType={} targetBlockId={} face={} slot={} predicted={}",
                InventoryTransactionPacketUtils::toString(typed->actionType),
                InventoryTransactionPacketUtils::toString(typed->triggerType),
                typed->targetBlockId,
                typed->face,
                typed->slot,
                InventoryTransactionPacketUtils::toString(typed->clientPredictedResult)
            );
            logBlockPosition("itemUse.pos", typed->pos);
            logPosition("itemUse.fromPos", typed->fromPos);
            logPosition("itemUse.clickPos", typed->clickPos);
            break;
        }
        case ComplexInventoryTransactionType::ItemUseOnEntityTransaction: {
            const auto* typed = reinterpret_cast<const ItemUseOnActorInventoryTransactionLayout*>(transaction);
            Logger::info(
                "[Packet Test] itemUseOnActor actionType={} runtimeId={} slot={}",
                InventoryTransactionPacketUtils::toString(typed->actionType),
                typed->runtimeId,
                typed->slot
            );
            logPosition("itemUseOnActor.fromPos", typed->fromPos);
            logPosition("itemUseOnActor.hitPos", typed->hitPos);
            break;
        }
        case ComplexInventoryTransactionType::ItemReleaseTransaction: {
            const auto* typed = reinterpret_cast<const ItemReleaseInventoryTransactionLayout*>(transaction);
            Logger::info(
                "[Packet Test] itemRelease actionType={} slot={}",
                InventoryTransactionPacketUtils::toString(typed->actionType),
                typed->slot
            );
            logPosition("itemRelease.fromPos", typed->fromPos);
            break;
        }
        case ComplexInventoryTransactionType::NormalTransaction:
        case ComplexInventoryTransactionType::InventoryMismatch:
            Logger::info("[Packet Test] no extra fields for this complex transaction type");
            break;
        default:
            Logger::info("[Packet Test] unknown complex transaction type");
            break;
    }
}
} // namespace

void PacketTest::onEnable() {
    Listen(this, PacketSendEvent, &PacketTest::onPacketSend)
    Module::onEnable();
}

void PacketTest::onDisable() {
    Deafen(this, PacketSendEvent, &PacketTest::onPacketSend)
    Module::onDisable();
}

void PacketTest::defaultConfig() {
    Module::defaultConfig("core");
    setDef("logLegacySetSlots", true);
    setDef("logTransactionFields", true);
}

void PacketTest::onPacketSend(PacketSendEvent& event) {
    if (!this->isEnabled()) return;

    Packet* packet = event.getPacket();
    if (packet == nullptr) return;
    if (packet->getId() != MinecraftPacketIds::InventoryTransaction) return;

    const auto* inventoryPacket = reinterpret_cast<const InventoryTransactionPacket*>(packet);

    Logger::info("[Packet Test] we have inventory packet sending out");
    Logger::info(
        "[Packet Test] legacyRequestRawId={} legacySetItemSlots={} isClientSide={}",
        inventoryPacket->getLegacyRequestRawId(),
        inventoryPacket->mLegacySetItemSlots.size(),
        inventoryPacket->mIsClientSide
    );

    if (getOps<bool>("logLegacySetSlots")) {
        for (size_t i = 0; i < inventoryPacket->mLegacySetItemSlots.size(); i++) {
            const auto& legacySetSlot = inventoryPacket->mLegacySetItemSlots[i];
            Logger::info(
                "[Packet Test] legacySetItemSlots[{}]: container={} slotCount={}",
                i,
                static_cast<int>(legacySetSlot.first),
                legacySetSlot.second.size()
            );
        }
    }

    if (getOps<bool>("logTransactionFields")) {
        logTransactionFields(inventoryPacket->mTransaction.get());
    }
}
