#pragma once

#include "HitResult.hpp"

namespace ChunkMath {
    constexpr int floorDiv(const int value, const int divisor) {
        int quotient = value / divisor;
        const int remainder = value % divisor;

        if (remainder != 0 && ((remainder < 0) != (divisor < 0))) {
            --quotient;
        }

        return quotient;
    }
}

struct ChunkPos {
    int x{};
    int z{};

    static ChunkPos fromBlockPos(const BlockPos& pos) {
        return {
            ChunkMath::floorDiv(pos.x, 16),
            ChunkMath::floorDiv(pos.z, 16)
        };
    }
};

struct SubChunkPos {
    int x{};
    int y{};
    int z{};

    static SubChunkPos fromBlockPos(const BlockPos& pos) {
        return {
            ChunkMath::floorDiv(pos.x, 16),
            ChunkMath::floorDiv(pos.y, 16),
            ChunkMath::floorDiv(pos.z, 16)
        };
    }
};
