#pragma once

#include <cstdint>

class Block;

class SubChunkStorage {
public:
    const Block* getElement(uint16_t idx) const;
    bool isUniform(const Block& block) const;

    bool tryGetElement(uint16_t idx, const Block** outBlock) const;
    bool tryIsUniform(const Block& block, bool* outIsUniform) const;
};
