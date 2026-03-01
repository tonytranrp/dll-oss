#pragma once

#include "Inventory.hpp"
#include <Utils/Memory/Memory.hpp>
#include <libhat/Access.hpp>

class PlayerInventory {
public:
    int getSelectedSlot() {
        return hat::member_at<int>(this, GET_OFFSET("PlayerInventory::SelectedSlot"));
    }

    void setSelectedSlot(int slot) {
        hat::member_at<int>(this, GET_OFFSET("PlayerInventory::SelectedSlot")) = slot;
    }

    Inventory *getInventory() {
        return hat::member_at<Inventory*>(this, GET_OFFSET("PlayerInventory::inventory"));
    }
};
