# Cross-Version Shader Discovery Tests

Offline test program for ShaderInjector's cross-patch shader recognition. The mod swaps shaders at runtime and must recognize "the same logical shader" when compiled GPU bytecode (DXIL) changes between game patches. These tests exercise that logic using **real ModifiedShader packages** from FF7 Rebirth 1.004 and 1.005.

For background on why matching is hard, see [`CrossVersionDiscovery.md`](../../CrossVersionDiscovery.md). Each case prints a human-readable report: which fingerprint fields match, the similarity score, and whether discovery would accept or reject the match.

## Fixture data

Real captures live under `data/case01-real-captures/`:

```
data/case01-real-captures/
  local-light/
    v1.004/   package.json, modified-shader.hlsl, modified-shader.blob
    v1.005/   ...
  directional-light/
    v1.004/   ...
    v1.005/   ...
```

Each folder is a complete `ShaderInjector.ModifiedShader` package copied from a live install (`ShaderInjector/ModifiedShaders/<PackageName>/`). Tests read `targets[0]` in `package.json` for the original game shader fingerprint (hash, byte length, analysis). The `.hlsl` and `.blob` files are the mod replacement; tests don't need them for matching math.

To add more fixtures: capture packages on each game version via the in-game "Create Modified Shader template" flow, then copy the folder into `data/case01-real-captures/<shader-name>/v<version>/` as `package.json`, `modified-shader.hlsl`, and `modified-shader.blob`.

## Test cases

**Case 1 — cross-patch fingerprint match.** Scores a 1.004 runtime shader against the paired 1.005 ModifiedShader package for Local Light and Directional Light. Both passes accept at ~0.946 similarity with matching `crossVersionIdentityHash`, confirming the one-candidate-vs-one-package path works for these shaders.

**Case 2 — ambiguity rejection.** When two enabled packages score within `0.02` of each other, discovery rejects both rather than guess. Sub-scenarios cover a clear winner among different shaders, a same-patch package beating a cross-patch one, and a constructed near-tie (in-memory clone) that triggers rejection despite both clearing the 0.90 minimum.

**Case 3 — replacement fuzzy fallback.** `DiscoverEnabledReplacement` now falls back to weighted similarity when `crossVersionIdentityHash` drifts but portable reflection still aligns. Uses constructed identity drift on real 1.005 data to show fuzzy accept, a cross-patch near-miss below 0.90, reflection gating, and fuzzy ambiguity rejection.

## Build and run

```bash
cd Tests/CrossVersionDiscovery
mkdir build && cd build
cmake ..
cmake --build .
./cross_version_discovery_tests
```

Or via CTest: `ctest --output-on-failure`

Filter by case:

```bash
./cross_version_discovery_tests -tc="case01*"
./cross_version_discovery_tests -tc="case02*"
./cross_version_discovery_tests -tc="case03*"
```

## What code this reuses

The build compiles real production sources where possible (`Hash.cpp`, `ShaderAnalysis.cpp`). Discovery decision math that depends on Windows-only analyzer/IO code lives in mirrored test files (`DiscoveryLogicMirror.*`, `ModifiedShaderMatching.*`) kept in sync with `ShaderDiscovery.cpp` and `ModifiedShader.cpp`. Package JSON deserializes through production `ModifiedShader::PackageDisk` / `ShaderAnalysis::ShaderAnalysisDisk` types via `LoadPackageTargetCapture` in `FixtureLoader.h`.
