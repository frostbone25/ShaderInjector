#pragma once
#include <cstddef>
#include <cstdint>

// Compatibility fingerprints for the verified CrossOver 26.3 D3DMetal build.
// Native Itanium virtual-base metadata and private methods extend the COM ABI.
// SHA256: 05a7beaed4494a4f5f53d3f626a82fffc3b70146436a908b7048a0632a49e1a8
// Values are relative to the vtable address point, so ASLR does not affect them.
// Prefix virtual-base displacements (before the RTTI pointer) remain literal.
// These describe primary D3D12 vtables only; other layouts are rejected.
namespace NativeVTableHooks {
enum class Family { Device, CommandList, Queue, PipelineLibrary };
struct NativeLayout { size_t advertised; size_t prefix; size_t entries; const intptr_t* words; };
inline constexpr intptr_t DeviceNativeWords[] = {
    2880, 0, 0, 0, 0, 0, 12672, -3393926,
    -3392476, -3392456, -3392268, -3392102, -3391934, -3391768, -3391566, -3391560,
    -3391276, -3391028, -3390494, -3390160, -3389512, -3387224, -3386870, -3386840,
    -3386522, -3386476, -3384880, -3383768, -3383468, -3383170, -3381000, -3380260,
    -3379920, -3379722, -3379616, -3379388, -3379024, -3378796, -3378598, -3378394,
    -3378210, -3378026, -3377656, -3377302, -3377064, -3377060, -3375668, -3375394,
    -3375210, -3374208, -3371924, -3371906, -3371658, -3371154, -3371150, -3370544,
    -3369998, -3369814, -3369598, -3369254, -3369070, -3368842, -3368828, -3368630,
    -3368424, -3368240, -3368064, -3367786, -3367602, -3367418, -3367106, -3366576,
    -3366394, -3366210, -3365998, -3365814, -3365608, -3365296, -3364982, -3364806,
    -3364800, -3364616, -3364432, -3364422, -3364042, -3363690, -3363458, -3363282,
    -3363070, -3362886, -3362392, -3362380, -3362354, -3361820,
};
inline constexpr NativeLayout DeviceNativeLayout = {83, 7, 87, DeviceNativeWords};
inline constexpr intptr_t CommandListNativeWords[] = {
    0, 0, 0, 0, 0, 10368, -2222890, -2221822,
    -2221802, -2221614, -2221450, -2221284, -2221122, -2221068, -2221054, -2221050,
    -2216980, -2216018, -2215716, -2215494, -2215258, -2215070, -2214614, -2214012,
    -2213426, -2211868, -2211272, -2211018, -2210470, -2210042, -2209690, -2209440,
    -2209044, -2208556, -2208142, -2207816, -2207640, -2207464, -2207234, -2207004,
    -2206764, -2206524, -2206248, -2205972, -2205742, -2205512, -2205282, -2205052,
    -2204822, -2204592, -2203880, -2203222, -2202532, -2202304, -2201780, -2201286,
    -2200698, -2200110, -2200108, -2199592, -2199192, -2198610, -2198504, -2198328,
    -2198152, -2197976, -2193260, -2193084, -2192908, -2192518, -2192220, -2191076,
    -2190900, -2190324, -2190148, -2189588, -2189586, -2189410, -2189234, -2183846,
    -2183066, -2182490, -2181916, -2181470, -2181290, -2181110, -2180690, -2180514,
    -2180338, -2180162, -2179986, -2179920, -2179170, -2179158,
};
inline constexpr NativeLayout CommandListNativeLayout = {86, 6, 88, CommandListNativeWords};
inline constexpr intptr_t QueueNativeWords[] = {
    0, 0, 0, 0, 0, 2088, -3312024, -3311700,
    -3311680, -3311492, -3311328, -3311162, -3311000, -3310946, -3310932, -3305884,
    -3304722, -3304462, -3304460, -3304458, -3304456, -3304200, -3303944, -3303928,
    -3303752, -3303738, -3303698, -3303652,
};
inline constexpr NativeLayout QueueNativeLayout = {19, 6, 22, QueueNativeWords};
inline constexpr intptr_t PipelineLibraryNativeWords[] = {
    0, 0, 0, 0, 0, 1736, -3436064, -3435722,
    -3435702, -3435514, -3435350, -3435184, -3435022, -3434968, -3434954, -3433676,
    -3433178, -3432790, -3432448, -3432060, -3431428, -3431392,
};
inline constexpr NativeLayout PipelineLibraryNativeLayout = {14, 6, 16, PipelineLibraryNativeWords};
static_assert(sizeof(DeviceNativeWords) / sizeof(*DeviceNativeWords) ==
    DeviceNativeLayout.prefix + DeviceNativeLayout.entries, "Incomplete Device native ABI fingerprint");
static_assert(sizeof(CommandListNativeWords) / sizeof(*CommandListNativeWords) ==
    CommandListNativeLayout.prefix + CommandListNativeLayout.entries, "Incomplete CommandList native ABI fingerprint");
static_assert(sizeof(QueueNativeWords) / sizeof(*QueueNativeWords) ==
    QueueNativeLayout.prefix + QueueNativeLayout.entries, "Incomplete Queue native ABI fingerprint");
static_assert(sizeof(PipelineLibraryNativeWords) / sizeof(*PipelineLibraryNativeWords) ==
    PipelineLibraryNativeLayout.prefix + PipelineLibraryNativeLayout.entries, "Incomplete PipelineLibrary native ABI fingerprint");
inline const NativeLayout& LayoutFor(Family family) {
    switch (family) {
        case Family::Device: return DeviceNativeLayout;
        case Family::CommandList: return CommandListNativeLayout;
        case Family::Queue: return QueueNativeLayout;
        case Family::PipelineLibrary: return PipelineLibraryNativeLayout;
    }
    return DeviceNativeLayout;
}
}
