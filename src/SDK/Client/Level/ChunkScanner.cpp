#include "ChunkScanner.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <optional>

#include "../Block/Block.hpp"
#include "../Block/BlockLegacy.hpp"
#include "../Block/BlockSource.hpp"
#include "../../../Utils/Concurrency/TaskRuntime.hpp"
#include "../../../Utils/Logger/Logger.hpp"
#include "ChunkMatchCodec.hpp"
#include "LevelChunk.hpp"
#include "SubChunk.hpp"
#include "SubChunkStorage.hpp"

namespace {
    struct ResolvedSubChunkIndex {
        int value = 0;
        bool usedFallback = false;
    };

    [[nodiscard]] int computeBoost(const int queuedChunks, const int targetChunks) {
        const int safeTarget = std::max(1, targetChunks);
        const int ratio1000 = (queuedChunks * 1000) / safeTarget;

        if (ratio1000 > 800) return 8;
        if (ratio1000 > 600) return 6;
        if (ratio1000 > 400) return 4;
        if (ratio1000 > 200) return 3;
        if (ratio1000 > 50) return 2;
        return 1;
    }

    void addWithSaturation(int64_t& value, const int64_t delta) {
        if (delta <= 0) {
            return;
        }

        const int64_t maxValue = std::numeric_limits<int64_t>::max();
        if (value > (maxValue - delta)) {
            value = maxValue;
            return;
        }

        value += delta;
    }

    void addWithSaturation(uint32_t& value, const uint32_t delta) {
        if (delta == 0) {
            return;
        }

        const uint32_t maxValue = std::numeric_limits<uint32_t>::max();
        if (value > (maxValue - delta)) {
            value = maxValue;
            return;
        }

        value += delta;
    }

    [[nodiscard]] bool isAirByName(const Block* block) {
        if (block == nullptr) {
            return false;
        }

        BlockLegacy* legacy = const_cast<Block*>(block)->getBlockLegacy();
        if (legacy == nullptr) {
            return false;
        }

        std::string name = legacy->getName();
        std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        return name == "air" || name == "cave_air" || name == "void_air" || name.ends_with("_air");
    }

    [[nodiscard]] bool isReasonableAbsoluteSubChunkIndex(
        const int absoluteIndex,
        const int worldMinY,
        const int worldMaxYExclusive
    ) {
        const int minExpected = ChunkMath::floorDiv(worldMinY, 16) - 2;
        const int maxExpected = ChunkMath::floorDiv(worldMaxYExclusive - 1, 16) + 2;
        return absoluteIndex >= minExpected && absoluteIndex <= maxExpected;
    }

    [[nodiscard]] ResolvedSubChunkIndex resolveSubChunkAbsoluteIndex(
        const SubChunk* subChunk,
        const int worldMinY,
        const int worldMaxYExclusive,
        const int fallbackVectorIndex
    ) {
        const int fallbackAbsolute = ChunkMath::floorDiv(worldMinY, 16) + fallbackVectorIndex;
        if (subChunk == nullptr) {
            return {.value = fallbackAbsolute, .usedFallback = false};
        }

        int absoluteIndex = fallbackAbsolute;
        if (!subChunk->tryGetAbsoluteIndex(&absoluteIndex) ||
            !isReasonableAbsoluteSubChunkIndex(absoluteIndex, worldMinY, worldMaxYExclusive)) {
            return {.value = fallbackAbsolute, .usedFallback = true};
        }
        return {.value = absoluteIndex, .usedFallback = false};
    }

    [[nodiscard]] uint32_t packMatchedIndex(const int absoluteSubChunkIndex, const uint16_t elementIdx) {
        return ChunkMatchCodec::packFromSubChunkElement(absoluteSubChunkIndex, elementIdx);
    }

    [[nodiscard]] bool verifyUniformStorage(const SubChunkStorage* storage, const Block* firstBlock) {
        if (storage == nullptr || firstBlock == nullptr) {
            return false;
        }

        constexpr uint16_t kSampleIndices[] = {
            1, 15, 16, 31, 63, 127, 255, 511, 1023, 2047, 3071, 4095
        };

        for (const uint16_t sampleIndex : kSampleIndices) {
            const Block* sampleBlock = nullptr;
            if (!storage->tryGetElement(sampleIndex, &sampleBlock) || sampleBlock != firstBlock) {
                return false;
            }
        }

        return true;
    }

}

void ChunkScanner::reset() {
    mMap.clear();
    mActiveTask.reset();
    mAsyncTasks.clear();
    mInFlightKeys.clear();
    mHasArea = false;
    mAreaCenter = {};
    mAreaRadius = 0;
    mStats = {};
    mRecheckedThisTick = 0;
    mInvalidatedThisTick = 0;
    mLastRecheckTick = 0;
    mRecheckCursor = 0;
    mCommittedAutoRadius = 1;
    mPendingAutoRadius = 1;
    mPendingRadiusTicks = 0;
    mLastTick = 0;
    mDecodePermutation = 1; // XZY layout used by SubChunkStorage element ids
    mDecodeSubChunkDelta = 0;
    mDecodeCalibrationScore = 0;
    mDecodeCalibrationSamples = 0;
}

void ChunkScanner::setConfig(const ChunkScannerConfig& config) {
    mConfig.deepEnabled = config.deepEnabled;
    // Keep field for compatibility, but scanner is deep-only by design.
    mConfig.fallbackEnabled = false;
    mConfig.maxSubChunksPerTick = std::clamp(config.maxSubChunksPerTick, 1, 16384);
    mConfig.maxElementsPerTick = std::clamp(config.maxElementsPerTick, 256, 4 * 1024 * 1024);
    mConfig.fallbackMaxBlocksPerTick = std::clamp(config.fallbackMaxBlocksPerTick, 512, 4 * 1024 * 1024);
    mConfig.maxScanMicrosPerTick = std::clamp(config.maxScanMicrosPerTick, 1000, 100000);
    mConfig.maxBurstMicrosPerTick = std::clamp(config.maxBurstMicrosPerTick, mConfig.maxScanMicrosPerTick, 200000);

    mConfig.refreshTicks = std::clamp(config.refreshTicks, 1, 600);
    mConfig.localRecheckTicks = std::clamp(config.localRecheckTicks, mConfig.refreshTicks, 2400);
    mConfig.localRecheckRadius = std::clamp(config.localRecheckRadius, 0, 2);
    mConfig.globalRecheckBudget = std::clamp(config.globalRecheckBudget, 16, 4096);
    mConfig.radiusHysteresisTicks = std::clamp(config.radiusHysteresisTicks, 1, 8);

    mConfig.cacheChunkLimit = std::max(64, config.cacheChunkLimit);
    mConfig.maxProbeRadius = std::clamp(config.maxProbeRadius, 1, 256);
    // Game-memory reads stay on the game thread for correctness/stability.
    mConfig.workerThreads = 0;
    mConfig.maxDispatchPerTick = 0;
    mConfig.maxRenderMatchesPerChunk = std::clamp(config.maxRenderMatchesPerChunk, 0, 4096);
}

void ChunkScanner::setWorldHeightBounds(int minY, int maxYExclusive) {
    if (maxYExclusive <= minY || (maxYExclusive - minY) > 2048) {
        mWorldMinY = -64;
        mWorldMaxYExclusive = 320;
        return;
    }

    mWorldMinY = minY;
    mWorldMaxYExclusive = maxYExclusive;
}

void ChunkScanner::setMatcher(BlockMatchFn fn, void* ctx, const uint8_t tokenSlots) {
    mMatchFn = fn;
    mMatchCtx = ctx;
    mMatchSlots = static_cast<uint8_t>(std::clamp<int>(tokenSlots, 0, kChunkTokenSlots));
}

int ChunkScanner::computeObservedRadius(BlockSource* blockSource, const ChunkPos& centerChunk) const {
    if (blockSource == nullptr) {
        return 1;
    }

    int loadedRadius = 1;
    const int probeRadius = mConfig.maxProbeRadius;
    for (int z = centerChunk.z - probeRadius; z <= centerChunk.z + probeRadius; ++z) {
        for (int x = centerChunk.x - probeRadius; x <= centerChunk.x + probeRadius; ++x) {
            if (blockSource->getChunk(x, z) == nullptr) {
                continue;
            }

            const int radius = std::max(std::abs(x - centerChunk.x), std::abs(z - centerChunk.z));
            if (radius > loadedRadius) {
                loadedRadius = radius;
            }
        }
    }

    return std::max(1, loadedRadius);
}

int ChunkScanner::detectAutoRadius(BlockSource* blockSource, const ChunkPos& centerChunk) {
    const int observedRadius = computeObservedRadius(blockSource, centerChunk);

    if (!mHasArea) {
        mCommittedAutoRadius = observedRadius;
        mPendingAutoRadius = observedRadius;
        mPendingRadiusTicks = 0;
        return mCommittedAutoRadius;
    }

    if (observedRadius == mCommittedAutoRadius) {
        mPendingAutoRadius = observedRadius;
        mPendingRadiusTicks = 0;
        return mCommittedAutoRadius;
    }

    if (observedRadius > mCommittedAutoRadius) {
        mCommittedAutoRadius = observedRadius;
        mPendingAutoRadius = observedRadius;
        mPendingRadiusTicks = 0;
        return mCommittedAutoRadius;
    }

    // Shrinks are delayed to avoid temporary stream jitter collapsing the scan area.
    const int shrinkHoldTicks = std::max(2, mConfig.radiusHysteresisTicks * 4);
    if (mPendingAutoRadius != observedRadius) {
        mPendingAutoRadius = observedRadius;
        mPendingRadiusTicks = 1;
        return mCommittedAutoRadius;
    }

    ++mPendingRadiusTicks;
    if (mPendingRadiusTicks >= shrinkHoldTicks) {
        mCommittedAutoRadius = observedRadius;
        mPendingRadiusTicks = 0;
    }

    return mCommittedAutoRadius;

}

bool ChunkScanner::shouldRebuildForPlayerChunk(const ChunkPos& playerChunk) const {
    if (!mHasArea || mMap.targetKeys().empty()) {
        return true;
    }

    const uint64_t key = ChunkScanMap::packKey(playerChunk.x, playerChunk.z);
    if (!mMap.containsTarget(key)) {
        return true;
    }

    return false;
}

void ChunkScanner::invalidateEntry(
    ChunkScanCacheEntry& entry,
    const uint64_t key,
    std::optional<uint64_t> activeKey,
    const bool highPriority
) {
    if (activeKey.has_value() && activeKey.value() == key) {
        mActiveTask.reset();
        activeKey.reset();
    }

    const bool hadValid = entry.valid;
    entry.stale = hadValid;
    if (!hadValid) {
        entry.counts = {};
        entry.matchedPacked.clear();
    }
    entry.valid = hadValid;
    entry.pending = false;
    if (!mInFlightKeys.contains(key)) {
        mMap.enqueue(key, activeKey, highPriority);
    }
    ++mInvalidatedThisTick;
}

void ChunkScanner::rebuildArea(BlockSource* blockSource, const ChunkPos& centerChunk, int radius, int tick) {
    if (blockSource == nullptr) {
        return;
    }

    const int clampedRadius = std::clamp(radius, 1, mConfig.maxProbeRadius);
    const int side = clampedRadius * 2 + 1;
    const int targetCount = side * side;
    if (mConfig.workerThreads > 0) {
        const int maxInFlight = std::clamp(mConfig.workerThreads * 4, 1, 256);
        mAsyncTasks.reserve(static_cast<size_t>(maxInFlight));
        mInFlightKeys.reserve(static_cast<size_t>(maxInFlight) * 2);
    }

    const std::optional<uint64_t> activeKey = mActiveTask ? std::optional<uint64_t>(mActiveTask->key) : std::nullopt;

    mMap.reserveForTargetCount(targetCount);
    mMap.rebuildTargets(centerChunk, clampedRadius, activeKey);

    if (mActiveTask && !mMap.containsTarget(mActiveTask->key)) {
        mActiveTask.reset();
    }

    const int expectedSubCount = expectedSubChunkCount();
    for (const uint64_t key : mMap.targetKeys()) {
        const ChunkPos chunkPos = ChunkScanMap::unpackKey(key);
        ChunkScanCacheEntry& entry = mMap.getOrCreateEntry(key, chunkPos, tick);
        entry.chunkPos = chunkPos;
        entry.lastSeenTick = static_cast<uint32_t>(tick);
        entry.expectedSubChunkCount = static_cast<uint16_t>(std::clamp(expectedSubCount, 0, kMaxSubChunksSafety));

        LevelChunk* chunkPtr = blockSource->getChunk(chunkPos.x, chunkPos.z);
        if (chunkPtr == nullptr) {
            if (mActiveTask && mActiveTask->key == key) {
                mActiveTask.reset();
            }
            entry.loaded = false;
            entry.valid = true;
            entry.stale = false;
            entry.pending = false;
            entry.chunkPtr = nullptr;
            entry.fingerprint = 0;
            entry.fingerprintProbe = 0;
            entry.fingerprintProbeHits = 0;
            entry.subChunkCount = entry.expectedSubChunkCount;
            entry.subChunkIndexFallbacks = 0;
            entry.counts = {};
            entry.matchedPacked.clear();
            continue;
        }

        const uint64_t nextFingerprint = chunkPtr->getRenderTrackingFingerprint();
        const int nextSubChunkCount = chunkPtr->getSubChunkCount();
        const bool wasLoaded = entry.loaded;
        const bool pointerChanged = entry.chunkPtr != chunkPtr;
        const bool firstSeen = !wasLoaded || entry.chunkPtr == nullptr;
        const bool subChunkCountChanged =
            entry.valid &&
            nextSubChunkCount >= 0 &&
            nextSubChunkCount <= kMaxSubChunksSafety &&
            static_cast<int>(entry.subChunkCount) != nextSubChunkCount;

        entry.loaded = true;
        entry.chunkPtr = chunkPtr;
        entry.fingerprint = nextFingerprint;

        if (firstSeen || pointerChanged || subChunkCountChanged || !entry.valid || entry.stale) {
            invalidateEntry(entry, key, activeKey, false);
        }
    }

    const std::optional<uint64_t> activeAfterUpdate = mActiveTask ? std::optional<uint64_t>(mActiveTask->key) : std::nullopt;
    mMap.evict(mConfig.cacheChunkLimit, activeAfterUpdate);

    mHasArea = true;
    mAreaCenter = centerChunk;
    mAreaRadius = clampedRadius;
    mCommittedAutoRadius = clampedRadius;
}

void ChunkScanner::recheckTargets(BlockSource* blockSource, const int tick) {
    if (blockSource == nullptr || mMap.targetKeys().empty()) {
        return;
    }

    if ((tick - mLastRecheckTick) < mConfig.refreshTicks) {
        return;
    }

    mLastRecheckTick = tick;
    const bool busy = mActiveTask.has_value() || mMap.queuedCount() > 0 || !mAsyncTasks.empty();
    const std::optional<uint64_t> activeKey = mActiveTask ? std::optional<uint64_t>(mActiveTask->key) : std::nullopt;

    auto processKey = [&](const uint64_t key, const bool localPriority) {
        ChunkScanCacheEntry* entryPtr = mMap.findEntry(key);
        if (entryPtr == nullptr) {
            return;
        }

        ChunkScanCacheEntry& entry = *entryPtr;
        ++mRecheckedThisTick;
        entry.lastSeenTick = static_cast<uint32_t>(tick);

        LevelChunk* nextChunk = blockSource->getChunk(entry.chunkPos.x, entry.chunkPos.z);
        if (nextChunk == nullptr) {
            if (mActiveTask && mActiveTask->key == key) {
                mActiveTask.reset();
            }

            const bool wasLoaded = entry.loaded;
            entry.loaded = false;
            entry.valid = true;
            entry.pending = false;
            entry.chunkPtr = nullptr;
            entry.fingerprint = 0;
            entry.fingerprintProbe = 0;
            entry.fingerprintProbeHits = 0;
            entry.subChunkCount = entry.expectedSubChunkCount;
            entry.subChunkIndexFallbacks = 0;
            if (wasLoaded) {
                entry.counts = {};
                entry.matchedPacked.clear();
                ++mInvalidatedThisTick;
            }
            return;
        }

        const uint64_t nextFingerprint = nextChunk->getRenderTrackingFingerprint();
        const int nextSubChunkCount = nextChunk->getSubChunkCount();
        const bool pointerChanged = entry.chunkPtr != nextChunk;
        const bool subChunkCountChanged =
            nextSubChunkCount >= 0 &&
            nextSubChunkCount <= kMaxSubChunksSafety &&
            static_cast<int>(entry.subChunkCount) != nextSubChunkCount;
        entry.loaded = true;
        entry.chunkPtr = nextChunk;

        bool fingerprintChangedDebounced = false;
        if (entry.fingerprint == 0 || nextFingerprint == 0 || entry.fingerprint == nextFingerprint) {
            entry.fingerprint = nextFingerprint;
            entry.fingerprintProbe = 0;
            entry.fingerprintProbeHits = 0;
        } else {
            if (entry.fingerprintProbe != nextFingerprint) {
                entry.fingerprintProbe = nextFingerprint;
                entry.fingerprintProbeHits = 1;
            } else {
                entry.fingerprintProbeHits = static_cast<uint8_t>(
                    std::min<int>(255, static_cast<int>(entry.fingerprintProbeHits) + 1)
                );
            }

            if (entry.fingerprintProbeHits >= 2) {
                entry.fingerprint = nextFingerprint;
                entry.fingerprintProbe = 0;
                entry.fingerprintProbeHits = 0;
                fingerprintChangedDebounced = true;
            }
        }

        const bool shouldInvalidateForFingerprint = fingerprintChangedDebounced;

        if (pointerChanged || subChunkCountChanged || shouldInvalidateForFingerprint) {
            invalidateEntry(entry, key, activeKey, localPriority);
        }
    };

    if (busy) {
        const int localR = mConfig.localRecheckRadius;
        for (int dz = -localR; dz <= localR; ++dz) {
            for (int dx = -localR; dx <= localR; ++dx) {
                const uint64_t key = ChunkScanMap::packKey(mAreaCenter.x + dx, mAreaCenter.z + dz);
                if (!mMap.containsTarget(key)) {
                    continue;
                }
                processKey(key, true);
            }
        }
        return;
    }

    const int budget = std::min<int>(static_cast<int>(mMap.targetKeys().size()), mConfig.globalRecheckBudget);
    for (int i = 0; i < budget; ++i) {
        if (mMap.targetKeys().empty()) {
            break;
        }

        if (mRecheckCursor >= mMap.targetKeys().size()) {
            mRecheckCursor = 0;
        }

        const uint64_t key = mMap.targetKeys()[mRecheckCursor++];
        processKey(key, false);
    }
}

ChunkScanner::AsyncScanResult ChunkScanner::scanChunkAsync(
    const ChunkPos& chunkPos,
    LevelChunk* chunkPtr,
    const uint64_t fingerprint
) const {
    AsyncScanResult result{};
    result.chunkPos = chunkPos;
    result.chunkPtr = chunkPtr;
    result.fingerprint = fingerprint;

    if (chunkPtr == nullptr || !useDeepMode()) {
        return result;
    }

    const int subChunkCount = chunkPtr->getSubChunkCount();
    if (subChunkCount < 0 || subChunkCount > kMaxSubChunksSafety) {
        return result;
    }

    result.subChunkCount = static_cast<uint16_t>(subChunkCount);
    if (mConfig.maxRenderMatchesPerChunk > 0) {
        result.matchedPacked.reserve(static_cast<size_t>(mConfig.maxRenderMatchesPerChunk));
    }

    for (int subChunkIndex = 0; subChunkIndex < subChunkCount; ++subChunkIndex) {
        const SubChunk* subChunk = chunkPtr->getSubChunk(subChunkIndex);
        const ResolvedSubChunkIndex resolvedSubChunk = resolveSubChunkAbsoluteIndex(
            subChunk,
            mWorldMinY,
            mWorldMaxYExclusive,
            subChunkIndex
        );
        const int absoluteSubChunkIndex = resolvedSubChunk.value;
        if (resolvedSubChunk.usedFallback) {
            addWithSaturation(result.subChunkIndexFallbacks, 1);
        }
        if (subChunk == nullptr) {
            addWithSaturation(result.counts.total, kSubChunkBlockCount);
            addWithSaturation(result.counts.air, kSubChunkBlockCount);
            continue;
        }

        const SubChunkStorage* storage = subChunk->getMainStorage();
        if (storage == nullptr) {
            addWithSaturation(result.counts.total, kSubChunkBlockCount);
            addWithSaturation(result.counts.air, kSubChunkBlockCount);
            continue;
        }

        const Block* firstBlock = nullptr;
        if (!storage->tryGetElement(0, &firstBlock)) {
            return result;
        }

        if (firstBlock == nullptr) {
            addWithSaturation(result.counts.total, kSubChunkBlockCount);
            addWithSaturation(result.counts.null, kSubChunkBlockCount);
            continue;
        }

        bool uniform = false;
        const bool hasUniformInfo = storage->tryIsUniform(*firstBlock, &uniform);
        const bool useUniformFastPath = hasUniformInfo && uniform && verifyUniformStorage(storage, firstBlock);
        if (useUniformFastPath) {
            addWithSaturation(result.counts.total, kSubChunkBlockCount);
            if (isAirByName(firstBlock)) {
                addWithSaturation(result.counts.air, kSubChunkBlockCount);
            } else {
                addWithSaturation(result.counts.nonAir, kSubChunkBlockCount);
            }

            if (mMatchFn != nullptr) {
                const int8_t code = mMatchFn(mMatchCtx, firstBlock);
                if (code >= 0) {
                    addWithSaturation(result.counts.matched, kSubChunkBlockCount);
                    if (code < mMatchSlots) {
                        uint16_t& slot = result.counts.tokenCounts[static_cast<size_t>(code)];
                        slot = static_cast<uint16_t>(std::min<int>(std::numeric_limits<uint16_t>::max(), static_cast<int>(slot) + kSubChunkBlockCount));
                    }

                    if (mConfig.maxRenderMatchesPerChunk > 0) {
                        for (uint16_t idx = 0;
                            idx < kSubChunkBlockCount &&
                             static_cast<int>(result.matchedPacked.size()) < mConfig.maxRenderMatchesPerChunk;
                             ++idx) {
                            const uint32_t packed = packMatchedIndex(absoluteSubChunkIndex, idx);
                            if (packed != ChunkMatchCodec::kInvalidPacked) {
                                result.matchedPacked.emplace_back(packed);
                            }
                        }
                    }
                }
            }
            continue;
        }

        for (uint16_t blockIdx = 0; blockIdx < kSubChunkBlockCount; ++blockIdx) {
            const Block* block = nullptr;
            if (!storage->tryGetElement(blockIdx, &block)) {
                return result;
            }

            addWithSaturation(result.counts.total, 1);
            if (block == nullptr) {
                addWithSaturation(result.counts.null, 1);
                continue;
            }

            if (isAirByName(block)) {
                addWithSaturation(result.counts.air, 1);
            } else {
                addWithSaturation(result.counts.nonAir, 1);
            }

            if (mMatchFn != nullptr) {
                const int8_t code = mMatchFn(mMatchCtx, block);
                if (code >= 0) {
                    addWithSaturation(result.counts.matched, 1);
                    if (code < mMatchSlots) {
                        uint16_t& slot = result.counts.tokenCounts[static_cast<size_t>(code)];
                        slot = static_cast<uint16_t>(std::min<int>(std::numeric_limits<uint16_t>::max(), static_cast<int>(slot) + 1));
                    }

                    if (static_cast<int>(result.matchedPacked.size()) < mConfig.maxRenderMatchesPerChunk) {
                        const uint32_t packed = packMatchedIndex(absoluteSubChunkIndex, blockIdx);
                        if (packed != ChunkMatchCodec::kInvalidPacked) {
                            result.matchedPacked.emplace_back(packed);
                        }
                    }
                }
            }
        }
    }

    result.success = true;
    return result;
}

void ChunkScanner::dispatchAsyncTasks(BlockSource* blockSource) {
    if (blockSource == nullptr || mConfig.workerThreads <= 0 || mConfig.maxDispatchPerTick <= 0 || mMap.targetKeys().empty()) {
        return;
    }

    const int maxInFlight = std::clamp(mConfig.workerThreads * 4, 1, 256);
    int dispatched = 0;
    while (static_cast<int>(mAsyncTasks.size()) < maxInFlight && dispatched < mConfig.maxDispatchPerTick) {
        uint64_t key = 0;
        if (!mMap.popNext(&key)) {
            break;
        }
        ++dispatched;

        mMap.markPending(key, false);
        if (!mMap.containsTarget(key) || mInFlightKeys.contains(key)) {
            continue;
        }

        ChunkScanCacheEntry* entryPtr = mMap.findEntry(key);
        if (entryPtr == nullptr) {
            continue;
        }

        ChunkScanCacheEntry& entry = *entryPtr;
        entry.pending = false;

        if (!entry.loaded) {
            entry.valid = true;
            entry.stale = false;
            continue;
        }

        if (entry.valid && !entry.stale) {
            continue;
        }

        const ChunkPos chunkPos = entry.chunkPos;
        LevelChunk* chunkPtr = entry.chunkPtr;
        const uint64_t fingerprint = entry.fingerprint;

        entry.pending = true;
        mInFlightKeys.emplace(key);
        mAsyncTasks.emplace_back(AsyncTask{
            .key = key,
            .chunkPos = chunkPos,
            .chunkPtr = chunkPtr,
            .fingerprint = fingerprint,
            .future = TaskRuntime::submit([this, chunkPos, chunkPtr, fingerprint]() {
                return scanChunkAsync(chunkPos, chunkPtr, fingerprint);
            })
        });
    }
}

void ChunkScanner::consumeAsyncResults(BlockSource* blockSource) {
    if (mAsyncTasks.empty()) {
        return;
    }

    for (size_t i = 0; i < mAsyncTasks.size();) {
        AsyncTask& task = mAsyncTasks[i];
        if (task.future.valid() &&
            task.future.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
            ++i;
            continue;
        }

        AsyncScanResult result{};
        if (task.future.valid()) {
            result = task.future.get();
        }

        mInFlightKeys.erase(task.key);

        ChunkScanCacheEntry* entryPtr = mMap.findEntry(task.key);
        if (entryPtr != nullptr) {
            ChunkScanCacheEntry& entry = *entryPtr;
            entry.pending = false;

            const bool pointerMismatch = (entry.chunkPtr != task.chunkPtr);
            const bool chunkUnloaded = !entry.loaded || entry.chunkPtr == nullptr;
            if (chunkUnloaded || pointerMismatch) {
                if (mMap.containsTarget(task.key)) {
                    const std::optional<uint64_t> activeKey = mActiveTask ? std::optional<uint64_t>(mActiveTask->key) : std::nullopt;
                    mMap.enqueue(task.key, activeKey, true);
                }
            } else if (result.success) {
                entry.valid = true;
                entry.stale = false;
                entry.fingerprint = result.fingerprint;
                entry.subChunkCount = result.subChunkCount;
                entry.subChunkIndexFallbacks = static_cast<uint16_t>(
                    std::min<uint32_t>(std::numeric_limits<uint16_t>::max(), result.subChunkIndexFallbacks)
                );
                entry.counts = result.counts;
                entry.matchedPacked = std::move(result.matchedPacked);
                entry.lastUpdatedTick = static_cast<uint32_t>(mLastTick);
            } else {
                if (!entry.valid) {
                    entry.valid = false;
                    entry.stale = false;
                    entry.counts = {};
                    entry.subChunkIndexFallbacks = 0;
                    entry.matchedPacked.clear();
                } else {
                    entry.stale = true;
                }
                if (mMap.containsTarget(task.key) && blockSource != nullptr) {
                    const std::optional<uint64_t> activeKey = mActiveTask ? std::optional<uint64_t>(mActiveTask->key) : std::nullopt;
                    mMap.enqueue(task.key, activeKey, true);
                }
            }
        }

        if (i + 1 < mAsyncTasks.size()) {
            mAsyncTasks[i] = std::move(mAsyncTasks.back());
        }
        mAsyncTasks.pop_back();
    }
}

BlockPos ChunkScanner::decodeMatchedPosition(const ChunkPos& chunkPos, const uint32_t packed) const {
    return ChunkMatchCodec::toBlockPos(chunkPos, packed);
}

void ChunkScanner::maybeCalibrateDecode(BlockSource* blockSource, const int tick) {
    (void) blockSource;
    (void) tick;
    // Deep decode is fixed to ChunkMatchCodec's XZY packing and absoluteSubChunkIndex*16+localY.
    mDecodePermutation = 1;
    mDecodeSubChunkDelta = 0;
    mDecodeCalibrationScore = 0;
    mDecodeCalibrationSamples = 0;
}

bool ChunkScanner::startNextTask() {
    uint64_t key = 0;
    while (mMap.popNext(&key)) {
        mMap.markPending(key, false);
        if (!mMap.containsTarget(key)) {
            continue;
        }

        ChunkScanCacheEntry* entryPtr = mMap.findEntry(key);
        if (entryPtr == nullptr) {
            continue;
        }

        ChunkScanCacheEntry& entry = *entryPtr;
        entry.pending = false;

        if (!entry.loaded) {
            entry.valid = true;
            continue;
        }

        if (entry.valid && !entry.stale) {
            continue;
        }

        bool started = false;
        if (useDeepMode()) {
            started = buildDeepTask(key, entry);
        }
        if (!started) {
            started = buildFallbackTask(key, entry);
        }

        if (started) {
            entry.pending = true;
            return true;
        }

        const int subChunkCount = entry.chunkPtr ? entry.chunkPtr->getSubChunkCount() : -1;
        if ((mLastTick - static_cast<int>(entry.lastUpdatedTick)) >= 20) {
            Logger::warn(
                "[ChunkScanner] Failed to start scan task for chunk({}, {}): deepMode={}, fallbackEnabled={}, loaded={}, subChunkCount={}",
                entry.chunkPos.x,
                entry.chunkPos.z,
                useDeepMode(),
                mConfig.fallbackEnabled,
                entry.loaded,
                subChunkCount
            );
            entry.lastUpdatedTick = static_cast<uint32_t>(mLastTick);
        }
        if (!entry.valid) {
            entry.stale = false;
            entry.counts = {};
            entry.matchedPacked.clear();
        }
        entry.pending = false;
    }

    return false;
}

bool ChunkScanner::buildDeepTask(const uint64_t key, ChunkScanCacheEntry& entry) {
    if (!entry.chunkPtr || !useDeepMode()) {
        return false;
    }

    const int subChunkCount = entry.chunkPtr->getSubChunkCount();
    if (subChunkCount < 0 || subChunkCount > kMaxSubChunksSafety) {
        return false;
    }

    ActiveScanTask task{};
    task.key = key;
    task.chunkPos = entry.chunkPos;
    task.chunkPtr = entry.chunkPtr;
    task.mode = ChunkScannerMode::Deep;
    task.totalWork = subChunkCount;
    task.progress = 0;
    task.elementProgress = 0;
    if (mConfig.maxRenderMatchesPerChunk > 0) {
        task.matchedPacked.reserve(static_cast<size_t>(mConfig.maxRenderMatchesPerChunk));
    }

    entry.subChunkCount = static_cast<uint16_t>(subChunkCount);
    mActiveTask = task;
    return true;
}

bool ChunkScanner::buildFallbackTask(const uint64_t key, ChunkScanCacheEntry& entry) {
    (void) key;
    (void) entry;
    // Intentionally disabled: scanner stays deep-only (SubChunkStorage::getElement).
    return false;
}

bool ChunkScanner::processDeepTask(
    const std::function<bool(const Block*)>& isAirResolver,
    int& subChunkBudget,
    int& elementBudget
) {
    if (!mActiveTask || mActiveTask->mode != ChunkScannerMode::Deep) {
        return false;
    }

    while (subChunkBudget > 0 && mActiveTask->progress < mActiveTask->totalWork) {
        const SubChunk* subChunk = mActiveTask->chunkPtr->getSubChunk(mActiveTask->progress);
        const ResolvedSubChunkIndex resolvedSubChunk = resolveSubChunkAbsoluteIndex(
            subChunk,
            mWorldMinY,
            mWorldMaxYExclusive,
            mActiveTask->progress
        );
        const int absoluteSubChunkIndex = resolvedSubChunk.value;
        if (resolvedSubChunk.usedFallback) {
            addWithSaturation(mActiveTask->subChunkIndexFallbacks, 1);
        }
        if (subChunk == nullptr) {
            addWithSaturation(mActiveTask->total, kSubChunkBlockCount);
            addWithSaturation(mActiveTask->air, kSubChunkBlockCount);
            ++mActiveTask->progress;
            mActiveTask->elementProgress = 0;
            --subChunkBudget;
            continue;
        }

        const SubChunkStorage* storage = subChunk->getMainStorage();
        if (storage == nullptr) {
            addWithSaturation(mActiveTask->total, kSubChunkBlockCount);
            addWithSaturation(mActiveTask->air, kSubChunkBlockCount);
            ++mActiveTask->progress;
            mActiveTask->elementProgress = 0;
            --subChunkBudget;
            continue;
        }

        if (mActiveTask->elementProgress == 0) {
            const Block* firstBlock = nullptr;
            if (!storage->tryGetElement(0, &firstBlock)) {
                mActiveTask->failed = true;
                mActiveTask->failureSubChunkIndex = mActiveTask->progress;
                mActiveTask->failureReason = "SubChunkStorage::getElement(0) failed";
                break;
            }

            if (firstBlock == nullptr) {
                addWithSaturation(mActiveTask->total, kSubChunkBlockCount);
                addWithSaturation(mActiveTask->null, kSubChunkBlockCount);
                ++mActiveTask->progress;
                --subChunkBudget;
                continue;
            }

            bool uniform = false;
            const bool hasUniformInfo = storage->tryIsUniform(*firstBlock, &uniform);
            const bool useUniformFastPath = hasUniformInfo && uniform && verifyUniformStorage(storage, firstBlock);
            if (useUniformFastPath) {
                addWithSaturation(mActiveTask->total, kSubChunkBlockCount);
                if (isAirResolver(firstBlock)) {
                    addWithSaturation(mActiveTask->air, kSubChunkBlockCount);
                } else {
                    addWithSaturation(mActiveTask->nonAir, kSubChunkBlockCount);
                }

                if (mMatchFn != nullptr) {
                    const int8_t code = mMatchFn(mMatchCtx, firstBlock);

                    if (code >= 0) {
                        addWithSaturation(mActiveTask->matched, kSubChunkBlockCount);
                        if (code < mMatchSlots) {
                            addWithSaturation(mActiveTask->tokenCounts[static_cast<size_t>(code)], kSubChunkBlockCount);
                        }

                        if (mConfig.maxRenderMatchesPerChunk > 0) {
                            for (uint16_t idx = 0;
                                 idx < kSubChunkBlockCount &&
                                 static_cast<int>(mActiveTask->matchedPacked.size()) < mConfig.maxRenderMatchesPerChunk;
                                 ++idx) {
                                const uint32_t packed = packMatchedIndex(absoluteSubChunkIndex, idx);
                                if (packed != ChunkMatchCodec::kInvalidPacked) {
                                    mActiveTask->matchedPacked.emplace_back(packed);
                                }
                            }
                        }
                    }
                }

                ++mActiveTask->progress;
                mActiveTask->elementProgress = 0;
                --subChunkBudget;
                continue;
            }
        }

        __try {
            while (elementBudget > 0 && mActiveTask->elementProgress < kSubChunkBlockCount) {
                const uint16_t blockIdx = static_cast<uint16_t>(mActiveTask->elementProgress);
                const Block* block = storage->getElement(blockIdx);

                ++mActiveTask->elementProgress;
                --elementBudget;
                addWithSaturation(mActiveTask->total, 1);

                if (block == nullptr) {
                    addWithSaturation(mActiveTask->null, 1);
                    continue;
                }

                if (isAirResolver(block)) {
                    addWithSaturation(mActiveTask->air, 1);
                } else {
                    addWithSaturation(mActiveTask->nonAir, 1);
                }

                if (mMatchFn != nullptr) {
                    const int8_t code = mMatchFn(mMatchCtx, block);
                    if (code >= 0) {
                        addWithSaturation(mActiveTask->matched, 1);
                        if (code < mMatchSlots) {
                            addWithSaturation(mActiveTask->tokenCounts[static_cast<size_t>(code)], 1);
                        }
                        if (static_cast<int>(mActiveTask->matchedPacked.size()) < mConfig.maxRenderMatchesPerChunk) {
                            const uint32_t packed = packMatchedIndex(absoluteSubChunkIndex, blockIdx);
                            if (packed != ChunkMatchCodec::kInvalidPacked) {
                                mActiveTask->matchedPacked.emplace_back(packed);
                            }
                        }
                    }
                }
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            mActiveTask->failed = true;
            mActiveTask->failureSubChunkIndex = mActiveTask->progress;
            mActiveTask->failureReason = "exception while iterating SubChunkStorage::getElement";
            break;
        }

        if (mActiveTask->elementProgress >= kSubChunkBlockCount) {
            ++mActiveTask->progress;
            mActiveTask->elementProgress = 0;
            --subChunkBudget;
            continue;
        }

        break;
    }

    return mActiveTask->progress >= mActiveTask->totalWork;
}

bool ChunkScanner::processFallbackTask(
    BlockSource* blockSource,
    const std::function<bool(const Block*)>& isAirResolver,
    int& blockBudget
) {
    (void) blockSource;
    (void) isAirResolver;
    (void) blockBudget;
    return false;
}

void ChunkScanner::finalizeTask(BlockSource* blockSource, const bool completed) {
    if (!mActiveTask) {
        return;
    }

    const uint64_t key = mActiveTask->key;
    ChunkScanCacheEntry* entryPtr = mMap.findEntry(key);
    if (entryPtr == nullptr) {
        mActiveTask.reset();
        return;
    }

    ChunkScanCacheEntry& entry = *entryPtr;
    entry.pending = false;
    entry.lastUpdatedTick = entry.lastSeenTick;

    if (completed && !mActiveTask->failed) {
        entry.valid = true;
        entry.stale = false;
        entry.loaded = true;
        entry.chunkPtr = mActiveTask->chunkPtr;
        entry.subChunkCount = static_cast<uint16_t>(
            mActiveTask->mode == ChunkScannerMode::Deep ? mActiveTask->totalWork : entry.expectedSubChunkCount
        );
        entry.subChunkIndexFallbacks = static_cast<uint16_t>(
            std::min<uint32_t>(std::numeric_limits<uint16_t>::max(), mActiveTask->subChunkIndexFallbacks)
        );
        entry.counts.total = clampCountToU32(mActiveTask->total);
        entry.counts.air = clampCountToU32(mActiveTask->air);
        entry.counts.nonAir = clampCountToU32(mActiveTask->nonAir);
        entry.counts.null = clampCountToU32(mActiveTask->null);
        entry.counts.matched = clampCountToU32(mActiveTask->matched);
        for (int i = 0; i < kChunkTokenSlots; ++i) {
            entry.counts.tokenCounts[static_cast<size_t>(i)] = static_cast<uint16_t>(
                std::min<uint32_t>(std::numeric_limits<uint16_t>::max(), mActiveTask->tokenCounts[static_cast<size_t>(i)])
            );
        }
        entry.matchedPacked = std::move(mActiveTask->matchedPacked);
    } else {
        Logger::warn(
            "[ChunkScanner] Chunk scan failed for ({}, {}): mode={}, subChunk={}, reason={}",
            mActiveTask->chunkPos.x,
            mActiveTask->chunkPos.z,
            mActiveTask->mode == ChunkScannerMode::Deep ? "Deep" : "Fallback",
            mActiveTask->failureSubChunkIndex,
            mActiveTask->failureReason.empty() ? "unknown" : mActiveTask->failureReason
        );
        if (!entry.valid) {
            entry.valid = false;
            entry.stale = false;
            entry.counts = {};
            entry.subChunkIndexFallbacks = 0;
            entry.matchedPacked.clear();
        } else {
            entry.stale = true;
        }
        if (mMap.containsTarget(key) && blockSource != nullptr) {
            const std::optional<uint64_t> activeKey = mActiveTask ? std::optional<uint64_t>(mActiveTask->key) : std::nullopt;
            mMap.enqueue(key, activeKey, true);
        }
    }

    mActiveTask.reset();
}

void ChunkScanner::tick(BlockSource* blockSource, const int tick, const std::function<bool(const Block*)>& isAirResolver) {
    mRecheckedThisTick = 0;
    mInvalidatedThisTick = 0;
    mLastTick = tick;

    if (blockSource == nullptr || !mHasArea) {
        refreshStats();
        return;
    }

    recheckTargets(blockSource, tick);

    consumeAsyncResults(blockSource);

    const int queuedChunks = mMap.queuedCount() + (mActiveTask ? 1 : 0) + static_cast<int>(mAsyncTasks.size());
    const int targetChunks = std::max(1, static_cast<int>(mMap.targetKeys().size()));
    const int boost = computeBoost(queuedChunks, targetChunks);

    int subChunkBudget = std::clamp(mConfig.maxSubChunksPerTick * boost, 1, 16384);
    int elementBudget = std::clamp(mConfig.maxElementsPerTick * boost, 256, 4 * 1024 * 1024);
    int blockBudget = std::clamp(mConfig.fallbackMaxBlocksPerTick * boost, 512, 4 * 1024 * 1024);

    int microBudget = mConfig.maxScanMicrosPerTick;
    if (queuedChunks > 0) {
        microBudget = std::clamp(
            mConfig.maxScanMicrosPerTick + (boost - 1) * 2000,
            mConfig.maxScanMicrosPerTick,
            mConfig.maxBurstMicrosPerTick
        );
    } else {
        microBudget = std::max(1000, mConfig.maxScanMicrosPerTick / 2);
    }

    const auto tickStart = std::chrono::steady_clock::now();
    while (true) {
        const auto now = std::chrono::steady_clock::now();
        const int64_t elapsedMicros = std::chrono::duration_cast<std::chrono::microseconds>(now - tickStart).count();
        if (elapsedMicros >= microBudget) {
            break;
        }

        if (!mActiveTask) {
            if (!startNextTask()) {
                break;
            }
        }

        if (!mActiveTask) {
            break;
        }

        bool completed = false;
        if (mActiveTask->mode == ChunkScannerMode::Deep) {
            completed = processDeepTask(isAirResolver, subChunkBudget, elementBudget);
        } else {
            completed = processFallbackTask(blockSource, isAirResolver, blockBudget);
        }

        if (mActiveTask && mActiveTask->failed) {
            finalizeTask(blockSource, false);
            continue;
        }

        if (completed) {
            finalizeTask(blockSource, true);
            continue;
        }

        break;
    }

    maybeCalibrateDecode(blockSource, tick);
    refreshStats();
}

void ChunkScanner::refreshStats() {
    mStats = {};
    mStats.centerChunk = mAreaCenter;
    mStats.radius = mAreaRadius;
    mStats.mode = useDeepMode() ? ChunkScannerMode::Deep : ChunkScannerMode::Fallback;

    mStats.targetChunks = static_cast<int>(mMap.targetKeys().size());
    mStats.queuedChunks = mMap.queuedCount() + (mActiveTask ? 1 : 0) + static_cast<int>(mAsyncTasks.size());
    mStats.recheckedChunks = mRecheckedThisTick;
    mStats.invalidatedChunks = mInvalidatedThisTick;
    mStats.decodePermutation = mDecodePermutation;
    mStats.decodeSubChunkDelta = mDecodeSubChunkDelta;
    mStats.decodeCalibrationScore = mDecodeCalibrationScore;
    mStats.decodeCalibrationSamples = mDecodeCalibrationSamples;

    for (const uint64_t key : mMap.targetKeys()) {
        const ChunkScanCacheEntry* entry = mMap.findEntry(key);
        if (entry == nullptr) {
            ++mStats.unloadedChunks;
            ++mStats.completedChunks;
            continue;
        }

        if (!entry->loaded) {
            ++mStats.unloadedChunks;
            ++mStats.completedChunks;
            continue;
        }

        ++mStats.loadedChunks;

        if (mStats.mode == ChunkScannerMode::Deep) {
            const int subCount = std::max(static_cast<int>(entry->subChunkCount), static_cast<int>(entry->expectedSubChunkCount));
            mStats.targetSubChunks += subCount;
            if (entry->valid || entry->stale) {
                mStats.completedSubChunks += subCount;
            } else if (mActiveTask && mActiveTask->key == key && mActiveTask->mode == ChunkScannerMode::Deep) {
                mStats.completedSubChunks += std::min(mActiveTask->progress, mActiveTask->totalWork);
            }
        }

        if (entry->valid || entry->stale) {
            ++mStats.completedChunks;
        }

        if (!(entry->valid || entry->stale)) {
            continue;
        }

        addWithSaturation(mStats.totalBlocks, entry->counts.total);
        addWithSaturation(mStats.airBlocks, entry->counts.air);
        addWithSaturation(mStats.nonAirBlocks, entry->counts.nonAir);
        addWithSaturation(mStats.nullBlocks, entry->counts.null);
        addWithSaturation(mStats.matchedBlocks, entry->counts.matched);
        mStats.subChunkIndexFallbacks += entry->subChunkIndexFallbacks;
        if (entry->counts.matched > 0) {
            ++mStats.matchedChunks;
        }

        for (int i = 0; i < kChunkTokenSlots; ++i) {
            mStats.tokenTotals[static_cast<size_t>(i)] += entry->counts.tokenCounts[static_cast<size_t>(i)];
        }
    }

    mStats.sweepComplete = mStats.targetChunks > 0 &&
                           mStats.completedChunks >= mStats.targetChunks &&
                           mStats.queuedChunks == 0;
}

const ChunkScannerStats& ChunkScanner::stats() const {
    return mStats;
}

std::vector<ChunkRenderEntry> ChunkScanner::buildRenderEntries() const {
    std::vector<ChunkRenderEntry> entries;
    entries.reserve(mMap.targetKeys().size());

    for (const uint64_t key : mMap.targetKeys()) {
        ChunkRenderEntry drawEntry{};
        drawEntry.chunkPos = ChunkScanMap::unpackKey(key);

        const ChunkScanCacheEntry* entry = mMap.findEntry(key);
        if (entry == nullptr) {
            drawEntry.loaded = false;
            drawEntry.hasMatch = false;
            drawEntry.state = ChunkRenderState::Unloaded;
            entries.emplace_back(drawEntry);
            continue;
        }

        drawEntry.loaded = entry->loaded;
        drawEntry.hasMatch = (entry->valid || entry->stale) && entry->counts.matched > 0;
        if (!entry->loaded) {
            drawEntry.state = ChunkRenderState::Unloaded;
        } else if (entry->valid || entry->stale) {
            drawEntry.state = ChunkRenderState::LoadedCached;
        } else {
            drawEntry.state = ChunkRenderState::LoadedPending;
        }

        entries.emplace_back(drawEntry);
    }

    return entries;
}

std::vector<BlockPos> ChunkScanner::buildMatchedBlockRenderList(const size_t maxBlocks) const {
    std::vector<BlockPos> blocks{};
    if (maxBlocks == 0) {
        return blocks;
    }

    blocks.reserve(std::min<size_t>(maxBlocks, 8192));

    for (const uint64_t key : mMap.targetKeys()) {
        if (blocks.size() >= maxBlocks) {
            break;
        }

        const ChunkScanCacheEntry* entry = mMap.findEntry(key);
        if (entry == nullptr || !entry->loaded || !entry->valid || entry->stale || entry->matchedPacked.empty()) {
            continue;
        }

        for (const uint32_t packed : entry->matchedPacked) {
            if (blocks.size() >= maxBlocks) {
                break;
            }
            blocks.emplace_back(decodeMatchedPosition(entry->chunkPos, packed));
        }
    }

    return blocks;
}

int ChunkScanner::expectedSubChunkCount() const {
    const int height = mWorldMaxYExclusive - mWorldMinY;
    if (height <= 0) {
        return 0;
    }

    return (height + (kChunkSize - 1)) / kChunkSize;
}

bool ChunkScanner::useDeepMode() const {
    return mConfig.deepEnabled;
}

uint32_t ChunkScanner::clampCountToU32(const int64_t value) {
    if (value <= 0) {
        return 0;
    }

    const int64_t maxValue = static_cast<int64_t>(std::numeric_limits<uint32_t>::max());
    return static_cast<uint32_t>(std::min(value, maxValue));
}
