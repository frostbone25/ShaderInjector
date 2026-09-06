#pragma once

#include <windows.h>
#include <unknwn.h>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>
#include "MinHook.h"
#include "ShaderInjectorIO.h"
#include "NativeD3DMetalLayouts.h"

// The supported D3DMetal build exposes COM objects from native Mach-O mappings
// that Wine reports as MEM_FREE, so MinHook and VirtualProtect cannot hook them.
// Redirect each object's writable vptr to a process-lifetime copy of the verified
// native ABI, including virtual-base metadata and methods beyond the COM API.
namespace NativeVTableHooks
{
    struct InterfaceVersion { const wchar_t* iid; size_t entries; };
    struct ShadowTable
    {
        void** native = nullptr;
        size_t prefix = 0;
        size_t entries = 0;
        std::unique_ptr<void*[]> allocation;
        void** table() const { return allocation.get() + prefix; }
    };
    struct Registry
    {
        std::mutex mutex;
        std::vector<std::unique_ptr<ShadowTable>> tables;
        bool pinned = false;
    };
    inline Registry& State()
    {
        // Native objects can outlive C++ DLL teardown. Neither the table nor its
        // detours may disappear while any object still references it.
        static Registry* state = new Registry;
        return *state;
    }
    inline bool IsWine()
    {
        static const bool wine = GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "wine_get_version") != nullptr;
        return wine;
    }
    inline bool IsNativeAddress(void* address)
    {
        MEMORY_BASIC_INFORMATION info{};
        return IsWine() && VirtualQuery(address, &info, sizeof(info)) != 0 && info.State == MEM_FREE;
    }
    inline ShadowTable* FindTableLocked(void** table)
    {
        for (const auto& entry : State().tables)
            if (entry->native == table || entry->table() == table)
                return entry.get();
        return nullptr;
    }
    inline bool IsNativeObject(IUnknown* object)
    {
        if (!object || !IsWine()) return false;
        void** table = *reinterpret_cast<void***>(object);
        if (IsNativeAddress(table)) return true;
        std::lock_guard<std::mutex> lock(State().mutex);
        return FindTableLocked(table) != nullptr;
    }
    inline size_t AdvertisedLength(IUnknown* object, Family family)
    {
        if (!object) return 0;
        // Counts are the Windows ABI vtable sizes in Microsoft's DirectX headers.
        // Probe newest first, and only count interfaces sharing this exact vptr.
        static const InterfaceVersion device[] = {
            {L"{76cff76f-1e9b-4450-8cdc-34f1af788e5b}",94},
            {L"{5f6e592d-d895-44c2-8e4a-88ad4926d323}",83},
            {L"{14eecffc-4df8-40f7-a118-5c816f45695e}",82},
            {L"{5af5c532-4c91-4cd0-b541-15a405395fc5}",81},
            {L"{5405c344-d457-444e-b4dd-2366e45aee39}",80},
            {L"{517f8718-aa66-49f9-b02b-a7ab89c06031}",79},
            {L"{4c80e962-f032-4f60-bc9e-ebc2cfa1d83c}",76},
            {L"{9218e6bb-f944-4f7e-a75c-b1b2c7b701f3}",73},
            {L"{5c014b53-68a1-4b9b-8bd1-dd6046b9358b}",68},
            {L"{c70b221b-40e4-4a17-89af-025a0727a6dc}",66},
            {L"{8b4f173b-2fea-4b80-8f58-4307191ab95d}",65},
            {L"{e865df17-a9ee-46f9-a463-3098315aa2e5}",57},
            {L"{81dadc15-2bad-4392-93c5-101345c4aa98}",51},
            {L"{30baa41e-b15b-475c-a0bb-1af5c5b64328}",48},
            {L"{77acce80-638e-4e65-8895-c1f23386863e}",47},
            {L"{189819f1-1db6-4b57-be54-1821339b85f7}",44}};
        static const InterfaceVersion commandList[] = {
            {L"{7013c015-d161-4b63-a08c-238552dd8acc}",86},
            {L"{34ed2808-ffe6-4c2b-b11a-cabd2b0c59e1}",84},
            {L"{ee936ef9-599d-4d28-938e-23c4ad05ce51}",82},
            {L"{dd171223-8b61-4769-90e3-160ccde4e2c1}",81},
            {L"{c3827890-e548-4cfa-96cf-5689a9370f80}",80},
            {L"{55050859-4024-474c-87f5-6472eaee44ea}",79},
            {L"{8754318e-d3a9-4541-98cf-645b50dc4874}",77},
            {L"{6fda83a7-b84c-4e38-9ac8-c7bd22016b3d}",68},
            {L"{38c3e585-ff17-412c-9150-4fc6f9d72a28}",67},
            {L"{553103fb-1fe7-4557-bb38-946d7d0e7ca7}",66},
            {L"{5b160d0f-ac1b-4185-8ba8-b3ae42a5a455}",60}};
        static const InterfaceVersion queue[] = {
            {L"{3a3c3165-0ee7-4b8e-a0af-6356b4c3bbb9}",23},
            {L"{0ec870a6-5d7e-4c22-8cfc-5baae07616ed}",19}};
        static const InterfaceVersion pipelineLibrary[] = {
            {L"{80eabf42-2568-4e5e-bd82-c37f86961dc3}",14},
            {L"{c64226a8-9201-46af-b4cc-53fb9ff7414f}",13}};
        const InterfaceVersion* versions = nullptr;
        size_t count = 0;
#define NATIVE_FAMILY_CASE(value, array) case Family::value: versions = array; count = sizeof(array) / sizeof(*array); break
        switch (family) {
            NATIVE_FAMILY_CASE(Device, device);
            NATIVE_FAMILY_CASE(CommandList, commandList);
            NATIVE_FAMILY_CASE(Queue, queue);
            NATIVE_FAMILY_CASE(PipelineLibrary, pipelineLibrary);
        }
#undef NATIVE_FAMILY_CASE
        void** current = *reinterpret_cast<void***>(object);
        for (size_t i = 0; i < count; ++i) {
            IID iid{};
            IUnknown* queried = nullptr;
            if (SUCCEEDED(IIDFromString(versions[i].iid, &iid)) && SUCCEEDED(object->QueryInterface(iid, reinterpret_cast<void**>(&queried))) && queried) {
                const bool sameTable = *reinterpret_cast<void***>(queried) == current;
                queried->Release();
                if (sameTable) return versions[i].entries;
            }
        }
        return 0;
    }
    inline bool MatchesNativeLayout(void** native, Family family, size_t advertisedEntries)
    {
        const NativeLayout& layout = LayoutFor(family);
        if (advertisedEntries != layout.advertised) return false;
        const intptr_t addressPoint = reinterpret_cast<intptr_t>(native);
        // First fingerprint only the COM entries whose existence QueryInterface
        // established. Then validate this known binary's native prefix and tail.
        for (size_t slot = 0; slot < layout.advertised; ++slot)
            if (reinterpret_cast<intptr_t>(native[slot]) - addressPoint != layout.words[layout.prefix + slot])
                return false;
        for (intptr_t slot = -static_cast<intptr_t>(layout.prefix); slot < 0; ++slot) {
            intptr_t value = reinterpret_cast<intptr_t>(native[slot]);
            if (slot == -1) value -= addressPoint; // RTTI is an image pointer.
            if (value != layout.words[layout.prefix + slot]) return false;
        }
        for (size_t slot = layout.advertised; slot < layout.entries; ++slot)
            if (reinterpret_cast<intptr_t>(native[slot]) - addressPoint != layout.words[layout.prefix + slot])
                return false;
        // The next sub-vtable starts with a virtual-base offset, proving the
        // native primary table still ends at the reviewed boundary.
        return reinterpret_cast<intptr_t>(native[layout.entries]) == (family == Family::Device ? 0xb38 : -8);
    }
    inline bool Install(IUnknown* object, Family family, size_t slot, void* detour, void** original, const char* name)
    {
        if (!object || !detour || !original) return false;
        void** current = *reinterpret_cast<void***>(object);
        if (!IsNativeObject(object)) {
            // A shared Original_* cannot simultaneously dispatch a native class
            // and an unrelated PE wrapper. Keep existing native callbacks valid
            // rather than replacing their downstream with a MinHook trampoline.
            if (*original && IsNativeAddress(*original)) {
                ShaderInjectorIO::WriteToLogFileError(std::string("NativeVTableHooks: refusing mixed native and inline originals for ") + name);
                return false;
            }
            MH_STATUS created = MH_CreateHook(current[slot], detour, original);
            if (created != MH_OK && created != MH_ERROR_ALREADY_CREATED) {
                ShaderInjectorIO::WriteToLogFileError(std::string("NativeVTableHooks: ") + name + " MinHook create failed: " + MH_StatusToString(created));
                return false;
            }
            MH_STATUS enabled = MH_EnableHook(current[slot]);
            if (enabled != MH_OK && enabled != MH_ERROR_ENABLED)
                ShaderInjectorIO::WriteToLogFileError(std::string("NativeVTableHooks: ") + name + " MinHook enable failed: " + MH_StatusToString(enabled));
            return enabled == MH_OK || enabled == MH_ERROR_ENABLED;
        }
        {
            std::lock_guard<std::mutex> lock(State().mutex);
            ShadowTable* installed = FindTableLocked(current);
            if (installed && current == installed->table() && slot < installed->entries &&
                installed->table()[slot] == detour && *original == installed->native[slot])
                return true;
        }
        // QueryInterface may execute native code, so determine the length outside
        // the registry lock and re-read the vptr once installation is serialized.
        const size_t advertisedEntries = AdvertisedLength(object, family);
        if (slot >= advertisedEntries) {
            ShaderInjectorIO::WriteToLogFileError(std::string("NativeVTableHooks: unsupported interface size for ") + name + " entries=" + std::to_string(advertisedEntries));
            return false;
        }
        std::lock_guard<std::mutex> lock(State().mutex);
        current = *reinterpret_cast<void***>(object);
        ShadowTable* shadow = FindTableLocked(current);
        void** native = shadow ? shadow->native : current;
        void* downstream = native[slot];
        if (*original && *original != downstream) {
            ShaderInjectorIO::WriteToLogFileError(std::string("NativeVTableHooks: refusing mismatched original for ") + name);
            return false;
        }
        if (!shadow) {
            if (!IsNativeAddress(native)) return false;
            if (!MatchesNativeLayout(native, family, advertisedEntries)) {
                ShaderInjectorIO::WriteToLogFileError(std::string("NativeVTableHooks: unsupported D3DMetal native ABI for ") + name);
                return false;
            }
            const NativeLayout& layout = LayoutFor(family);
            auto created = std::make_unique<ShadowTable>();
            created->native = native;
            created->prefix = layout.prefix;
            created->entries = layout.entries;
            created->allocation = std::make_unique<void*[]>(layout.prefix + layout.entries);
            // Preserve virtual-base offsets, offset-to-top, RTTI, COM methods,
            // native destructors, and internal virtual methods as one ABI unit.
            std::memcpy(created->allocation.get(), native - layout.prefix,
                (layout.prefix + layout.entries) * sizeof(void*));
            shadow = created.get();
            State().tables.push_back(std::move(created));
        }
        if (slot >= shadow->entries || advertisedEntries > shadow->entries) {
            ShaderInjectorIO::WriteToLogFileError(std::string("NativeVTableHooks: incompatible existing shadow size for ") + name);
            return false;
        }
        if (!State().pinned) {
            HMODULE module = nullptr;
            if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                reinterpret_cast<LPCWSTR>(detour), &module)) {
                ShaderInjectorIO::WriteToLogFileError(std::string("NativeVTableHooks: could not retain hook module for ") + name);
                return false;
            }
            State().pinned = true;
        }
        const bool newHook = shadow->table()[slot] != detour;
        if (!*original) *original = downstream;
        InterlockedExchangePointer(reinterpret_cast<PVOID volatile*>(&shadow->table()[slot]), detour);
        InterlockedExchangePointer(reinterpret_cast<PVOID volatile*>(object), shadow->table());
        if (newHook)
            ShaderInjectorIO::WriteToLogFile(std::string("NativeVTableHooks: installed ") + name + " entries=" + std::to_string(shadow->entries));
        return true;
    }
    template<typename Object, typename Function>
    inline bool Install(Object* object, Family family, size_t slot, Function detour, Function* original, const char* name)
    {
        return Install(static_cast<IUnknown*>(object), family, slot, reinterpret_cast<void*>(detour), reinterpret_cast<void**>(original), name);
    }
}
