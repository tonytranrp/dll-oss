#pragma once

#include <cstddef>
#include <cstdint>

class SubChunkStorage;

class SubChunk {
public:
    static constexpr size_t kExpectedSize = 0x60;

private:
    // Ensure std::vector<SubChunk> uses the real in-memory stride for Bedrock 1.21.114.
    uint8_t _storage[kExpectedSize]{};

public:
    void* getMainStoragePtr();
    const void* getMainStoragePtr() const;
    SubChunkStorage* getMainStorage();
    const SubChunkStorage* getMainStorage() const;

    bool tryGetAbsoluteIndex(int* outAbsoluteIndex) const;
    int getAbsoluteIndexOrFallback(int fallbackAbsoluteIndex) const;
    uint8_t getRenderTrackingVersion() const;
};
