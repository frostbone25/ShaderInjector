# CrossOver build and layout reproduction

The native COM fallback targets CrossOver 26.3's x86-64 D3DMetal image with SHA256:

```text
05a7beaed4494a4f5f53d3f626a82fffc3b70146436a908b7048a0632a49e1a8
```

D3DMetal's native vtables contain Itanium virtual-base metadata and private methods beyond the advertised Windows COM interfaces. Copying only the COM entries corrupts these objects. The compatibility header records the complete observed layouts and validates their relative pointer values before enabling the fallback. Other layouts require separate analysis and runtime validation.

No Apple binary is included or redistributed. The generator reads the user's locally installed image and emits only the structural layout fingerprints used by the source.

## Runtime behavior and validation

With the unmodified injector, CrossOver's native D3D12 methods fail MinHook installation with `MH_ERROR_NOT_EXECUTABLE`: Wine reports their Mach-O mappings as `MEM_FREE`, and Windows `VirtualProtect` cannot modify them. The fallback redirects each supported native object's vptr to a complete copied table, including its virtual-base offsets, RTTI, destructors, and private methods. Normal Windows/PE methods continue through MinHook.

Device creation hooks capture new command queues and command lists. Queues supplied during swap-chain creation are captured too; lists created before startup are discovered when submitted and intercepted on subsequent recordings. Commands already recorded before discovery cannot be changed. Tables and the DLL remain pinned for the process lifetime so native objects cannot outlive their callbacks.

Unrecognized native layouts are rejected before the vptr is changed. A shared downstream function slot cannot represent different native/wrapper implementations, so mixed targets are rejected too; third-party overlays and other D3DMetal versions still need separate validation. The Present guard skips injector rendering when baseline graphics callbacks are unavailable.

Verified on September 6, 2026 with CrossOver `26.3.0.39832`, an Apple M4 Max, Shader Injector `2.2.1` Performance shaders, and FFVII Rebirth `1.0.0.5`:

- A standalone D3D12 test completed 745 rendered/compute frames, including command-list reset, compute pipeline recreation, presentation, and overlay submission, with no device error.
- Rebirth loaded 44 shader packages and applied 9 pipeline replacements across 8 shader targets, with overlay submission and zero injector errors in the observed startup session.

These observations establish startup and shader replacement for the recorded configuration, not long-session stability or support for every CrossOver release. The Windows build workflow checks the MSVC Release x64 project; Windows rendering behavior remains unverified.

Later standalone runs with the PR build stalled at `Hooks->CleanupDummyObjects: destroying window...` before runtime initialization completed. Startup reliability remains an open draft-PR issue; the initial successful Rebirth run does not establish that every subsequent launch succeeds, and a frame-count result without injector initialization is not sufficient validation.

For a runtime check, install the rebuilt DLL with the matching release's shader assets and the game-specific `dsound=native,builtin` override, back up and clear the game's driver shader cache, and inspect `ShaderInjector/Logs/ShaderInjector.log` after launch for native hook installation, overlay submission, and actual shader replacements. Merely loading the DLL or displaying the menu is insufficient.

## Build on macOS

Required tools: CMake 3.20+, Python 3, and x86-64 MinGW GCC, including `windres`. The verified toolchain was Homebrew `mingw-w64` 14.0.0 / GCC 15.2.0. The source base was Shader Injector tag `2.2.1`, commit `690e0e90eb399481fea7515908baf50ad894de2d`; use this checkout with its compatibility changes.

MinHook must be built from source because GNU ld cannot read the bundled MSVC LTCG library. The dependency is tag `v1.3.4`, commit `c3fcafdc10146beb5919319d0683e44e3c30d537`. Other dependencies are bundled in Shader Injector.

From the repository root:

```sh
git clone --depth 1 --branch v1.3.4 https://github.com/TsudaKageyu/minhook.git ../minhook
cmake -S tools/crossover -B build/crossover \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/tools/crossover/mingw-x86_64.cmake" \
  -DCMAKE_BUILD_TYPE=Release \
  -DMINHOOK_SOURCE_DIR="$PWD/../minhook"
cmake --build build/crossover --parallel 8
```

The output is `build/crossover/dsound.dll`. `SHADER_INJECTOR_SOURCE_DIR` defaults to this repository; both source paths can be overridden. Set `MINGW_TOOLCHAIN_PREFIX` to an absolute command prefix if the compiler is outside `PATH`.

The recipe reads the `.vcxproj` source list. It uses C++17, supplies `<cstdint>` needed by upstream headers, accommodates upstream MinHook pointer conversions with `-fpermissive`, and defines `D3D12_SHVER_LIBRARY=6` only when missing from MinGW's SDK. GCC/C++/pthread runtimes are linked statically; Windows/UCRT imports remain. It does not run the upstream release-signing script.

To make a smaller DLL, retain the original for debugging and strip a copy:

```sh
cp build/crossover/dsound.dll build/crossover/dsound-stripped.dll
x86_64-w64-mingw32-strip --strip-unneeded build/crossover/dsound-stripped.dll
x86_64-w64-mingw32-objdump -p build/crossover/dsound-stripped.dll
```

Verify the 12 DirectSound exports against `ShaderInjector/exports.def` and absence of companion `libwinpthread`, `libstdc++`, and `libgcc` DLL imports. Compilation alone does not verify a game: exercise real pipeline creation, command submission, presentation, and shader replacement under the intended backend.

## Reproduce the native fingerprints

The generator requires the exact image hash above and validates the x86-64 Mach-O segment layout. The address points and lengths in `LAYOUTS` were established through live COM interface probes and native constructor/vtable disassembly. Its decoding of `DYLD_CHAINED_PTR_64_OFFSET` rebases is restricted to that image; this is a reproduction tool, not an automatic discovery tool for new releases.

Check the committed header without changing it:

```sh
python3 tools/crossover/generate_native_layouts.py \
  '/Applications/CrossOver.app/Contents/SharedSupport/CrossOver/lib64/apple_gptk/external/D3DMetal.framework/Versions/A/D3DMetal' \
  --check ShaderInjector/NativeD3DMetalLayouts.h
```

Use `--output /path/to/NativeD3DMetalLayouts.h` to write a separate generated copy, or omit both options to print it. The script rejects an unsupported binary before writing output. Rechecking a header does not prove a newly changed runtime path is safe; unknown versions need new boundary analysis and independent rendering/compute validation.
