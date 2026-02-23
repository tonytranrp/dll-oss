#include "SignatureAndOffsetManager.hpp"

#include <algorithm>
#include <Utils/Concurrency/TaskRuntime.hpp>
#include <Utils/Memory/Memory.hpp>
#include <future>
#include <thread>
#include <utility>
#include <vector>

SignatureAndOffsetManager Mgr;

SignatureAndOffsetManager::SignatureAndOffsetManager() {
    sigs.reserve(2048);
    sigIndices.reserve(2048);
    offsets.reserve(1024);
}

void SignatureAndOffsetManager::addSignature(unsigned int hash, const char* sig, const char* name) {
    std::lock_guard<std::mutex> lock(sigMutex);
    auto it = sigIndices.find(hash);
    if (it != sigIndices.end()) {
        auto& existing = sigs[it->second];
        existing.signature = sig;
        existing.name = name;
        existing.address = 0;
        existing.resolved = false;
        return;
    }

    sigIndices[hash] = sigs.size();
    sigs.push_back({ hash, sig, name, 0, false });
}

void SignatureAndOffsetManager::removeSignature(unsigned int hash) {
    std::lock_guard<std::mutex> lock(sigMutex);
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
    std::lock_guard<std::mutex> lock(sigMutex);
    auto it = sigIndices.find(hash);
    return it != sigIndices.end() ? sigs[it->second].signature.c_str() : nullptr;
}

const char* SignatureAndOffsetManager::getSigName(unsigned int hash) const {
    std::lock_guard<std::mutex> lock(sigMutex);
    auto it = sigIndices.find(hash);
    return it != sigIndices.end() ? sigs[it->second].name.c_str() : nullptr;
}

uintptr_t SignatureAndOffsetManager::getSigAddress(unsigned int hash) {
    std::string signaturePattern;
    std::string signatureName;

    {
        std::lock_guard<std::mutex> lock(sigMutex);
        auto it = sigIndices.find(hash);
        if (it == sigIndices.end()) {
            return 0;
        }

        auto& signature = sigs[it->second];
        if (signature.resolved) {
            return signature.address;
        }

        signaturePattern = signature.signature;
        signatureName = signature.name;
    }

    const uintptr_t resolvedAddress = Memory::findSig(signaturePattern, signatureName);

    {
        std::lock_guard<std::mutex> lock(sigMutex);
        auto it = sigIndices.find(hash);
        if (it == sigIndices.end()) {
            return resolvedAddress;
        }

        auto& signature = sigs[it->second];
        if (!signature.resolved) {
            signature.address = resolvedAddress;
            signature.resolved = true;
        }
        return signature.address;
    }
}

void SignatureAndOffsetManager::addOffset(unsigned int hash, int offset) {
    std::lock_guard<std::mutex> lock(offsetMutex);
    offsets[hash] = offset;
}

int SignatureAndOffsetManager::getOffset(unsigned int hash) const {
    std::lock_guard<std::mutex> lock(offsetMutex);
    auto it = offsets.find(hash);
    return it != offsets.end() ? it->second : 0; // Default to 0 if not found
}

void SignatureAndOffsetManager::clear() {
    {
        std::lock_guard<std::mutex> lock(sigMutex);
        sigs.clear();
        sigIndices.clear();
    }
    {
        std::lock_guard<std::mutex> lock(offsetMutex);
        offsets.clear();
    }
}

void SignatureAndOffsetManager::scanAllSignatures() {
    std::vector<unsigned int> hashes;
    {
        std::lock_guard<std::mutex> lock(sigMutex);
        if (sigs.empty()) {
            return;
        }

        hashes.reserve(sigs.size());
        for (const auto& signature : sigs) {
            hashes.emplace_back(signature.hash);
        }
    }

    if (hashes.empty()) {
        return;
    }

    const std::size_t total = hashes.size();
    const std::size_t workerCount = std::max<std::size_t>(1, std::thread::hardware_concurrency());
    const std::size_t chunkSize = std::max<std::size_t>(1, (total + workerCount - 1) / workerCount);

    std::vector<std::future<void>> futures;
    futures.reserve((total + chunkSize - 1) / chunkSize);

    for (std::size_t begin = 0; begin < total; begin += chunkSize) {
        const std::size_t end = std::min(total, begin + chunkSize);
        futures.emplace_back(TaskRuntime::submit([begin, end, &hashes, this]() {
            for (std::size_t index = begin; index < end; ++index) {
                (void)getSigAddress(hashes[index]);
            }
        }));
    }

    for (auto& future : futures) {
        if (future.valid()) {
            future.wait();
        }
    }
}
