#include "SubChunkStorage.hpp"

#include "../../../Utils/Memory/Memory.hpp"
#include "../../../Utils/Memory/Game/SignatureAndOffsetManager.hpp"
#include "../Block/Block.hpp"

const Block* SubChunkStorage::getElement(uint16_t idx) const {
    const Block& result = Memory::CallVFuncI<const Block&, uint16_t>(
        GET_OFFSET("SubChunkStorage::getElement"),
        const_cast<SubChunkStorage*>(this),
        idx
    );
    return &result;
}

bool SubChunkStorage::isUniform(const Block& block) const {
    return Memory::CallVFuncI<bool, const Block&>(
        GET_OFFSET("SubChunkStorage::isUniform"),
        const_cast<SubChunkStorage*>(this),
        block
    );
}

bool SubChunkStorage::tryGetElement(uint16_t idx, const Block** outBlock) const {
    if (outBlock == nullptr) {
        return false;
    }

    __try {
        const Block& result = Memory::CallVFuncI<const Block&, uint16_t>(
            GET_OFFSET("SubChunkStorage::getElement"),
            const_cast<SubChunkStorage*>(this),
            idx
        );
        *outBlock = &result;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        *outBlock = nullptr;
        return false;
    }
}

bool SubChunkStorage::tryIsUniform(const Block& block, bool* outIsUniform) const {
    if (outIsUniform == nullptr) {
        return false;
    }

    __try {
        *outIsUniform = Memory::CallVFuncI<bool, const Block&>(
            GET_OFFSET("SubChunkStorage::isUniform"),
            const_cast<SubChunkStorage*>(this),
            block
        );
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        *outIsUniform = false;
        return false;
    }
}
