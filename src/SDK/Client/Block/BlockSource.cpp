#include "BlockSource.hpp"

Block *BlockSource::getBlock(const BlockPos & pos) {
    return Memory::CallVFuncI<Block *, const BlockPos &>(GET_OFFSET("BlockSource::getBlock"), this, pos);
}

LevelChunk* BlockSource::getChunkAt(const BlockPos& pos) {
    return Memory::CallVFuncI<LevelChunk*, const BlockPos&>(GET_OFFSET("BlockSource::getChunkAt"), this, pos);
}

LevelChunk* BlockSource::getChunk(const ChunkPos& pos) {
    return Memory::CallVFuncI<LevelChunk*, const ChunkPos&>(GET_OFFSET("BlockSource::getChunk"), this, pos);
}

LevelChunk* BlockSource::getChunk(int chunkX, int chunkZ) {
    return Memory::CallVFuncI<LevelChunk*>(GET_OFFSET("BlockSource::getChunkXZ"), this, chunkX, chunkZ);
}

bool BlockSource::isChunkLoaded(const ChunkPos& pos) {
    return getChunk(pos) != nullptr;
}

bool BlockSource::isChunkLoaded(int chunkX, int chunkZ) {
    return getChunk(chunkX, chunkZ) != nullptr;
}

ChunkSource* BlockSource::getChunkSource() {
    return Memory::CallVFuncI<ChunkSource*>(GET_OFFSET("BlockSource::getChunkSource"), this);
}

short BlockSource::getMinHeight() const {
    return Memory::CallVFuncI<short>(GET_OFFSET("BlockSource::getMinHeight"), const_cast<BlockSource*>(this));
}

short BlockSource::getMaxHeight() const {
    return Memory::CallVFuncI<short>(GET_OFFSET("BlockSource::getMaxHeight"), const_cast<BlockSource*>(this));
}

bool BlockSource::areChunksFullyLoaded(const BlockPos& pos, int radius) const {
    return Memory::CallVFuncI<bool, const BlockPos&, int>(
        GET_OFFSET("BlockSource::areChunksFullyLoaded"),
        const_cast<BlockSource*>(this),
        pos,
        radius
    );
}

Dimension *BlockSource::getDimension() {
    return hat::member_at<Dimension *>(this, GET_OFFSET("BlockSource::dimension"));
}

Biome *BlockSource::getBiome(const BlockPos &bp) {

    static uintptr_t sig;

    if (sig == NULL) sig = GET_SIG_ADDRESS("BlockSource::getBiome");

    using efunc = Biome *(__cdecl *)(BlockSource *, const BlockPos &);
    auto func = reinterpret_cast<efunc>(sig);
    return func(this, bp);
}
