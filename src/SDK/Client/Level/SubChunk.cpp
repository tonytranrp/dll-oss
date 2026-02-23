#include "SubChunk.hpp"

#include "../../../Utils/Memory/Memory.hpp"
#include "../../../Utils/Memory/Game/SignatureAndOffsetManager.hpp"
#include "SubChunkStorage.hpp"

void* SubChunk::getMainStoragePtr() {
    const int offset = GET_OFFSET("SubChunk::mBlocksReadPtr");
    if (offset <= 0) {
        return nullptr;
    }

    __try {
        return hat::member_at<void*>(this, offset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

const void* SubChunk::getMainStoragePtr() const {
    const int offset = GET_OFFSET("SubChunk::mBlocksReadPtr");
    if (offset <= 0) {
        return nullptr;
    }

    __try {
        return hat::member_at<const void*>(this, offset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

SubChunkStorage* SubChunk::getMainStorage() {
    return reinterpret_cast<SubChunkStorage*>(getMainStoragePtr());
}

const SubChunkStorage* SubChunk::getMainStorage() const {
    return reinterpret_cast<const SubChunkStorage*>(getMainStoragePtr());
}

bool SubChunk::tryGetAbsoluteIndex(int* outAbsoluteIndex) const {
    if (outAbsoluteIndex == nullptr) {
        return false;
    }

    const int offset = GET_OFFSET("SubChunk::mAbsoluteIndex");
    if (offset <= 0) {
        *outAbsoluteIndex = 0;
        return false;
    }

    __try {
        *outAbsoluteIndex = static_cast<int>(hat::member_at<int8_t>(this, offset));
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        *outAbsoluteIndex = 0;
        return false;
    }
}

int SubChunk::getAbsoluteIndexOrFallback(const int fallbackAbsoluteIndex) const {
    int absoluteIndex = fallbackAbsoluteIndex;
    if (tryGetAbsoluteIndex(&absoluteIndex)) {
        return absoluteIndex;
    }
    return fallbackAbsoluteIndex;
}

uint8_t SubChunk::getRenderTrackingVersion() const {
    const int offset = GET_OFFSET("SubChunk::mRenderChunkTrackingVersionNumber");
    if (offset <= 0) {
        return 0;
    }

    __try {
        return hat::member_at<uint8_t>(this, offset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}
