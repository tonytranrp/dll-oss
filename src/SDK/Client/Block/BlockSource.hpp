#pragma once

#include "../Level/Dimension.hpp"
#include "../../../Utils/Utils.hpp"
#include "../../../Utils/Memory/Memory.hpp"
#include "../Level/Biome.hpp"
#include "../Level/Level.hpp"
#include "../Level/ChunkTypes.hpp"
#include "../Level/LevelChunk.hpp"
#include "../Level/ChunkSource.hpp"
#include "Block.hpp"

class BlockSource {
public:
    Block *getBlock(BlockPos const & pos);

    LevelChunk* getChunkAt(const BlockPos& pos);

    LevelChunk* getChunk(const ChunkPos& pos);

    LevelChunk* getChunk(int chunkX, int chunkZ);

    bool isChunkLoaded(const ChunkPos& pos);

    bool isChunkLoaded(int chunkX, int chunkZ);

    ChunkSource* getChunkSource();

    short getMinHeight() const;

    short getMaxHeight() const;

    bool areChunksFullyLoaded(const BlockPos& pos, int radius) const;

    Dimension* getDimension();

    Biome *getBiome(BlockPos const & bp);
};
