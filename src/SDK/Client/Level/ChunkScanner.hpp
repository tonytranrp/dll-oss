#pragma once

#include <cstdint>
#include <future>
#include <functional>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

#include "ChunkScanMap.hpp"

class Block;
class BlockSource;
class LevelChunk;
class SubChunk;
class SubChunkStorage;

enum class ChunkScannerMode : uint8_t {
    Fallback,
    Deep
};

enum class ChunkRenderState : uint8_t {
    LoadedCached,
    LoadedPending,
    Unloaded
};

struct ChunkRenderEntry {
    ChunkPos chunkPos{};
    bool loaded = false;
    ChunkRenderState state = ChunkRenderState::Unloaded;
    bool hasMatch = false;
};

struct ChunkScannerStats {
    ChunkPos centerChunk{};
    int radius = 0;
    ChunkScannerMode mode = ChunkScannerMode::Fallback;

    int targetChunks = 0;
    int completedChunks = 0;
    int targetSubChunks = 0;
    int completedSubChunks = 0;
    int queuedChunks = 0;
    int loadedChunks = 0;
    int unloadedChunks = 0;
    int recheckedChunks = 0;
    int invalidatedChunks = 0;
    int matchedChunks = 0;
    int subChunkIndexFallbacks = 0;
    int decodeCalibrationScore = 0;
    int decodeCalibrationSamples = 0;
    int decodeSubChunkDelta = 0;
    uint8_t decodePermutation = 1;

    int64_t totalBlocks = 0;
    int64_t airBlocks = 0;
    int64_t nonAirBlocks = 0;
    int64_t nullBlocks = 0;
    int64_t matchedBlocks = 0;
    uint64_t tokenTotals[kChunkTokenSlots]{};

    bool sweepComplete = false;
};

struct ChunkScannerConfig {
    bool deepEnabled = true;
    bool fallbackEnabled = true;
    int maxSubChunksPerTick = 1024;
    int maxElementsPerTick = 524288;
    int fallbackMaxBlocksPerTick = 32768;
    int maxScanMicrosPerTick = 8000;
    int maxBurstMicrosPerTick = 20000;

    int refreshTicks = 40;
    int localRecheckTicks = 90;
    int localRecheckRadius = 1;
    int globalRecheckBudget = 384;
    int radiusHysteresisTicks = 3;

    int cacheChunkLimit = 2048;
    int maxProbeRadius = 32;
    int workerThreads = 2;
    int maxDispatchPerTick = 64;
    int maxRenderMatchesPerChunk = 96;
};

using BlockMatchFn = int8_t(*)(void* ctx, const Block* block);

class ChunkScanner {
private:
    static constexpr int kChunkSize = 16;
    static constexpr int kSubChunkBlockCount = 4096;
    static constexpr int kMaxSubChunksSafety = 64;

    struct ActiveScanTask {
        uint64_t key = 0;
        ChunkPos chunkPos{};
        LevelChunk* chunkPtr = nullptr;
        ChunkScannerMode mode = ChunkScannerMode::Fallback;

        int minY = -64;
        int maxYExclusive = 320;
        int blocksPerLayer = kChunkSize * kChunkSize;

        int progress = 0;
        int elementProgress = 0;
        int totalWork = 0;
        bool failed = false;
        int failureSubChunkIndex = -1;
        std::string failureReason{};

        int64_t total = 0;
        int64_t air = 0;
        int64_t nonAir = 0;
        int64_t null = 0;
        int64_t matched = 0;
        uint32_t tokenCounts[kChunkTokenSlots]{};
        uint32_t subChunkIndexFallbacks = 0;
        std::vector<uint32_t> matchedPacked{};
    };

    struct AsyncScanResult {
        bool success = false;
        ChunkPos chunkPos{};
        LevelChunk* chunkPtr = nullptr;
        uint64_t fingerprint = 0;
        uint16_t subChunkCount = 0;
        uint32_t subChunkIndexFallbacks = 0;
        ChunkBlockCounts counts{};
        std::vector<uint32_t> matchedPacked{};
    };

    struct AsyncTask {
        uint64_t key = 0;
        ChunkPos chunkPos{};
        LevelChunk* chunkPtr = nullptr;
        uint64_t fingerprint = 0;
        std::future<AsyncScanResult> future{};
    };

    ChunkScannerConfig mConfig{};
    ChunkScanMap mMap{};
    std::optional<ActiveScanTask> mActiveTask{};

    bool mHasArea = false;
    ChunkPos mAreaCenter{};
    int mAreaRadius = 0;

    int mWorldMinY = -64;
    int mWorldMaxYExclusive = 320;

    ChunkScannerStats mStats{};
    int mRecheckedThisTick = 0;
    int mInvalidatedThisTick = 0;
    int mLastRecheckTick = 0;
    size_t mRecheckCursor = 0;
    int mCommittedAutoRadius = 1;
    int mPendingAutoRadius = 1;
    int mPendingRadiusTicks = 0;
    int mLastTick = 0;

    BlockMatchFn mMatchFn = nullptr;
    void* mMatchCtx = nullptr;
    uint8_t mMatchSlots = 0;
    std::vector<AsyncTask> mAsyncTasks{};
    std::unordered_set<uint64_t> mInFlightKeys{};
    uint8_t mDecodePermutation = 1; // 0:XYZ 1:XZY 2:YXZ 3:YZX 4:ZXY 5:ZYX
    int8_t mDecodeSubChunkDelta = 0;
    int mDecodeCalibrationScore = 0;
    int mDecodeCalibrationSamples = 0;

    bool startNextTask();
    bool buildDeepTask(uint64_t key, ChunkScanCacheEntry& entry);
    bool buildFallbackTask(uint64_t key, ChunkScanCacheEntry& entry);
    bool processDeepTask(const std::function<bool(const Block*)>& isAirResolver, int& subChunkBudget, int& elementBudget);
    bool processFallbackTask(BlockSource* blockSource, const std::function<bool(const Block*)>& isAirResolver, int& blockBudget);
    void finalizeTask(BlockSource* blockSource, bool completed);
    void invalidateEntry(ChunkScanCacheEntry& entry, uint64_t key, std::optional<uint64_t> activeKey, bool highPriority);
    void recheckTargets(BlockSource* blockSource, int tick);
    int computeObservedRadius(BlockSource* blockSource, const ChunkPos& centerChunk) const;
    void dispatchAsyncTasks(BlockSource* blockSource);
    void consumeAsyncResults(BlockSource* blockSource);
    AsyncScanResult scanChunkAsync(const ChunkPos& chunkPos, LevelChunk* chunkPtr, uint64_t fingerprint) const;
    void maybeCalibrateDecode(BlockSource* blockSource, int tick);
    BlockPos decodeMatchedPosition(const ChunkPos& chunkPos, uint32_t packed) const;
    void refreshStats();
    int expectedSubChunkCount() const;
    bool useDeepMode() const;
    static uint32_t clampCountToU32(int64_t value);

public:
    void reset();
    void setConfig(const ChunkScannerConfig& config);
    void setWorldHeightBounds(int minY, int maxYExclusive);
    void setMatcher(BlockMatchFn fn, void* ctx, uint8_t tokenSlots);

    int detectAutoRadius(BlockSource* blockSource, const ChunkPos& centerChunk);
    bool shouldRebuildForPlayerChunk(const ChunkPos& playerChunk) const;
    void rebuildArea(BlockSource* blockSource, const ChunkPos& centerChunk, int radius, int tick);
    void tick(BlockSource* blockSource, int tick, const std::function<bool(const Block*)>& isAirResolver);

    const ChunkScannerStats& stats() const;
    std::vector<ChunkRenderEntry> buildRenderEntries() const;
    std::vector<BlockPos> buildMatchedBlockRenderList(size_t maxBlocks) const;
};
