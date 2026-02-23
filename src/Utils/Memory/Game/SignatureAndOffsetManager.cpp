#include "SignatureAndOffsetManager.hpp"

#include <algorithm>
#include <Utils/Concurrency/TaskRuntime.hpp>
#include <Utils/Memory/Memory.hpp>
#include <future>
#include <thread>
#include <utility>
#include <vector>

SignatureAndOffsetManager Mgr;

void SignatureAndOffsetManager::addSignature(unsigned int hash, const char* sig, const char* name) {
    auto it = sigIndices.find(hash);
    if (it != sigIndices.end()) {
        auto& existing = sigs[it->second];
        existing.signature = sig;
        existing.name = name;
        existing.address = 0;
        return;
    }

    sigIndices[hash] = sigs.size();
    sigs.push_back({ hash, sig, name, 0 });
}

void SignatureAndOffsetManager::removeSignature(unsigned int hash) {
    auto it = sigIndices.find(hash);
    if (it == sigIndices.end()) {
        return;
    }

    const auto index = it->second;
    const auto lastIndex = sigs.size() - 1;

    if (index != lastIndex) {
        sigs[index] = std::move(sigs[lastIndex]);
        sigIndices[sigs[index].hash] = index;
    }

    sigs.pop_back();
    sigIndices.erase(it);
}

const char* SignatureAndOffsetManager::getSig(unsigned int hash) const {
    auto it = sigIndices.find(hash);
    return it != sigIndices.end() ? sigs[it->second].signature.c_str() : nullptr;
}

const char* SignatureAndOffsetManager::getSigName(unsigned int hash) const {
    auto it = sigIndices.find(hash);
    return it != sigIndices.end() ? sigs[it->second].name.c_str() : nullptr;
}

uintptr_t SignatureAndOffsetManager::getSigAddress(unsigned int hash) const {
    auto it = sigIndices.find(hash);
    return it != sigIndices.end() ? sigs[it->second].address : 0;
}

void SignatureAndOffsetManager::addOffset(unsigned int hash, int offset) {
    offsets[hash] = offset;
}

int SignatureAndOffsetManager::getOffset(unsigned int hash) const {
    auto it = offsets.find(hash);
    return it != offsets.end() ? it->second : 0; // Default to 0 if not found
}

void SignatureAndOffsetManager::clear() {
    sigs.clear();
    sigIndices.clear();
    offsets.clear();
}

void SignatureAndOffsetManager::scanAllSignatures() {
    if (sigs.empty()) {
        return;
    }

    const std::size_t total = sigs.size();
    const std::size_t workerCount = std::max<std::size_t>(1, std::thread::hardware_concurrency());
    const std::size_t chunkSize = std::max<std::size_t>(1, (total + workerCount - 1) / workerCount);

    std::vector<std::future<void>> futures;
    futures.reserve((total + chunkSize - 1) / chunkSize);

    for (std::size_t begin = 0; begin < total; begin += chunkSize) {
        const std::size_t end = std::min(total, begin + chunkSize);
        futures.emplace_back(TaskRuntime::submit([begin, end, this]() {
            for (std::size_t index = begin; index < end; ++index) {
                auto& signature = sigs[index];
                signature.address = Memory::findSig(signature.signature, signature.name);
            }
        }));
    }

    for (auto& future : futures) {
        if (future.valid()) {
            future.wait();
        }
    }
}
