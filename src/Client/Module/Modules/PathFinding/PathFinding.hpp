#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "../Module.hpp"
#include "Assets/Assets.hpp"
#include "Events/Game/TickEvent.hpp"
#include "Events/Render/Render3DEvent.hpp"
#include "Events/Render/RenderEvent.hpp"
#include "SDK/Client/Level/ChunkTypes.hpp"

class Block;
class BlockSource;

class PathFindingModule : public Module {
private:
    static constexpr int kMaxTrackedTokens = 4;

    struct SubChunkCoord {
        int chunkX = 0;
        int chunkZ = 0;
        int absoluteSubChunkY = 0;

        bool operator==(const SubChunkCoord& other) const {
            return chunkX == other.chunkX &&
                   chunkZ == other.chunkZ &&
                   absoluteSubChunkY == other.absoluteSubChunkY;
        }
    };

    struct SubChunkCoordHash {
        size_t operator()(const SubChunkCoord& key) const {
            uint64_t value = static_cast<uint32_t>(key.chunkX);
            value = (value << 32ULL) | static_cast<uint32_t>(key.chunkZ);
            value ^= (static_cast<uint64_t>(static_cast<uint32_t>(key.absoluteSubChunkY)) + 0x9E3779B97F4A7C15ULL +
                      (value << 6ULL) + (value >> 2ULL));
            return static_cast<size_t>(value);
        }
    };

    struct FoundBlockEntry {
        BlockPos pos{};
        uint8_t tokenSlot = 0;
    };

    struct ScanStats {
        uint64_t tick = 0;
        int autoRadius = 8;
        int scannedSubChunksThisTick = 0;
        int loadedChunkHitsThisTick = 0;
        int unloadedChunkHitsThisTick = 0;
        int renderedBlocksLastFrame = 0;
        std::array<uint64_t, kMaxTrackedTokens> tokenTotals{};
    };

    int worldMinY = -64;
    int worldMaxYExclusive = 320;
    int worldSubChunkCount = 24;

    bool offsetsLogged = false;
    bool offsetsValid = false;
    bool hasSearchCenter = false;

    ChunkPos searchCenter{};
    ChunkPos playerChunkPos{};
    ChunkPos currentChunkPos{};
    int currentSubChunkIndex = 0;
    int directionIndex = 0;
    int steps = 1;
    int stepsProgress = 0;

    int autoRadius = 8;
    int pendingRadius = -1;
    int pendingRadiusHits = 0;
    int lastProbeTick = 0;
    int lastLogTick = 0;
    int scanIntervalCounter = 0;
    int lastMatcherDiagTick = 0;

    std::string lastTokenCsv{};
    std::array<std::string, kMaxTrackedTokens> trackedTokens{};
    std::array<std::string, kMaxTrackedTokens> trackedCanonicalTokens{};
    int trackedTokenCount = 0;

    std::unordered_map<const Block*, int8_t> blockMatchCache{};
    std::unordered_map<uint64_t, FoundBlockEntry> foundBlocks{};
    std::unordered_map<SubChunkCoord, std::vector<uint64_t>, SubChunkCoordHash> subChunkHits{};
    std::unordered_map<SubChunkCoord, uint8_t, SubChunkCoordHash> emptyScanStreak{};
    std::vector<BlockPos> renderBlocks{};

    ScanStats stats{};
    std::string statusLine = "Waiting for world context...";

    static std::string normalizeToken(std::string value);
    static int chebyshevDistance(const ChunkPos& a, const ChunkPos& b);
    static uint64_t packBlockKey(const BlockPos& pos);
    static D2D_COLOR_F colorForTokenSlot(int tokenSlot);

    void resetRuntime(bool clearCache);
    void resetSpiral(const ChunkPos& centerChunk, bool clearCache);
    bool isEnvironmentReady() const;
    void validateOffsets();
    void updateWorldBounds(BlockSource* blockSource);
    int detectAutoRadius(BlockSource* blockSource, const ChunkPos& centerChunk) const;
    void updateAutoRadius(BlockSource* blockSource);
    void parseTrackedTokens();

    void clearSubChunkHitCache(const SubChunkCoord& key);
    int8_t classifyBlock(const Block* block);
    bool scanCurrentSubChunk(BlockSource* blockSource);
    void moveToNextSpiralSubChunk();
    void rebuildRenderCache();
    std::string buildTokenSummary() const;
    void maybeLogProgress();

public:
    PathFindingModule()
        : Module(
              "PathFinding",
              "BlockESP-style direct subchunk scanner (getElement).",
              IDR_SETTINGS_PNG,
              "",
              false,
              {"blockesp", "pathfinding", "oreesp"}) {}

    void onEnable() override;
    void onDisable() override;
    void defaultConfig() override;
    void settingsRender(float settingsOffset) override;

    void onTick(TickEvent& event);
    void onRender3D(Render3DEvent& event);
    void onRender(RenderEvent& event);
};
