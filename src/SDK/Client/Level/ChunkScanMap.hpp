#pragma once

#include <cstdint>
#include <deque>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "ChunkTypes.hpp"

class LevelChunk;

inline constexpr uint8_t kChunkTokenSlots = 4;

struct ChunkBlockCounts {
    uint32_t total = 0;
    uint32_t air = 0;
    uint32_t nonAir = 0;
    uint32_t null = 0;
    uint32_t matched = 0;
    uint16_t tokenCounts[kChunkTokenSlots]{};
};

struct ChunkScanCacheEntry {
    ChunkPos chunkPos{};
    LevelChunk* chunkPtr = nullptr;
    uint64_t fingerprint = 0;
    bool loaded = false;
    bool valid = false;
    bool stale = false;
    bool pending = false;

    uint16_t subChunkCount = 0;
    uint16_t expectedSubChunkCount = 0;
    uint16_t subChunkIndexFallbacks = 0;

    uint32_t lastSeenTick = 0;
    uint32_t lastUpdatedTick = 0;
    uint64_t fingerprintProbe = 0;
    uint8_t fingerprintProbeHits = 0;

    ChunkBlockCounts counts{};
    std::vector<uint32_t> matchedPacked{};
};

class ChunkScanMap {
public:
    using Key = uint64_t;

    void clear() {
        mEntries.clear();
        mTargetKeys.clear();
        mTargetKeySet.clear();
        mScanQueue.clear();
        mQueuedKeySet.clear();
    }

    void reserveForTargetCount(int targetCount) {
        if (targetCount <= 0) {
            return;
        }

        mTargetKeys.reserve(static_cast<size_t>(targetCount));
        mTargetKeySet.reserve(static_cast<size_t>(targetCount) * 2);
        mEntries.reserve(static_cast<size_t>(targetCount) * 4);
    }

    void rebuildTargets(const ChunkPos& centerChunk, int radius, std::optional<Key> activeKey = std::nullopt) {
        std::vector<Key> nextKeys{};
        std::unordered_set<Key> nextSet{};

        const int side = radius * 2 + 1;
        const int targetCount = side * side;
        nextKeys.reserve(static_cast<size_t>(targetCount));
        nextSet.reserve(static_cast<size_t>(targetCount) * 2);

        for (int chunkZ = centerChunk.z - radius; chunkZ <= centerChunk.z + radius; ++chunkZ) {
            for (int chunkX = centerChunk.x - radius; chunkX <= centerChunk.x + radius; ++chunkX) {
                const Key key = packKey(chunkX, chunkZ);
                nextKeys.emplace_back(key);
                nextSet.emplace(key);
            }
        }

        if (!mScanQueue.empty()) {
            std::deque<Key> nextQueue{};
            std::unordered_set<Key> nextQueued{};
            nextQueued.reserve(mQueuedKeySet.size() * 2);

            for (const Key key : mScanQueue) {
                if (activeKey.has_value() && activeKey.value() == key) {
                    continue;
                }

                if (!nextSet.contains(key)) {
                    auto it = mEntries.find(key);
                    if (it != mEntries.end()) {
                        it->second.pending = false;
                    }
                    continue;
                }

                if (nextQueued.contains(key)) {
                    continue;
                }

                nextQueue.emplace_back(key);
                nextQueued.emplace(key);
            }

            mScanQueue = std::move(nextQueue);
            mQueuedKeySet = std::move(nextQueued);
        }

        mTargetKeys = std::move(nextKeys);
        mTargetKeySet = std::move(nextSet);
    }

    ChunkScanCacheEntry& getOrCreateEntry(Key key, const ChunkPos& chunkPos, int lastSeenTick) {
        ChunkScanCacheEntry& entry = mEntries[key];
        entry.chunkPos = chunkPos;
        entry.lastSeenTick = lastSeenTick;
        return entry;
    }

    ChunkScanCacheEntry* findEntry(Key key) {
        auto it = mEntries.find(key);
        if (it == mEntries.end()) {
            return nullptr;
        }
        return &it->second;
    }

    const ChunkScanCacheEntry* findEntry(Key key) const {
        auto it = mEntries.find(key);
        if (it == mEntries.end()) {
            return nullptr;
        }
        return &it->second;
    }

    std::unordered_map<Key, ChunkScanCacheEntry>& entries() {
        return mEntries;
    }

    const std::unordered_map<Key, ChunkScanCacheEntry>& entries() const {
        return mEntries;
    }

    const std::vector<Key>& targetKeys() const {
        return mTargetKeys;
    }

    bool containsTarget(Key key) const {
        return mTargetKeySet.contains(key);
    }

    void clearQueue() {
        for (const Key key : mQueuedKeySet) {
            auto it = mEntries.find(key);
            if (it != mEntries.end()) {
                it->second.pending = false;
            }
        }
        mScanQueue.clear();
        mQueuedKeySet.clear();
    }

    void enqueue(Key key, std::optional<Key> activeKey = std::nullopt, bool highPriority = false) {
        if (activeKey.has_value() && activeKey.value() == key) {
            return;
        }

        if (mQueuedKeySet.contains(key)) {
            return;
        }

        if (highPriority) {
            mScanQueue.emplace_front(key);
        } else {
            mScanQueue.emplace_back(key);
        }
        mQueuedKeySet.emplace(key);

        auto it = mEntries.find(key);
        if (it != mEntries.end()) {
            it->second.pending = true;
        }
    }

    bool popNext(Key* outKey) {
        if (outKey == nullptr || mScanQueue.empty()) {
            return false;
        }

        *outKey = mScanQueue.front();
        mScanQueue.pop_front();
        mQueuedKeySet.erase(*outKey);
        return true;
    }

    int queuedCount() const {
        return static_cast<int>(mScanQueue.size());
    }

    void markPending(Key key, bool pending) {
        auto it = mEntries.find(key);
        if (it != mEntries.end()) {
            it->second.pending = pending;
        }
    }

    void evict(int configuredLimit, std::optional<Key> activeKey) {
        const int baseLimit = configuredLimit < 64 ? 64 : configuredLimit;
        const int targetFloor = static_cast<int>(mTargetKeySet.size());
        const int limit = baseLimit > targetFloor ? baseLimit : targetFloor;
        if (static_cast<int>(mEntries.size()) <= limit) {
            return;
        }

        for (auto it = mEntries.begin(); it != mEntries.end() && static_cast<int>(mEntries.size()) > limit;) {
            const bool active = activeKey.has_value() && activeKey.value() == it->first;
            if (!mTargetKeySet.contains(it->first) && !it->second.pending && !active) {
                it = mEntries.erase(it);
                continue;
            }
            ++it;
        }

        while (static_cast<int>(mEntries.size()) > limit) {
            auto oldestIt = mEntries.end();
            for (auto it = mEntries.begin(); it != mEntries.end(); ++it) {
                const bool active = activeKey.has_value() && activeKey.value() == it->first;
                if (active || it->second.pending || mTargetKeySet.contains(it->first)) {
                    continue;
                }

                if (oldestIt == mEntries.end() || it->second.lastSeenTick < oldestIt->second.lastSeenTick) {
                    oldestIt = it;
                }
            }

            if (oldestIt == mEntries.end()) {
                break;
            }

            mEntries.erase(oldestIt);
        }
    }

    static Key packKey(int chunkX, int chunkZ) {
        return (static_cast<uint64_t>(static_cast<uint32_t>(chunkX)) << 32) |
               static_cast<uint64_t>(static_cast<uint32_t>(chunkZ));
    }

    static ChunkPos unpackKey(Key key) {
        return ChunkPos{
            static_cast<int>(static_cast<int32_t>(key >> 32)),
            static_cast<int>(static_cast<int32_t>(key & 0xFFFFFFFFULL))
        };
    }

private:
    std::unordered_map<Key, ChunkScanCacheEntry> mEntries;
    std::vector<Key> mTargetKeys;
    std::unordered_set<Key> mTargetKeySet;
    std::deque<Key> mScanQueue;
    std::unordered_set<Key> mQueuedKeySet;
};
