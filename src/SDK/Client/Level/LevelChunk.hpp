#pragma once

#include <cstdint>
#include <vector>

#include "SubChunk.hpp"

class LevelChunk {
public:
    std::vector<SubChunk>* tryGetSubChunks();
    const std::vector<SubChunk>* tryGetSubChunks() const;

    int getSubChunkCount() const;
    SubChunk* getSubChunk(int index);
    const SubChunk* getSubChunk(int index) const;
    uint64_t getRenderTrackingFingerprint() const;
};
