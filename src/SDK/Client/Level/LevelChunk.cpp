#include "LevelChunk.hpp"

#include "../../../Utils/Memory/Memory.hpp"
#include "../../../Utils/Memory/Game/SignatureAndOffsetManager.hpp"

namespace {
    [[nodiscard]] uint64_t hashMix(uint64_t seed, uint64_t value) {
        seed ^= value + 0x9E3779B97F4A7C15ULL + (seed << 6) + (seed >> 2);
        return seed;
    }
}

std::vector<SubChunk>* LevelChunk::tryGetSubChunks() {
    const int offset = GET_OFFSET("LevelChunk::mSubChunks");
    if (offset <= 0) {
        return nullptr;
    }

    __try {
        auto& subChunks = hat::member_at<std::vector<SubChunk>>(this, offset);
        return &subChunks;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

const std::vector<SubChunk>* LevelChunk::tryGetSubChunks() const {
    const int offset = GET_OFFSET("LevelChunk::mSubChunks");
    if (offset <= 0) {
        return nullptr;
    }

    __try {
        const auto& subChunks = hat::member_at<std::vector<SubChunk>>(this, offset);
        return &subChunks;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

int LevelChunk::getSubChunkCount() const {
    const auto* subChunks = tryGetSubChunks();
    if (subChunks == nullptr) {
        return 0;
    }

    __try {
        const size_t size = subChunks->size();
        if (size > 64) {
            return 0;
        }
        return static_cast<int>(size);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

SubChunk* LevelChunk::getSubChunk(int index) {
    auto* subChunks = tryGetSubChunks();
    if (subChunks == nullptr || index < 0) {
        return nullptr;
    }

    __try {
        if (index >= static_cast<int>(subChunks->size())) {
            return nullptr;
        }
        return &(*subChunks)[static_cast<size_t>(index)];
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

const SubChunk* LevelChunk::getSubChunk(int index) const {
    const auto* subChunks = tryGetSubChunks();
    if (subChunks == nullptr || index < 0) {
        return nullptr;
    }

    __try {
        if (index >= static_cast<int>(subChunks->size())) {
            return nullptr;
        }
        return &(*subChunks)[static_cast<size_t>(index)];
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

uint64_t LevelChunk::getRenderTrackingFingerprint() const {
    const int subChunkCount = getSubChunkCount();
    if (subChunkCount <= 0) {
        return 0;
    }

    uint64_t hash = 1469598103934665603ULL;
    hash = hashMix(hash, reinterpret_cast<uint64_t>(this));
    hash = hashMix(hash, static_cast<uint64_t>(subChunkCount));

    for (int i = 0; i < subChunkCount; ++i) {
        const SubChunk* subChunk = getSubChunk(i);
        if (subChunk == nullptr) {
            continue;
        }

        const int absoluteIndex = subChunk->getAbsoluteIndexOrFallback(i);
        const uint64_t packed =
            static_cast<uint64_t>(subChunk->getRenderTrackingVersion()) |
            (static_cast<uint64_t>(static_cast<uint8_t>(absoluteIndex)) << 8);
        hash = hashMix(hash, packed);
    }

    return hash;
}
