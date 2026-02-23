#include "Memory.hpp"

#include <Utils/Logger/Logger.hpp>
#include <libhat/Scanner.hpp>
#include <minhook/MinHook.h>

void Memory::hookFunc(void *pTarget, void *pDetour, void **ppOriginal, std::string name) {
    if (pTarget == nullptr) {
        Logger::custom(fg(fmt::color::crimson), "vFunc Hook", "{} has invalid address", name);
        return;
    }

    if (MH_CreateHook(pTarget, pDetour, ppOriginal) != MH_OK) {
        Logger::custom(fg(fmt::color::crimson), "vFunc Hook", "Failed to hook {}", name);
        return;
    }

    const MH_STATUS status = queueHookEnable ? MH_QueueEnableHook(pTarget) : MH_EnableHook(pTarget);
    if (status != MH_OK) {
        Logger::custom(fg(fmt::color::crimson), "vFunc Hook", "Failed to enable {}", name);
    }
}

uintptr_t Memory::findSig(const std::string_view signature) {
    auto parsed = hat::parse_signature(signature);
    if (!parsed.has_value()) {
        Logger::custom(fg(fmt::color::crimson), "Signatures", "Failed to parse signature: {} ", signature);
        return 0u;
    }

    auto result = hat::find_pattern(parsed.value(), ".text");
    if (!result.has_result()) {
        Logger::custom(fg(fmt::color::crimson), "Signatures", "Failed to find signature: {} ", signature);
        return 0u;
    }

    return reinterpret_cast<uintptr_t>(result.get());
}

uintptr_t Memory::findSig(const std::string_view signature, const std::string_view name) {
    auto parsed = hat::parse_signature(signature);
    if (!parsed.has_value()) {
        Logger::custom(fg(fmt::color::crimson), "Signatures", "Failed to parse signature: {} ", name);
        return 0u;
    }

    auto result = hat::find_pattern(parsed.value(), ".text");
    if (!result.has_result()) {
        Logger::custom(fg(fmt::color::crimson), "Signatures", "Failed to find signature: {} ", name);
        return 0u;
    }

    return reinterpret_cast<uintptr_t>(result.get());
}

uintptr_t Memory::findDMAAddy(uintptr_t ptr, const std::vector<unsigned int>& offsets) {
    uintptr_t addr = ptr;
    for (const unsigned int offset : offsets) {
        addr = *reinterpret_cast<uintptr_t *>(addr);
        addr += offset;
    }
    return addr;
}

void Memory::nopBytes(void *dst, const unsigned int size) {
    if (dst == nullptr) {
        return;
    }

    DWORD oldProtect{};
    VirtualProtect(dst, size, PAGE_EXECUTE_READWRITE, &oldProtect);
    std::memset(dst, 0x90, size);
    VirtualProtect(dst, size, oldProtect, &oldProtect);
}

void Memory::copyBytes(void *src, void *dst, const unsigned int size) {
    if (src == nullptr || dst == nullptr) {
        return;
    }

    DWORD oldProtect{};
    VirtualProtect(src, size, PAGE_EXECUTE_READWRITE, &oldProtect);
    std::memcpy(dst, src, size);
    VirtualProtect(src, size, oldProtect, &oldProtect);
}

void Memory::patchBytes(void *dst, const void *src, const unsigned int size) {
    if (src == nullptr || dst == nullptr) {
        return;
    }

    DWORD oldProtect{};
    VirtualProtect(dst, size, PAGE_EXECUTE_READWRITE, &oldProtect);
    std::memcpy(dst, src, size);
    VirtualProtect(dst, size, oldProtect, &oldProtect);
}

uintptr_t Memory::offsetFromSig(const uintptr_t sig, const int offset) {
    if (sig == 0) {
        return 0;
    }

    return sig + offset + 4 + *reinterpret_cast<int *>(sig + offset);
}

std::array<std::byte, 4> Memory::getRipRel(const uintptr_t instructionAddress, const uintptr_t targetAddress) {
    const uintptr_t relAddress = targetAddress - (instructionAddress + 4);
    std::array<std::byte, 4> relRipBytes{};

    for (size_t i = 0; i < 4; ++i) {
        relRipBytes[i] = static_cast<std::byte>((relAddress >> (i * 8)) & 0xFF);
    }

    return relRipBytes;
}

uintptr_t Memory::GetAddressByIndex(const uintptr_t vtable, const int index) {
    return *reinterpret_cast<uintptr_t *>(vtable + 8 * index);
}

void Memory::SetProtection(const uintptr_t addr, const size_t size, const DWORD protect) {
    DWORD oldProtect{};
    VirtualProtect(reinterpret_cast<LPVOID>(addr), size, protect, &oldProtect);
}

std::vector<std::byte> Memory::readFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return {};
    }

    file.seekg(0, std::ios::end);
    const std::streampos fileSize = file.tellg();
    file.seekg(0, std::ios::beg);

    if (fileSize <= 0) {
        return {};
    }

    const auto size = static_cast<std::streamsize>(fileSize);
    std::vector<std::byte> buffer(static_cast<std::size_t>(size));
    file.read(reinterpret_cast<char *>(buffer.data()), size);
    return buffer;
}
