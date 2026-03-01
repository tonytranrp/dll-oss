#include "Inventory.hpp"
#include "../../../Utils/Memory/Game/SignatureAndOffsetManager.hpp"

ItemStack *Inventory::getItem(int slot) {
    static int off = GET_OFFSET("Inventory::getItem");
    return Memory::CallVFuncI<ItemStack *>(off, this, slot);
}

void Inventory::setItem(int slot, const ItemStack& item) {
    setItemWithForceBalance(slot, item, false);
}

void Inventory::setItemWithForceBalance(int slot, const ItemStack& item, bool forceBalance) {
    static int off = GET_OFFSET("Inventory::setItem");
    if (off <= 0) off = 13;
    Memory::CallVFuncI<void, int, const ItemStack&, bool>(off, this, slot, item, forceBalance);
}
