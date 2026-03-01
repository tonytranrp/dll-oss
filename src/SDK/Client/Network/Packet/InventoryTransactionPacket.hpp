#pragma once

#include "Packet.hpp"
#include "Types/ContainerID.hpp"
#include "../../Level/HitResult.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

struct ItemStackLegacyRequestId {
    void** vtable;
    int32_t rawId;
    int32_t _padding;
};

static_assert(sizeof(ItemStackLegacyRequestId) == 0x10);

enum class ContainerEnumName : uint8_t {
    AnvilInputContainer = 0,
    AnvilMaterialContainer = 1,
    AnvilResultPreviewContainer = 2,
    SmithingTableInputContainer = 3,
    SmithingTableMaterialContainer = 4,
    SmithingTableResultPreviewContainer = 5,
    ArmorContainer = 6,
    LevelEntityContainer = 7,
    BeaconPaymentContainer = 8,
    BrewingStandInputContainer = 9,
    BrewingStandResultContainer = 10,
    BrewingStandFuelContainer = 11,
    CombinedHotbarAndInventoryContainer = 12,
    CraftingInputContainer = 13,
    CraftingOutputPreviewContainer = 14,
    RecipeConstructionContainer = 15,
    RecipeNatureContainer = 16,
    RecipeItemsContainer = 17,
    RecipeSearchContainer = 18,
    RecipeSearchBarContainer = 19,
    RecipeEquipmentContainer = 20,
    RecipeBookContainer = 21,
    EnchantingInputContainer = 22,
    EnchantingMaterialContainer = 23,
    FurnaceFuelContainer = 24,
    FurnaceIngredientContainer = 25,
    FurnaceResultContainer = 26,
    HorseEquipContainer = 27,
    HotbarContainer = 28,
    InventoryContainer = 29,
    ShulkerBoxContainer = 30,
    TradeIngredient1Container = 31,
    TradeIngredient2Container = 32,
    TradeResultPreviewContainer = 33,
    OffhandContainer = 34,
    CompoundCreatorInput = 35,
    CompoundCreatorOutputPreview = 36,
    ElementConstructorOutputPreview = 37,
    MaterialReducerInput = 38,
    MaterialReducerOutput = 39,
    LabTableInput = 40,
    LoomInputContainer = 41,
    LoomDyeContainer = 42,
    LoomMaterialContainer = 43,
    LoomResultPreviewContainer = 44,
    BlastFurnaceIngredientContainer = 45,
    SmokerIngredientContainer = 46,
    Trade2Ingredient1Container = 47,
    Trade2Ingredient2Container = 48,
    Trade2ResultPreviewContainer = 49,
    GrindstoneInputContainer = 50,
    GrindstoneAdditionalContainer = 51,
    GrindstoneResultPreviewContainer = 52,
    StonecutterInputContainer = 53,
    StonecutterResultPreviewContainer = 54,
    CartographyInputContainer = 55,
    CartographyAdditionalContainer = 56,
    CartographyResultPreviewContainer = 57,
    BarrelContainer = 58,
    CursorContainer = 59,
    CreatedOutputContainer = 60,
    SmithingTableTemplateContainer = 61,
    CrafterLevelEntityContainer = 62,
    DynamicContainer = 63,
};

enum class InventorySourceType : uint32_t {
    InvalidInventory = 0xFFFFFFFFu,
    ContainerInventory = 0,
    GlobalInventory = 1,
    WorldInteraction = 2,
    CreativeInventory = 3,
    NonImplementedFeatureTODO = 99999,
};

enum class InventorySourceFlags : uint32_t {
    NoFlag = 0,
    WorldInteractionRandom = 1,
};

struct InventorySource {
    InventorySourceType type;
    ContainerID containerId;
    uint8_t _padding0[3];
    InventorySourceFlags flags;
};

static_assert(sizeof(InventorySource) == 0xC);

enum class ComplexInventoryTransactionType : uint32_t {
    NormalTransaction = 0,
    InventoryMismatch = 1,
    ItemUseTransaction = 2,
    ItemUseOnEntityTransaction = 3,
    ItemReleaseTransaction = 4,
};

struct ComplexInventoryTransactionLayout {
    void** vtable;
    ComplexInventoryTransactionType type;
    uint32_t _padding0;
    std::array<std::byte, 0x58> transactionData;
};

static_assert(sizeof(ComplexInventoryTransactionLayout) == 0x68);

enum class ItemUseInventoryTransactionActionType : int32_t {
    Place = 0,
    Use = 1,
    Destroy = 2,
    UseAsAttack = 3,
};

enum class ItemUseInventoryTransactionTriggerType : uint8_t {
    Unknown = 0,
    PlayerInput = 1,
    SimulationTick = 2,
};

enum class ItemUseInventoryTransactionPredictedResult : uint8_t {
    Failure = 0,
    Success = 1,
};

struct ItemUseInventoryTransactionLayout : ComplexInventoryTransactionLayout {
    ItemUseInventoryTransactionActionType actionType;
    ItemUseInventoryTransactionTriggerType triggerType;
    uint8_t _padding1[3];
    BlockPos pos;
    uint32_t targetBlockId;
    uint8_t face;
    uint8_t _padding2[3];
    int32_t slot;
    std::array<std::byte, 0x60> item;
    Vec3<float> fromPos;
    Vec3<float> clickPos;
    ItemUseInventoryTransactionPredictedResult clientPredictedResult;
    uint8_t _padding3[7];
};

static_assert(sizeof(ItemUseInventoryTransactionLayout) == 0x108);

enum class ItemUseOnActorInventoryTransactionActionType : int32_t {
    Interact = 0,
    Attack = 1,
    ItemInteract = 2,
};

struct ItemUseOnActorInventoryTransactionLayout : ComplexInventoryTransactionLayout {
    uint64_t runtimeId;
    ItemUseOnActorInventoryTransactionActionType actionType;
    int32_t slot;
    std::array<std::byte, 0x60> item;
    Vec3<float> fromPos;
    Vec3<float> hitPos;
};

static_assert(sizeof(ItemUseOnActorInventoryTransactionLayout) == 0xF0);

enum class ItemReleaseInventoryTransactionActionType : int32_t {
    Release = 0,
    Use = 1,
};

struct ItemReleaseInventoryTransactionLayout : ComplexInventoryTransactionLayout {
    ItemReleaseInventoryTransactionActionType actionType;
    int32_t slot;
    std::array<std::byte, 0x60> item;
    Vec3<float> fromPos;
    uint8_t _padding1[4];
};

static_assert(sizeof(ItemReleaseInventoryTransactionLayout) == 0xE0);

class InventoryTransactionPacket : public Packet {
public:
    using LegacySetSlot = std::pair<ContainerEnumName, std::vector<unsigned char>>;

    ItemStackLegacyRequestId mLegacyRequestId;
    std::vector<LegacySetSlot> mLegacySetItemSlots;
    std::unique_ptr<ComplexInventoryTransactionLayout> mTransaction;
    bool mIsClientSide;

private:
    uint8_t _padding0[7]{};

public:
    [[nodiscard]] int32_t getLegacyRequestRawId() const {
        return mLegacyRequestId.rawId;
    }
};

static_assert(sizeof(InventoryTransactionPacket) == 0x68);

namespace InventoryTransactionPacketUtils {
inline std::string_view toString(ComplexInventoryTransactionType type) {
    switch (type) {
        case ComplexInventoryTransactionType::NormalTransaction: return "NormalTransaction";
        case ComplexInventoryTransactionType::InventoryMismatch: return "InventoryMismatch";
        case ComplexInventoryTransactionType::ItemUseTransaction: return "ItemUseTransaction";
        case ComplexInventoryTransactionType::ItemUseOnEntityTransaction: return "ItemUseOnEntityTransaction";
        case ComplexInventoryTransactionType::ItemReleaseTransaction: return "ItemReleaseTransaction";
        default: return "Unknown";
    }
}

inline std::string_view toString(ItemUseInventoryTransactionActionType type) {
    switch (type) {
        case ItemUseInventoryTransactionActionType::Place: return "Place";
        case ItemUseInventoryTransactionActionType::Use: return "Use";
        case ItemUseInventoryTransactionActionType::Destroy: return "Destroy";
        case ItemUseInventoryTransactionActionType::UseAsAttack: return "UseAsAttack";
        default: return "Unknown";
    }
}

inline std::string_view toString(ItemUseInventoryTransactionTriggerType type) {
    switch (type) {
        case ItemUseInventoryTransactionTriggerType::Unknown: return "Unknown";
        case ItemUseInventoryTransactionTriggerType::PlayerInput: return "PlayerInput";
        case ItemUseInventoryTransactionTriggerType::SimulationTick: return "SimulationTick";
        default: return "Unknown";
    }
}

inline std::string_view toString(ItemUseInventoryTransactionPredictedResult type) {
    switch (type) {
        case ItemUseInventoryTransactionPredictedResult::Failure: return "Failure";
        case ItemUseInventoryTransactionPredictedResult::Success: return "Success";
        default: return "Unknown";
    }
}

inline std::string_view toString(ItemUseOnActorInventoryTransactionActionType type) {
    switch (type) {
        case ItemUseOnActorInventoryTransactionActionType::Interact: return "Interact";
        case ItemUseOnActorInventoryTransactionActionType::Attack: return "Attack";
        case ItemUseOnActorInventoryTransactionActionType::ItemInteract: return "ItemInteract";
        default: return "Unknown";
    }
}

inline std::string_view toString(ItemReleaseInventoryTransactionActionType type) {
    switch (type) {
        case ItemReleaseInventoryTransactionActionType::Release: return "Release";
        case ItemReleaseInventoryTransactionActionType::Use: return "Use";
        default: return "Unknown";
    }
}
} // namespace InventoryTransactionPacketUtils
