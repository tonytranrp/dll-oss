#include "PathFinding.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <format>
#include <sstream>

#include "SDK/SDK.hpp"
#include "SDK/Client/Block/Block.hpp"
#include "SDK/Client/Block/BlockLegacy.hpp"
#include "SDK/Client/Block/BlockSource.hpp"
#include "SDK/Client/Level/LevelChunk.hpp"
#include "SDK/Client/Level/SubChunk.hpp"
#include "SDK/Client/Level/SubChunkStorage.hpp"
#include "Utils/Logger/Logger.hpp"
#include "Utils/Memory/Game/SignatureAndOffsetManager.hpp"
#include "Utils/Render/DrawUtil3D.hpp"
#include "Utils/Render/MaterialUtils.hpp"

namespace {
constexpr std::array<std::pair<int, int>, 4> kSpiralDirections = {
    std::pair{1, 0},
    std::pair{0, 1},
    std::pair{-1, 0},
    std::pair{0, -1},
};

std::string canonicalizeBlockId(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](const unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });

    if (value.rfind("minecraft:", 0) == 0) {
        value.erase(0, 10);
    }
    if (value.rfind("tile.", 0) == 0) {
        value.erase(0, 5);
    }

    const size_t colon = value.rfind(':');
    if (colon != std::string::npos && (colon + 1) < value.size()) {
        value = value.substr(colon + 1);
    }

    if (value.rfind("tile.", 0) == 0) {
        value.erase(0, 5);
    }

    if (const size_t statePos = value.find('['); statePos != std::string::npos) {
        value.erase(statePos);
    }

    return value;
}

bool tokenMatches(
    const std::string& token,
    const std::string& canonicalToken,
    const std::string& shortName,
    const std::string& fullName,
    const std::string& canonicalShort,
    const std::string& canonicalFull
) {
    if (token.empty()) {
        return false;
    }

    if (token == shortName || token == fullName) {
        return true;
    }

    if (!canonicalToken.empty() &&
        (canonicalToken == canonicalShort || canonicalToken == canonicalFull)) {
        return true;
    }

    if (shortName.find(token) != std::string::npos || fullName.find(token) != std::string::npos) {
        return true;
    }

    if (!canonicalToken.empty() &&
        (canonicalShort.find(canonicalToken) != std::string::npos ||
         canonicalFull.find(canonicalToken) != std::string::npos)) {
        return true;
    }

    return false;
}
}

std::string PathFindingModule::normalizeToken(std::string value) {
    value.erase(
        std::remove_if(value.begin(), value.end(), [](const unsigned char c) { return std::isspace(c) != 0; }),
        value.end()
    );
    std::transform(value.begin(), value.end(), value.begin(), [](const unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

int PathFindingModule::chebyshevDistance(const ChunkPos& a, const ChunkPos& b) {
    return std::max(std::abs(a.x - b.x), std::abs(a.z - b.z));
}

uint64_t PathFindingModule::packBlockKey(const BlockPos& pos) {
    const uint64_t x = static_cast<uint64_t>(static_cast<uint32_t>(pos.x) & 0x3FFFFFF);
    const uint64_t y = static_cast<uint64_t>(static_cast<uint32_t>(pos.y) & 0xFFF);
    const uint64_t z = static_cast<uint64_t>(static_cast<uint32_t>(pos.z) & 0x3FFFFFF);
    return (x << 38ULL) | (z << 12ULL) | y;
}

D2D_COLOR_F PathFindingModule::colorForTokenSlot(const int tokenSlot) {
    static constexpr std::array<D2D_COLOR_F, kMaxTrackedTokens> kColors = {
        D2D_COLOR_F{0.08f, 0.95f, 0.95f, 0.95f},
        D2D_COLOR_F{0.16f, 0.75f, 1.00f, 0.95f},
        D2D_COLOR_F{1.00f, 0.82f, 0.15f, 0.95f},
        D2D_COLOR_F{0.95f, 0.35f, 0.20f, 0.95f},
    };
    if (tokenSlot < 0 || tokenSlot >= static_cast<int>(kColors.size())) {
        return kColors[0];
    }
    return kColors[static_cast<size_t>(tokenSlot)];
}

void PathFindingModule::resetRuntime(const bool clearCache) {
    hasSearchCenter = false;

    worldMinY = -64;
    worldMaxYExclusive = 320;
    worldSubChunkCount = 24;

    searchCenter = {};
    playerChunkPos = {};
    currentChunkPos = {};
    currentSubChunkIndex = 0;
    directionIndex = 0;
    steps = 1;
    stepsProgress = 0;

    autoRadius = 8;
    pendingRadius = -1;
    pendingRadiusHits = 0;
    lastProbeTick = 0;
    lastLogTick = 0;
    scanIntervalCounter = 0;
    lastMatcherDiagTick = 0;

    if (clearCache) {
        blockMatchCache.clear();
        foundBlocks.clear();
        subChunkHits.clear();
        emptyScanStreak.clear();
        renderBlocks.clear();
        stats = {};
    } else {
        renderBlocks.clear();
        stats.renderedBlocksLastFrame = 0;
        stats.scannedSubChunksThisTick = 0;
        stats.loadedChunkHitsThisTick = 0;
        stats.unloadedChunkHitsThisTick = 0;
        emptyScanStreak.clear();
    }

    statusLine = "Waiting for world context...";
}

void PathFindingModule::resetSpiral(const ChunkPos& centerChunk, const bool clearCache) {
    searchCenter = centerChunk;
    currentChunkPos = centerChunk;
    currentSubChunkIndex = 0;
    directionIndex = 0;
    steps = 1;
    stepsProgress = 0;
    hasSearchCenter = true;

    if (clearCache) {
        blockMatchCache.clear();
        foundBlocks.clear();
        subChunkHits.clear();
        renderBlocks.clear();
        stats.tokenTotals = {};
    }
}

bool PathFindingModule::isEnvironmentReady() const {
    if (!SDK::hasInstanced || !SDK::clientInstance) {
        return false;
    }

    auto* player = SDK::clientInstance->getLocalPlayer();
    if (player == nullptr || player->getPosition() == nullptr) {
        return false;
    }

    return SDK::clientInstance->getBlockSource() != nullptr;
}

void PathFindingModule::validateOffsets() {
    if (offsetsLogged) {
        return;
    }

    const int getChunkXZ = GET_OFFSET("BlockSource::getChunkXZ");
    const int getMinHeight = GET_OFFSET("BlockSource::getMinHeight");
    const int getMaxHeight = GET_OFFSET("BlockSource::getMaxHeight");
    const int levelChunkSubChunks = GET_OFFSET("LevelChunk::mSubChunks");
    const int subChunkBlocksReadPtr = GET_OFFSET("SubChunk::mBlocksReadPtr");
    const int subChunkAbsIndex = GET_OFFSET("SubChunk::mAbsoluteIndex");
    const int storageGetElement = GET_OFFSET("SubChunkStorage::getElement");
    const int blockLegacyPtr = GET_OFFSET("Block::blockLegacy");
    const int blockLegacyName = GET_OFFSET("BlockLegacy::name");
    const int blockLegacyNamespace = GET_OFFSET("BlockLegacy::namespace");

    offsetsValid = getChunkXZ >= 0 &&
                   getMinHeight >= 0 &&
                   getMaxHeight >= 0 &&
                   levelChunkSubChunks > 0 &&
                   subChunkBlocksReadPtr > 0 &&
                   subChunkAbsIndex > 0 &&
                   storageGetElement >= 0 &&
                   blockLegacyPtr > 0 &&
                   blockLegacyName > 0;

    Logger::info("[PathFinding] BlockSource::getChunkXZ   index={} valid={}", getChunkXZ, getChunkXZ >= 0);
    Logger::info("[PathFinding] BlockSource::getMinHeight index={} valid={}", getMinHeight, getMinHeight >= 0);
    Logger::info("[PathFinding] BlockSource::getMaxHeight index={} valid={}", getMaxHeight, getMaxHeight >= 0);
    Logger::info(
        "[PathFinding] LevelChunk::mSubChunks   offset=0x{:X} valid={}",
        levelChunkSubChunks,
        levelChunkSubChunks > 0
    );
    Logger::info(
        "[PathFinding] SubChunk::mBlocksReadPtr offset=0x{:X} valid={}",
        subChunkBlocksReadPtr,
        subChunkBlocksReadPtr > 0
    );
    Logger::info(
        "[PathFinding] SubChunk::mAbsoluteIndex offset=0x{:X} valid={}",
        subChunkAbsIndex,
        subChunkAbsIndex > 0
    );
    Logger::info(
        "[PathFinding] SubChunkStorage::getElement index={} valid={}",
        storageGetElement,
        storageGetElement >= 0
    );
    Logger::info(
        "[PathFinding] Block::blockLegacy     offset=0x{:X} valid={}",
        blockLegacyPtr,
        blockLegacyPtr > 0
    );
    Logger::info(
        "[PathFinding] BlockLegacy::name      offset=0x{:X} valid={}",
        blockLegacyName,
        blockLegacyName > 0
    );
    Logger::info(
        "[PathFinding] BlockLegacy::namespace offset=0x{:X} valid={}",
        blockLegacyNamespace,
        blockLegacyNamespace > 0
    );

    if (!offsetsValid) {
        Logger::warn("[PathFinding] Deep scan offsets invalid, scanner paused.");
    }

    offsetsLogged = true;
}

void PathFindingModule::updateWorldBounds(BlockSource* blockSource) {
    if (blockSource == nullptr) {
        return;
    }

    int minY = -64;
    int maxY = 320;

    __try {
        minY = static_cast<int>(blockSource->getMinHeight());
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        minY = -64;
    }

    __try {
        maxY = static_cast<int>(blockSource->getMaxHeight());
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        maxY = 320;
    }

    if (maxY <= minY || (maxY - minY) > 4096) {
        minY = -64;
        maxY = 320;
    }

    worldMinY = minY;
    worldMaxYExclusive = maxY;
    worldSubChunkCount = std::clamp((worldMaxYExclusive - worldMinY + 15) / 16, 1, 64);
}

int PathFindingModule::detectAutoRadius(BlockSource* blockSource, const ChunkPos& centerChunk) const {
    if (blockSource == nullptr) {
        return autoRadius;
    }

    constexpr int maxProbe = 48;
    int loadedRadius = 1;
    int emptyRings = 0;

    for (int r = 1; r <= maxProbe; ++r) {
        bool foundInRing = false;

        for (int x = -r; x <= r; ++x) {
            if (blockSource->getChunk(centerChunk.x + x, centerChunk.z + r) != nullptr ||
                blockSource->getChunk(centerChunk.x + x, centerChunk.z - r) != nullptr) {
                foundInRing = true;
                break;
            }
        }

        if (!foundInRing) {
            for (int z = -r + 1; z <= r - 1; ++z) {
                if (blockSource->getChunk(centerChunk.x + r, centerChunk.z + z) != nullptr ||
                    blockSource->getChunk(centerChunk.x - r, centerChunk.z + z) != nullptr) {
                    foundInRing = true;
                    break;
                }
            }
        }

        if (foundInRing) {
            loadedRadius = r;
            emptyRings = 0;
        } else {
            ++emptyRings;
            if (emptyRings >= 2 && r > loadedRadius + 1) {
                break;
            }
        }
    }

    return std::max(1, loadedRadius);
}

void PathFindingModule::updateAutoRadius(BlockSource* blockSource) {
    const int probeInterval = std::clamp(getOps<int>("probeIntervalTicks"), 5, 200);
    if (static_cast<int>(stats.tick) - lastProbeTick < probeInterval) {
        return;
    }

    lastProbeTick = static_cast<int>(stats.tick);
    const int detected = detectAutoRadius(blockSource, playerChunkPos);
    const int delta = std::abs(detected - autoRadius);

    if (delta >= 2) {
        autoRadius = detected;
        pendingRadius = -1;
        pendingRadiusHits = 0;
        resetSpiral(playerChunkPos, false);
        statusLine = std::format("Adjusted auto radius to {}", autoRadius);
        return;
    }

    if (delta == 1) {
        if (pendingRadius == detected) {
            ++pendingRadiusHits;
        } else {
            pendingRadius = detected;
            pendingRadiusHits = 1;
        }

        if (pendingRadiusHits >= 3) {
            autoRadius = detected;
            pendingRadius = -1;
            pendingRadiusHits = 0;
            resetSpiral(playerChunkPos, false);
            statusLine = std::format("Adjusted auto radius to {}", autoRadius);
        }
        return;
    }

    pendingRadius = -1;
    pendingRadiusHits = 0;
}

void PathFindingModule::parseTrackedTokens() {
    const std::string csv = getOps<std::string>("goalBlocks");
    if (csv == lastTokenCsv) {
        return;
    }

    lastTokenCsv = csv;
    trackedTokens = {};
    trackedCanonicalTokens = {};
    trackedTokenCount = 0;

    std::stringstream stream(csv);
    std::string token;
    while (std::getline(stream, token, ',') && trackedTokenCount < kMaxTrackedTokens) {
        token = normalizeToken(token);
        if (token.empty()) {
            continue;
        }

        bool duplicate = false;
        for (int i = 0; i < trackedTokenCount; ++i) {
            if (trackedTokens[static_cast<size_t>(i)] == token) {
                duplicate = true;
                break;
            }
        }

        if (duplicate) {
            continue;
        }

        trackedTokens[static_cast<size_t>(trackedTokenCount)] = token;
        trackedCanonicalTokens[static_cast<size_t>(trackedTokenCount)] = canonicalizeBlockId(token);
        ++trackedTokenCount;
    }

    blockMatchCache.clear();
    foundBlocks.clear();
    subChunkHits.clear();
    emptyScanStreak.clear();
    renderBlocks.clear();
    stats.tokenTotals = {};

    Logger::info("[PathFinding] Tracked block tokens: {}", buildTokenSummary());
}

void PathFindingModule::clearSubChunkHitCache(const SubChunkCoord& key) {
    const auto it = subChunkHits.find(key);
    if (it == subChunkHits.end()) {
        return;
    }

    for (const uint64_t blockKey : it->second) {
        const auto foundIt = foundBlocks.find(blockKey);
        if (foundIt == foundBlocks.end()) {
            continue;
        }

        const int slot = static_cast<int>(foundIt->second.tokenSlot);
        if (slot >= 0 && slot < kMaxTrackedTokens && stats.tokenTotals[static_cast<size_t>(slot)] > 0) {
            --stats.tokenTotals[static_cast<size_t>(slot)];
        }

        foundBlocks.erase(foundIt);
    }

    subChunkHits.erase(it);
}

int8_t PathFindingModule::classifyBlock(const Block* block) {
    if (block == nullptr || trackedTokenCount <= 0) {
        return -1;
    }

    const auto cacheIt = blockMatchCache.find(block);
    if (cacheIt != blockMatchCache.end()) {
        return cacheIt->second;
    }

    int8_t result = -1;
    std::string shortName{};
    std::string nameSpace{};

    BlockLegacy* legacy = const_cast<Block*>(block)->getBlockLegacy();
    if (legacy != nullptr) {
        shortName = normalizeToken(legacy->getName());
        nameSpace = normalizeToken(legacy->getNamespace());
    }

    if (!shortName.empty()) {
        if (const size_t sep = shortName.find(':'); sep != std::string::npos) {
            if (nameSpace.empty() && sep > 0) {
                nameSpace = shortName.substr(0, sep);
            }
            if ((sep + 1) < shortName.size()) {
                shortName = shortName.substr(sep + 1);
            }
        }

        const std::string fullName = nameSpace.empty() ? shortName : std::format("{}:{}", nameSpace, shortName);
        const std::string canonicalShort = canonicalizeBlockId(shortName);
        const std::string canonicalFull = canonicalizeBlockId(fullName);

        for (int i = 0; i < trackedTokenCount; ++i) {
            const std::string& token = trackedTokens[static_cast<size_t>(i)];
            const std::string& canonicalToken = trackedCanonicalTokens[static_cast<size_t>(i)];
            if (token.empty()) {
                continue;
            }

            if (tokenMatches(token, canonicalToken, shortName, fullName, canonicalShort, canonicalFull)) {
                result = static_cast<int8_t>(i);
                break;
            }
        }
    }

    if (result < 0 && static_cast<int>(stats.tick) - lastMatcherDiagTick >= 240) {
        Logger::info(
            "[PathFinding] Matcher sample miss: name='{}' namespace='{}' ptr=0x{:X}",
            shortName,
            nameSpace,
            reinterpret_cast<uintptr_t>(block)
        );
        lastMatcherDiagTick = static_cast<int>(stats.tick);
    }

    blockMatchCache.emplace(block, result);
    return result;
}

bool PathFindingModule::scanCurrentSubChunk(BlockSource* blockSource) {
    if (blockSource == nullptr || !hasSearchCenter) {
        return false;
    }

    LevelChunk* chunk = blockSource->getChunk(currentChunkPos.x, currentChunkPos.z);
    if (chunk == nullptr) {
        ++stats.unloadedChunkHitsThisTick;
        currentSubChunkIndex = std::max(0, worldSubChunkCount - 1);
        return false;
    }
    ++stats.loadedChunkHitsThisTick;

    std::vector<SubChunk>* subChunks = chunk->tryGetSubChunks();
    if (subChunks == nullptr || subChunks->empty()) {
        currentSubChunkIndex = std::max(0, worldSubChunkCount - 1);
        return false;
    }

    const int availableSubChunks = std::clamp(static_cast<int>(subChunks->size()), 0, 64);
    if (currentSubChunkIndex < 0 || currentSubChunkIndex >= availableSubChunks) {
        currentSubChunkIndex = std::max(0, worldSubChunkCount - 1);
        return false;
    }

    SubChunk* subChunk = chunk->getSubChunk(currentSubChunkIndex);
    if (subChunk == nullptr) {
        return false;
    }

    SubChunkStorage* storage = subChunk->getMainStorage();
    if (storage == nullptr) {
        return false;
    }

    const int fallbackAbsolute = ChunkMath::floorDiv(worldMinY, 16) + currentSubChunkIndex;
    const int absoluteSubChunk = subChunk->getAbsoluteIndexOrFallback(fallbackAbsolute);
    const SubChunkCoord subKey{currentChunkPos.x, currentChunkPos.z, absoluteSubChunk};

    struct PendingMatch {
        uint64_t key = 0;
        BlockPos pos{};
        uint8_t tokenSlot = 0;
    };

    std::vector<PendingMatch> pending{};
    pending.reserve(12);

    const Block* sample = nullptr;
    if (!storage->tryGetElement(0, &sample) || sample == nullptr) {
        return false;
    }

    bool isUniform = false;
    if (storage->tryIsUniform(*sample, &isUniform) && isUniform) {
        const int8_t uniformToken = classifyBlock(sample);
        if (uniformToken < 0 || uniformToken >= kMaxTrackedTokens) {
            ++stats.scannedSubChunksThisTick;
            return true;
        }

        // Rare path: uniform tracked block. Keep exact per-block rendering by emitting all 4096 cells.
        for (int localX = 0; localX < 16; ++localX) {
            for (int localZ = 0; localZ < 16; ++localZ) {
                for (int localY = 0; localY < 16; ++localY) {
                    const BlockPos pos{
                        currentChunkPos.x * 16 + localX,
                        absoluteSubChunk * 16 + localY,
                        currentChunkPos.z * 16 + localZ
                    };
                    const uint64_t blockKey = packBlockKey(pos);
                    pending.push_back(PendingMatch{
                        .key = blockKey,
                        .pos = pos,
                        .tokenSlot = static_cast<uint8_t>(uniformToken)
                    });
                }
            }
        }
    } else {
        for (int localX = 0; localX < 16; ++localX) {
            for (int localZ = 0; localZ < 16; ++localZ) {
                for (int localY = 0; localY < 16; ++localY) {
                    const uint16_t elementId = static_cast<uint16_t>(((localX * 16) + localZ) * 16 + (localY & 0xF));
                    const Block* block = storage->getElement(elementId);
                    if (block == nullptr) {
                        continue;
                    }
                    const int8_t tokenSlot = classifyBlock(block);
                    if (tokenSlot < 0 || tokenSlot >= kMaxTrackedTokens) {
                        continue;
                    }

                    const BlockPos pos{
                        currentChunkPos.x * 16 + localX,
                        absoluteSubChunk * 16 + localY,
                        currentChunkPos.z * 16 + localZ
                    };

                    pending.push_back(PendingMatch{
                        .key = packBlockKey(pos),
                        .pos = pos,
                        .tokenSlot = static_cast<uint8_t>(tokenSlot)
                    });
                }
            }
        }
    }

    const bool hadPreviousHits = subChunkHits.find(subKey) != subChunkHits.end();
    if (pending.empty()) {
        if (hadPreviousHits) {
            const uint8_t prev = emptyScanStreak[subKey];
            const uint8_t next = static_cast<uint8_t>(std::min<int>(255, static_cast<int>(prev) + 1));
            emptyScanStreak[subKey] = next;
            if (next < 2) {
                ++stats.scannedSubChunksThisTick;
                return true;
            }
        }
    } else {
        emptyScanStreak.erase(subKey);
    }

    clearSubChunkHitCache(subKey);

    if (!pending.empty()) {
        std::vector<uint64_t> hits{};
        hits.reserve(pending.size());

        for (const PendingMatch& match : pending) {
            foundBlocks[match.key] = FoundBlockEntry{match.pos, match.tokenSlot};
            hits.emplace_back(match.key);
            ++stats.tokenTotals[static_cast<size_t>(match.tokenSlot)];
        }

        subChunkHits[subKey] = std::move(hits);
    } else {
        emptyScanStreak.erase(subKey);
    }

    ++stats.scannedSubChunksThisTick;
    return true;
}

void PathFindingModule::moveToNextSpiralSubChunk() {
    if (!hasSearchCenter) {
        return;
    }

    if (currentSubChunkIndex + 1 < worldSubChunkCount) {
        ++currentSubChunkIndex;
        return;
    }

    currentSubChunkIndex = 0;

    currentChunkPos.x += kSpiralDirections[static_cast<size_t>(directionIndex)].first;
    currentChunkPos.z += kSpiralDirections[static_cast<size_t>(directionIndex)].second;

    ++stepsProgress;
    if (stepsProgress >= steps) {
        stepsProgress = 0;
        directionIndex = (directionIndex + 1) % static_cast<int>(kSpiralDirections.size());
        if ((directionIndex % 2) == 0) {
            ++steps;
        }
    }

    if (chebyshevDistance(currentChunkPos, searchCenter) > autoRadius) {
        resetSpiral(playerChunkPos, true);
        statusLine = "Restarted spiral scan cycle.";
    }
}

void PathFindingModule::rebuildRenderCache() {
    renderBlocks.clear();

    const int maxRender = std::max(0, getOps<int>("maxRenderBlocks"));
    const int renderRadius = std::max(0, getOps<int>("renderRadius"));

    auto* player = SDK::clientInstance ? SDK::clientInstance->getLocalPlayer() : nullptr;
    if (player == nullptr || player->getPosition() == nullptr) {
        return;
    }

    const Vec3<float>* pos = player->getPosition();
    const int playerX = static_cast<int>(std::floor(pos->x));
    const int playerY = static_cast<int>(std::floor(pos->y));
    const int playerZ = static_cast<int>(std::floor(pos->z));
    const int64_t radiusSq = static_cast<int64_t>(renderRadius) * static_cast<int64_t>(renderRadius);

    const size_t reserveSize = (maxRender == 0)
                                   ? foundBlocks.size()
                                   : std::min(foundBlocks.size(), static_cast<size_t>(maxRender));
    renderBlocks.reserve(reserveSize);

    for (const auto& [key, entry] : foundBlocks) {
        (void) key;
        if (renderRadius > 0) {
            const int64_t dx = static_cast<int64_t>(entry.pos.x - playerX);
            const int64_t dy = static_cast<int64_t>(entry.pos.y - playerY);
            const int64_t dz = static_cast<int64_t>(entry.pos.z - playerZ);
            const int64_t distSq = dx * dx + dy * dy + dz * dz;
            if (distSq > radiusSq) {
                continue;
            }
        }

        renderBlocks.emplace_back(entry.pos);
        if (maxRender > 0 && static_cast<int>(renderBlocks.size()) >= maxRender) {
            break;
        }
    }
}

std::string PathFindingModule::buildTokenSummary() const {
    if (trackedTokenCount <= 0) {
        return "none";
    }

    std::string out{};
    for (int i = 0; i < trackedTokenCount; ++i) {
        if (i > 0) {
            out += "  ";
        }
        out += std::format(
            "{}={}",
            trackedTokens[static_cast<size_t>(i)],
            stats.tokenTotals[static_cast<size_t>(i)]
        );
    }
    return out;
}

void PathFindingModule::maybeLogProgress() {
    const int logInterval = std::clamp(getOps<int>("logIntervalTicks"), 10, 600);
    if (static_cast<int>(stats.tick) - lastLogTick < logInterval) {
        return;
    }

    Logger::info(
        "[PathFinding] BlockESP scan: center({}, {}), radius={}, scannedSubChunks/tick={}, foundBlocks={}, tokenCounts=[{}], loadedHits={}, unloadedHits={}",
        searchCenter.x,
        searchCenter.z,
        autoRadius,
        stats.scannedSubChunksThisTick,
        foundBlocks.size(),
        buildTokenSummary(),
        stats.loadedChunkHitsThisTick,
        stats.unloadedChunkHitsThisTick
    );

    lastLogTick = static_cast<int>(stats.tick);
}

void PathFindingModule::onEnable() {
    resetRuntime(true);

    Listen(this, TickEvent, &PathFindingModule::onTick)
    Listen(this, Render3DEvent, &PathFindingModule::onRender3D)
    Listen(this, RenderEvent, &PathFindingModule::onRender)
    Module::onEnable();

    Logger::info("[PathFinding] Enabled");
}

void PathFindingModule::onDisable() {
    Deafen(this, TickEvent, &PathFindingModule::onTick)
    Deafen(this, Render3DEvent, &PathFindingModule::onRender3D)
    Deafen(this, RenderEvent, &PathFindingModule::onRender)
    Module::onDisable();

    resetRuntime(true);
}

void PathFindingModule::defaultConfig() {
    Module::defaultConfig("core");
    Module::defaultConfig("pos");
    Module::defaultConfig("main");
    Module::defaultConfig("text");
    Module::defaultConfig("colors");
    Module::defaultConfig("misc");

    setDef("goalBlocks", static_cast<std::string>("diamond_ore,deepslate_diamond_ore,diamond_block"));
    setDef("scanIntervalTicks", 1);
    setDef("subChunksPerTick", 12);
    setDef("probeIntervalTicks", 20);
    setDef("logIntervalTicks", 80);

    setDef("drawFoundBlocks", true);
    setDef("drawFilledBoxes", false);
    setDef("renderRadius", 160);
    setDef("maxRenderBlocks", 8000);
    setDef("drawScannerChunk", false);

    setDef("showHud", true);
    setDef("showProgress", true);
    setDef("text", static_cast<std::string>("PathFinding"));
    setDef("textalignment", static_cast<std::string>("Left"));
    setDef("textscale", 0.85f);
    setDef("percentageX", 0.02f);
    setDef("percentageY", 0.24f);
    setDef("showBg", true);
    setDef("border", false);
    setDef("glow", false);
}

void PathFindingModule::settingsRender(float settingsOffset) {
    (void) settingsOffset;
    initSettingsPage();

    addHeader("Target");
    addTextBox("Block Tokens", "Comma-separated block names or full ids.", 180, "goalBlocks");

    addHeader("Scanner");
    addElementText("Auto Radius", "Detected from currently loaded chunks.");
    addSliderInt("Scan Interval (ticks)", "Run scanning every N ticks.", "scanIntervalTicks", 20, 1);
    addSliderInt("SubChunks / Scan", "Subchunk budget each scan tick.", "subChunksPerTick", 1024, 1);
    addSliderInt("Probe Interval (ticks)", "Auto-radius refresh cadence.", "probeIntervalTicks", 200, 5);
    addSliderInt("Log Interval (ticks)", "Progress log throttle.", "logIntervalTicks", 600, 10);

    addHeader("Rendering");
    addToggle("Draw Found Blocks", "Render boxes on matched blocks.", "drawFoundBlocks");
    addConditionalToggle(getOps<bool>("drawFoundBlocks"), "Draw Filled", "Fill faces using ui_fill_color material.", "drawFilledBoxes");
    addConditionalSliderInt(getOps<bool>("drawFoundBlocks"), "Render Radius", "0 = no radius culling.", "renderRadius", 512, 0);
    addConditionalSliderInt(getOps<bool>("drawFoundBlocks"), "Max Rendered Blocks", "0 = render all cached matches.", "maxRenderBlocks", 100000, 0);
    addToggle("Draw Scanner Chunk", "Show current scanner chunk on bedrock level.", "drawScannerChunk");

    addHeader("HUD");
    addToggle("Show Progress", "", "showProgress");
    addToggle("Show HUD", "", "showHud");

    FlarialGUI::UnsetScrollView();
    resetPadding();
}

void PathFindingModule::onTick(TickEvent& event) {
    (void) event;
    if (!isEnabled()) {
        return;
    }

    ++stats.tick;
    stats.scannedSubChunksThisTick = 0;
    stats.loadedChunkHitsThisTick = 0;
    stats.unloadedChunkHitsThisTick = 0;

    if (!isEnvironmentReady()) {
        statusLine = "Waiting for world context...";
        return;
    }

    validateOffsets();
    if (!offsetsValid) {
        statusLine = "Invalid offsets for deep scanner.";
        return;
    }

    auto* blockSource = SDK::clientInstance->getBlockSource();
    auto* player = SDK::clientInstance->getLocalPlayer();
    const Vec3<float>* playerPos = player->getPosition();
    const BlockPos playerBlockPos{
        static_cast<int>(std::floor(playerPos->x)),
        static_cast<int>(std::floor(playerPos->y)),
        static_cast<int>(std::floor(playerPos->z))
    };
    playerChunkPos = ChunkPos::fromBlockPos(playerBlockPos);

    updateWorldBounds(blockSource);
    parseTrackedTokens();
    updateAutoRadius(blockSource);

    if (!hasSearchCenter) {
        resetSpiral(playerChunkPos, true);
    }

    ++scanIntervalCounter;
    const int scanInterval = std::clamp(getOps<int>("scanIntervalTicks"), 1, 20);
    if (scanIntervalCounter >= scanInterval) {
        scanIntervalCounter = 0;

        const int subChunkBudget = std::clamp(getOps<int>("subChunksPerTick"), 1, 1024);
        for (int i = 0; i < subChunkBudget; ++i) {
            scanCurrentSubChunk(blockSource);
            moveToNextSpiralSubChunk();
        }
    }

    rebuildRenderCache();
    stats.autoRadius = autoRadius;
    statusLine = std::format("Scanning loaded chunks for {} token(s)", trackedTokenCount);
    maybeLogProgress();
}

void PathFindingModule::onRender3D(Render3DEvent& event) {
    if (!isEnabled() || !isEnvironmentReady()) {
        stats.renderedBlocksLastFrame = 0;
        return;
    }

    const bool drawBlocks = getOps<bool>("drawFoundBlocks");
    const bool drawFilled = getOps<bool>("drawFilledBoxes");
    const bool drawScannerChunk = getOps<bool>("drawScannerChunk");
    stats.renderedBlocksLastFrame = 0;

    if (drawScannerChunk && hasSearchCenter) {
        MCDrawUtil3D chunkDrawer(event.LevelRenderer, event.screenContext, MaterialUtils::getUIFillColor());
        const D2D_COLOR_F color{1.0f, 1.0f, 1.0f, 0.85f};
        const float y = static_cast<float>(worldMinY) + 0.02f;
        const float minX = static_cast<float>(currentChunkPos.x * 16);
        const float minZ = static_cast<float>(currentChunkPos.z * 16);
        const float maxX = minX + 16.0f;
        const float maxZ = minZ + 16.0f;
        chunkDrawer.drawLineList(Vec3<float>{minX, y, minZ}, Vec3<float>{maxX, y, minZ}, color);
        chunkDrawer.drawLineList(Vec3<float>{maxX, y, minZ}, Vec3<float>{maxX, y, maxZ}, color);
        chunkDrawer.drawLineList(Vec3<float>{maxX, y, maxZ}, Vec3<float>{minX, y, maxZ}, color);
        chunkDrawer.drawLineList(Vec3<float>{minX, y, maxZ}, Vec3<float>{minX, y, minZ}, color);
        chunkDrawer.flush();
    }

    if (!drawBlocks || renderBlocks.empty()) {
        return;
    }

    MCDrawUtil3D lineDrawer(event.LevelRenderer, event.screenContext, MaterialUtils::getUIFillColor());
    MCDrawUtil3D fillDrawer(event.LevelRenderer, event.screenContext, MaterialUtils::getUIFillColor());

    for (const BlockPos& pos : renderBlocks) {
        const auto foundIt = foundBlocks.find(packBlockKey(pos));
        const D2D_COLOR_F color = (foundIt == foundBlocks.end())
                                      ? colorForTokenSlot(0)
                                      : colorForTokenSlot(foundIt->second.tokenSlot);

        const float x = static_cast<float>(pos.x);
        const float y = static_cast<float>(pos.y);
        const float z = static_cast<float>(pos.z);
        const float i0 = 0.02f;
        const float i1 = 0.98f;

        const Vec3<float> p000{x + i0, y + i0, z + i0};
        const Vec3<float> p001{x + i0, y + i0, z + i1};
        const Vec3<float> p010{x + i0, y + i1, z + i0};
        const Vec3<float> p011{x + i0, y + i1, z + i1};
        const Vec3<float> p100{x + i1, y + i0, z + i0};
        const Vec3<float> p101{x + i1, y + i0, z + i1};
        const Vec3<float> p110{x + i1, y + i1, z + i0};
        const Vec3<float> p111{x + i1, y + i1, z + i1};

        lineDrawer.drawLineList(p000, p100, color);
        lineDrawer.drawLineList(p100, p101, color);
        lineDrawer.drawLineList(p101, p001, color);
        lineDrawer.drawLineList(p001, p000, color);

        lineDrawer.drawLineList(p010, p110, color);
        lineDrawer.drawLineList(p110, p111, color);
        lineDrawer.drawLineList(p111, p011, color);
        lineDrawer.drawLineList(p011, p010, color);

        lineDrawer.drawLineList(p000, p010, color);
        lineDrawer.drawLineList(p100, p110, color);
        lineDrawer.drawLineList(p101, p111, color);
        lineDrawer.drawLineList(p001, p011, color);

        if (drawFilled) {
            const D2D_COLOR_F fillColor{color.r, color.g, color.b, 0.12f};
            fillDrawer.fillQuad(p000, p100, p101, p001, fillColor);
            fillDrawer.fillQuad(p010, p110, p111, p011, fillColor);
            fillDrawer.fillQuad(p000, p100, p110, p010, fillColor);
            fillDrawer.fillQuad(p001, p101, p111, p011, fillColor);
            fillDrawer.fillQuad(p000, p001, p011, p010, fillColor);
            fillDrawer.fillQuad(p100, p101, p111, p110, fillColor);
        }

        ++stats.renderedBlocksLastFrame;
    }

    if (drawFilled) {
        fillDrawer.flush();
    }
    lineDrawer.flush();
}

void PathFindingModule::onRender(RenderEvent& event) {
    (void) event;
    if (!isEnabled() || !getOps<bool>("showHud")) {
        return;
    }

    if (SDK::getCurrentScreen() != "hud_screen" &&
        SDK::getCurrentScreen() != "f3_screen" &&
        SDK::getCurrentScreen() != "zoom_screen") {
        return;
    }

    const bool showProgress = getOps<bool>("showProgress");
    std::string hudText{};

    if (showProgress) {
        hudText = std::format(
            "PathFinding (BlockESP)\n"
            "State: {}\n"
            "Center/Cursor: ({}, {}) -> ({}, {})\n"
            "Auto Radius: {}\n"
            "Scanned SubChunks/Tick: {}\n"
            "Loaded/Unloaded Hits: {}/{}\n"
            "Found Blocks: {}\n"
            "Token Counts: {}\n"
            "Rendered Blocks: {}",
            statusLine,
            searchCenter.x, searchCenter.z,
            currentChunkPos.x, currentChunkPos.z,
            autoRadius,
            stats.scannedSubChunksThisTick,
            stats.loadedChunkHitsThisTick,
            stats.unloadedChunkHitsThisTick,
            foundBlocks.size(),
            buildTokenSummary(),
            stats.renderedBlocksLastFrame
        );
    } else {
        hudText = std::format(
            "PathFinding\n"
            "State: {}\n"
            "Found: {}\n"
            "Tokens: {}",
            statusLine,
            foundBlocks.size(),
            buildTokenSummary()
        );
    }

    normalRenderCore(40, hudText);
}
