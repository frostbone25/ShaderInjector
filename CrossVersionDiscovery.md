# Cross-Version Shader Auto-Discovery Improvements

Working notes for making automatic shader discovery and replacement reliable across FF7 Rebirth patches (for example 1.004 vs 1.005), GPU/driver differences, and ordinary shader recompilation drift.

**Context / next steps (not done in this doc):** capture the same logical shaders from multiple game versions, build a small offline test suite that explains *why* auto-discovery would fail today, then harden the fingerprint / matching code until those tests pass.

---

## 1. Problem in plain English

The mod needs to find “the local light pixel shader” (and similar targets) even when the game’s compiled bytecode is not byte-identical.

Today:

- **Raw bytecode hash** works on one machine / one compile, then breaks when the game patches or the driver/compiler output changes.
- A second ID, **`crossVersionIdentityHash`**, tries to recognize the same logical shader across recompiles.
- That second ID is currently **all-or-nothing**. Close-but-not-exact compiles miss.

Result: discovery can work well on the patch used to author packages (recently 1.005) and fail for the patch most players still run (1.004).

There is **no hardcoded 1.004 / 1.005 logic** in the injector. Survival across patches depends entirely on fingerprints and scoring.

---

## 2. Current architecture (what exists today)

Two related pipelines:

| Pipeline | Job | Entry |
|----------|-----|--------|
| Automatic discovery | Match a captured runtime shader to a `ModifiedShader` package, then create a `ShaderTarget` | `ShaderAutomaticDiscovery` |
| Runtime replacement | Apply an existing `ShaderTarget` when PSOs appear | `HookD3D12::FindEnabledShaderTarget` |

### Identification stack

1. **FNV-1a of raw DXIL bytes** (`Hash::HashMemory`) — primary fast ID.
2. **`crossVersionIdentityHash`** — reflection + normalized instruction set + entry name.
3. **Weighted similarity score** (≥ 0.90, ambiguity margin 0.02) for ModifiedShader matching.
4. **`shaderBytecodeHashAliases`** — persist newly seen hashes after a successful cross-version match.
5. **Cached PSO blob** exact / fuzzy match for uncaptured pipelines.

### How `crossVersionIdentityHash` is built

Key file: `ShaderInjector/ShaderAnalyzer.cpp`

Rough recipe:

1. Parse DXIL with DXC reflection.
2. Fingerprint interface (inputs/outputs), resources, constant buffers, execution properties → `portableReflectionIdentityHash`.
3. Disassemble LLVM IR text; strip SSA ids / some metadata; collect **unique** instruction strings; hash → `semanticInstructionSetHash`.
4. Combine:

```text
crossVersionIdentityHash =
  hash(portableReflectionIdentityHash + ":" + entryFunctionName + ":" + semanticInstructionSetHash)
```

### Matching behavior that matters

| Path | Match rule | Notes |
|------|------------|--------|
| Direct hash | Exact known bytecode hash | Fast; dies on patch |
| `DiscoverEnabledReplacement` | Exact `crossVersionIdentityHash` | No “almost” |
| `DiscoverEnabledModifiedShader` (analysis) | Similarity ≥ 0.90 and unique winner | Soft, but still strict |
| Length pre-filter (replacement) | ±5% bytecode length | Can drop candidates before analysis |
| Length pre-filter (auto-discovery queue) | ±15% of known target lengths | Same risk at larger scale |

Key files:

- `ShaderInjector/ShaderAnalyzer.cpp` — analysis / fingerprints
- `ShaderInjector/ShaderAnalysis.cpp` — similarity weights
- `ShaderInjector/ShaderDiscovery.cpp` — replacement + ModifiedShader match logic
- `ShaderInjector/ShaderAutomaticDiscovery.cpp` — queue, length gates, async analysis
- `ShaderInjector/HookD3D12ReplacementLookup.cpp` — runtime lookup order
- `ShaderInjector/ModifiedShader.h` / `.cpp` — package targets metadata

---

## 3. Findings: why this is insufficient across patches

### 3.1 Exact identity is a light switch

`DiscoverEnabledReplacement` only accepts **exact** equality of `crossVersionIdentityHash`.

If a patch changes even one unique normalized instruction form, the entire hash flips. There is no graded “98% the same shader” on that path.

Analogy: identifying a car by a VIN built from every bolt serial. One bolt changes → different VIN → miss, even though it is still the same car model.

### 3.2 Instruction fingerprint is brittle

`semanticInstructionSetHash` currently:

- Works on **text disassembly**, not a structured DXIL opcode stream.
- Normalizes SSA (`%12` → `%v`) and some metadata.
- Does **not** normalize float/int literals, many type decorations, or global/name churn.
- Uses a **set** of unique lines (presence), not a multiset (counts).
- Hashes the whole sorted set into one value.

Compiler drift that preserves “this is still the deferred local light pass” can still change that set enough to break identity.

### 3.3 Soft similarity still punishes unstable fields

Auto-discovery similarity (`ShaderAnalysisDisk::CalculateSimilarityScore`) still weights things that move on ordinary recompiles, including:

- DXIL container part sizes / content hashes
- Instruction statistics (counts from reflection)
- Exact equality of semantic / cross-version hashes (binary score contribution)

A true match can land under **0.90**, or two candidates can land within **0.02** and both get rejected as ambiguous.

### 3.4 Length gates can skip analysis entirely

Comments in `ShaderDiscovery.cpp` say validated old/new pairs differed by &lt;1%, so ±5% felt safe. Larger 1.004 ↔ 1.005 drift would never reach identity comparison.

Auto-discovery uses a wider ±15% enqueue gate, but the same class of failure applies if length moves more than expected.

### 3.5 Packages are effectively version-anchored

`ModifiedShader` packages store:

- `knownShaderBytecodeHashes`
- `originalShaderBytecodeLength`
- full `shaderAnalysis` per `targets[]`

If those were authored against 1.005 only:

- 1.004 misses direct hash
- Must rely on exact identity or soft similarity
- Both can fail for the reasons above

`gameVersion` exists on targets but is metadata only (tiny similarity weight). It does not route matching.

### 3.6 Official DXIL HASH parts will not save us

DXIL container `HASH` / validator hashes are integrity digests of the binary (or binary+source). They change whenever the binary changes. They are **not** a stable logical-shader fingerprint across patches.

What *is* relatively stable in DXIL is the **interface**: signatures, bindings, CB layouts. The hard problem is a stable **body** fingerprint under compile drift.

---

## 4. Likely failure modes for 1.004 vs 1.005

Ordered by likelihood (to confirm with paired dumps + a test suite):

1. **Direct hash miss** (expected on any patch).
2. **`crossVersionIdentityHash` exact miss** due to instruction-set / entry-name drift.
3. **Similarity score below 0.90** because unstable fields dominate the average.
4. **Ambiguous near-ties** between two packages / targets → reject.
5. **Length pre-filter drop** before analysis runs.
6. Less likely but possible: analysis failure (DXC load/disassemble), so no identity at all.

The offline test suite should classify failures into these buckets with evidence, not guesses.

---

## 5. Recommended improvements

### Tier A — pragmatic / high ROI (do early)

1. **Multi-version targets in each ModifiedShader package**  
   Capture the same logical shader on 1.004 and 1.005. Store both hashes + both analyses in `targets[]`. Discovery already takes the best score across targets. This alone may unblock most players without algorithm work.

2. **Near-miss logging**  
   When analysis matching fails, log best score, second-best score, length deltas, and which identity fields differed (`portableReflectionIdentityHash`, `semanticInstructionSetHash`, entry name, etc.). Turns player reports into actionable diffs.

3. **Offline pairwise compare tool / test harness**  
   Input: two bytecode dumps of the “same” shader from different patches.  
   Output: field-by-field fingerprint diff + whether current discovery gates would accept/reject, and why.  
   This is the intended next step before changing matching code.

### Tier B — matching architecture (core fix)

Replace “one exact hash or fail” with **tiered matching**:

| Tier | Signal | Role |
|------|--------|------|
| 0 | Raw bytecode hash | Instant accept |
| 1 | `portableReflectionIdentityHash` (I/O + resources + CBs + stage/model) | Hard filter / strong candidate set |
| 2 | Fuzzy body similarity (Jaccard / MinHash over normalized ops) | Soft accept among filtered set |
| 3 | Full weighted score with downweighted unstable fields | Tie-break |
| 4 | PSO context (paired VS, root signature, RTV formats, etc.) | Disambiguation |

Accept when tier-1 matches (or is extremely close), tier-2 is high, the winner is unique, and length/PSO context are not contradictory.

Also:

- Keep exact `crossVersionIdentityHash` as a fast positive path.
- Add a **fuzzy fallback** for replacement discovery (today it is exact-only).
- Persist hash aliases when fuzzy match succeeds (same as today for exact).

### Tier C — better generic fingerprint

Improve the body fingerprint without depending on raw bytes:

1. **Literal-stripped instruction shapes**  
   Replace constants with placeholders (`$c`), coarsen types, keep opcode / dx.op identity.

2. **Opcode multiset / histogram**  
   Counts of instruction families, not only unique text lines. Survives reorder / mild opt churn better than a single set-hash.

3. **MinHash / LSH over shapes**  
   Approximate similarity under compile drift; cheap to store beside exact hashes.

4. **Prefer structured DXIL / dxil-op parsing over regexing DXC text** long-term  
   Same idea as today’s semantic analysis, fewer text-format footguns.

5. **Keep reflection identity as the hard gate**  
   Fuzzy body matching only among shaders that share the same (or near-same) portable reflection identity. That is the main false-positive defense.

### Tier D — scoring / gate tuning

1. For **cross-version** scoring, downweight or exclude:
   - container part sizes / content hashes
   - raw instruction statistics
   - exact equality bonuses that double-count identity hashes
2. Revisit length gates:
   - either widen after measuring real 1.004/1.005 deltas
   - or gate on reflection first, then length
3. Consider separate thresholds:
   - discovery (creating targets) can be slightly stricter
   - replacement aliasing can be slightly looser once a package is trusted

### Tier E — packaging / release process

1. Author packages with **at least two game-version targets** when a patch is live for both populations.
2. Optionally fill `gameVersion` meaningfully for diagnostics (not as a hard match requirement).
3. Document: “hashes are version-local; analysis targets are how cross-version survival works.”

---

## 6. What not to chase

- Hoping Microsoft’s DXIL `HASH` / validator digest is a stable logical ID — it is not.
- Relying only on bytecode length — useful as a pre-filter, useless as identity.
- Hardcoding 1.004 / 1.005 tables as the long-term strategy — fine as temporary known-hash lists, bad as the only design.
- Making soft matching too loose without a reflection hard gate — false positives would replace wrong shaders.

---

## 7. Suggested offline test suite (for upcoming work)

Goal: given paired dumps, **explain** current failure, then lock improvements with regressions.

Minimal cases:

1. Same shader, same patch, same machine → raw hash match.
2. Same shader, different patch (1.004 vs 1.005) → should eventually match via identity / fuzzy path.
3. Different shaders with similar size → must **not** match.
4. Same shader, deliberately mutated instruction text / literals → measures fingerprint brittleness.
5. Length-delta cases around the 5% / 15% boundaries → proves gate behavior.

For each case, assert and print:

- raw hash equal?
- portable reflection equal?
- semantic instruction set equal?
- crossVersionIdentity equal?
- similarity score + which sub-scores hurt
- would `DiscoverEnabledModifiedShader` accept?
- would `DiscoverEnabledReplacement` accept?
- would length gates enqueue / analyze?

Artifacts needed from capture:

- `OriginalShaderBytecode.bin` (or equivalent) per version
- optional DXC disassembly text
- packaged `targets[].shaderAnalysis` JSON for comparison

---

## 8. Priority order (recommended)

1. Capture paired 1.004 / 1.005 bytecodes for Local Light + Directional Light.
2. Build the offline “why would discovery fail?” report/tests.
3. Ship multi-version targets if that immediately helps players.
4. Add near-miss logging in runtime discovery.
5. Implement tiered matching + literal-stripped / multiset body fingerprint.
6. Retune similarity weights and length gates from measured diffs.
7. Only then consider deeper DXIL bitcode parsing if text-normalized fingerprints still fail.

---

## 9. Bottom line

The injector already has the right high-level idea: **don’t trust raw hashes; fingerprint semantics.**

The weak link is treating a lightly-normalized unique-instruction-set hash as an exact identity, while soft scoring still listens too much to compile-unstable noise.

Fix path:

> multi-version evidence + offline failure classification → tiered matching with a stable reflection hard gate and a fuzzy body fingerprint → thresholds tuned from real 1.004/1.005 pairs.

That is how auto-discovery stays accurate across patches without inventing a mythical forever-stable DXIL vendor ID.
