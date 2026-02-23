#pragma once

#include <cstdint>

#include "ChunkTypes.hpp"

namespace ChunkMatchCodec {
    inline constexpr int kSubChunkBias = 128;
    inline constexpr uint32_t kInvalidPacked = 0xFFFFFFFFu;

    inline int floorMod(const int value, const int divisor) {
        int remainder = value % divisor;
        if (remainder < 0) {
            remainder += (divisor < 0) ? -divisor : divisor;
        }
        return remainder;
    }

    inline uint16_t makeElementIndex(const int localX, const int localY, const int localZ) {
        return static_cast<uint16_t>(((localX & 0xF) << 8) | ((localZ & 0xF) << 4) | (localY & 0xF));
    }

    inline uint32_t packFromSubChunkElement(const int absoluteSubChunkIndex, const uint16_t elementIndex) {
        const int biased = absoluteSubChunkIndex + kSubChunkBias;
        if (biased < 0 || biased > 255) {
            return kInvalidPacked;
        }
        return (static_cast<uint32_t>(biased) << 12) | static_cast<uint32_t>(elementIndex & 0x0FFFu);
    }

    inline bool unpackToSubChunkElement(const uint32_t packed, int* outAbsoluteSubChunkIndex, uint16_t* outElementIndex) {
        if (packed == kInvalidPacked || outAbsoluteSubChunkIndex == nullptr || outElementIndex == nullptr) {
            return false;
        }

        *outAbsoluteSubChunkIndex = static_cast<int>((packed >> 12) & 0xFFu) - kSubChunkBias;
        *outElementIndex = static_cast<uint16_t>(packed & 0x0FFFu);
        return true;
    }

    inline BlockPos toBlockPos(const ChunkPos& chunkPos, const uint32_t packed) {
        int absoluteSubChunkIndex = 0;
        uint16_t elementIndex = 0;
        if (!unpackToSubChunkElement(packed, &absoluteSubChunkIndex, &elementIndex)) {
            return BlockPos{};
        }

        const int localX = static_cast<int>((elementIndex >> 8) & 0xF);
        const int localZ = static_cast<int>((elementIndex >> 4) & 0xF);
        const int localY = static_cast<int>(elementIndex & 0xF);
        return BlockPos{
            chunkPos.x * 16 + localX,
            absoluteSubChunkIndex * 16 + localY,
            chunkPos.z * 16 + localZ
        };
    }

    inline uint32_t packFromBlockPos(const BlockPos& pos) {
        const int absoluteSubChunkIndex = ChunkMath::floorDiv(pos.y, 16);
        const int localX = floorMod(pos.x, 16);
        const int localY = floorMod(pos.y, 16);
        const int localZ = floorMod(pos.z, 16);
        return packFromSubChunkElement(absoluteSubChunkIndex, makeElementIndex(localX, localY, localZ));
    }
}

