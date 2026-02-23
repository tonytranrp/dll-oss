#pragma once

#include <array>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>
#include <windows.h>
#include <unknwn.h>
#include <winrt/base.h>

#define in_range(x, a, b) (x >= a && x <= b)
#define get_bits(x) (in_range((x & (~0x20)), 'A', 'F') ? ((x & (~0x20)) - 'A' + 0xa) : (in_range(x, '0', '9') ? x - '0' : 0))
#define get_byte(x) (get_bits(x[0]) << 4 | get_bits(x[1]))

template<typename Ret, typename Type>
Ret &direct_access(Type *type, size_t offset) {
    union {
        size_t raw;
        Type *source;
        Ret *target;
    } u;
    u.source = type;
    u.raw += offset;
    return *u.target;
}

#define AS_FIELD(type, name, fn) __declspec(property(get = fn, put = set##name)) type name
#define DEF_FIELD_RW(type, name) __declspec(property(get = get##name, put = set##name)) type name

//fake class macro to avoid compile errors when using pragma once

#define FK(typep) \
class typep;

#define FAKE_FIELD(type, name)                                                                                       \
AS_FIELD(type, name, get##name);                                                                                     \
type get##name()

#define BUILD_ACCESS(ptr, type, name, offset)                                                                        \
AS_FIELD(type, name, get##name);                                                                                     \
type get##name() const { return direct_access<type>(ptr, offset); }                                                     \
void set##name(type v) const { direct_access<type>(ptr, offset) = v; }

#define BUILD_ACCESS_REF(ptr, type, name, offset)                                                                    \
type& get##name() { return direct_access<type>(ptr, offset); }                                                    \
const type& get##name() const { return direct_access<type>(ptr, offset); }                                        \
void set##name(const type& v) { direct_access<type>(ptr, offset) = v; }

class Memory {
public:
    static inline bool queueHookEnable = false;

    static void setQueueHookEnable(const bool enabled) {
        queueHookEnable = enabled;
    }

    template<unsigned int IIdx, typename TRet, typename... TArgs>
    static auto CallVFunc(void *thisptr, TArgs... argList) -> TRet {
        using Fn = TRet(__thiscall *)(void *, decltype(argList)...);
        return (*static_cast<Fn **>(thisptr))[IIdx](thisptr, std::forward<TArgs>(argList)...);
    }

    template<typename TRet, typename... TArgs>
    static auto CallVFuncI(uint32_t index, void *thisptr, TArgs... argList) -> TRet {
        using Fn = TRet(__thiscall*)(void *, TArgs...);
        return (*static_cast<Fn **>(thisptr))[index](thisptr, std::forward<TArgs>(argList)...);
    }

    static void hookFunc(void *pTarget, void *pDetour, void **ppOriginal, std::string name);

    template<typename R, typename... Args>
    static R CallFunc(void *func, Args... args) {
        return ((R(*)(Args...)) func)(args...);
    }

    template<unsigned int index>
    static void HookVFunc(uintptr_t sigOffset, void *pDetour, void **ppOriginal, std::string name) {
        auto **vTable = reinterpret_cast<uintptr_t **>(sigOffset + 3 + 7);

        hookFunc(vTable[index], pDetour, ppOriginal, std::move(name));
    }

    static uintptr_t findSig(std::string_view signature);
    static uintptr_t findSig(std::string_view signature, std::string_view name);


    template<typename T>
    static void SafeRelease(T *&pPtr) {
        if (pPtr != nullptr) {
            pPtr->Release();
            pPtr = nullptr;
        }
    }


    // Overload for winrt::com_ptr - smart pointer handles Release() automatically
    template<typename T>
    static void SafeRelease(winrt::com_ptr<T> &pPtr) {
        pPtr = nullptr;  // Smart pointer automatically calls Release()
    }

    static uintptr_t findDMAAddy(uintptr_t ptr, const std::vector<unsigned int>& offsets);
    static void nopBytes(void *dst, unsigned int size);
    static void copyBytes(void *src, void *dst, unsigned int size);
    static void patchBytes(void *dst, const void *src, unsigned int size);
    static uintptr_t offsetFromSig(uintptr_t sig, int offset);

    template<typename Ret>
    static Ret getOffsetFromSig(const uintptr_t sig, const int offset) {
        return reinterpret_cast<Ret>(offsetFromSig(sig, offset));
    }

    static std::array<std::byte, 4> getRipRel(uintptr_t instructionAddress, uintptr_t targetAddress);
    static uintptr_t GetAddressByIndex(uintptr_t vtable, int index);
    static void SetProtection(uintptr_t addr, size_t size, DWORD protect);
    static std::vector<std::byte> readFile(const std::filesystem::path& path);
};

class ScopedVirtualProtect {
public:
    ScopedVirtualProtect(void *addr, size_t size, DWORD newProtect,
                         bool instruction = true)
        : addr(addr), size(size), instruction(instruction) {
        restore = VirtualProtect(addr, size, newProtect, &oldProtect);
    }
    ~ScopedVirtualProtect() {
        if (restore) {
            VirtualProtect(addr, size, oldProtect, &oldProtect);
        }
        if (instruction) {
            FlushInstructionCache(GetCurrentProcess(), addr, size);
        }
    }

private:
    void *addr;
    size_t size;
    DWORD oldProtect;
    bool instruction;
    bool restore;
};

#define GLUE1(a, b) a##b
#define GLUE(a, b) GLUE1(a, b)
#define ScopedVP(ptr, ...)                                                     \
ScopedVirtualProtect GLUE(svp, __LINE__)((void *)(ptr), __VA_ARGS__);
