#pragma once

#include "../Item/ItemStack.hpp"

class Inventory {
public:
    ItemStack *getItem(int slot);
    void setItem(int slot, const ItemStack& item);
    void setItemWithForceBalance(int slot, const ItemStack& item, bool forceBalance);
};

class SimpleContainer : public Inventory {}; // Derived from container but I CBA
