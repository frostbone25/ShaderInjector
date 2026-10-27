import json
import os
import re
import sys


def fnv1a64(data):
    value = 14695981039346656037
    for byte in data:
        value = ((value ^ byte) * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return value


def fmt(value):
    return "%016X" % value


def variant_hash(defines):
    return fmt(fnv1a64(";".join(defines).encode("utf-8")))


def main():
    package_dir = sys.argv[1]
    json_path = next(os.path.join(package_dir, n) for n in os.listdir(package_dir) if n.endswith(".json"))
    with open(json_path, encoding="utf-8-sig") as handle:
        package = json.load(handle)

    targets = package["targets"]

    # --- analysis population ---
    analyzed = sum(1 for t in targets if (t.get("shaderAnalysis") or {}).get("succeeded"))
    print("targets with succeeded shaderAnalysis : %d / %d" % (analyzed, len(targets)))

    # --- hash reachability ---
    with_hash = sum(1 for t in targets if t.get("knownShaderBytecodeHashes"))
    print("targets with knownShaderBytecodeHashes: %d / %d" % (with_hash, len(targets)))

    # --- hash uniqueness across targets (RebuildVariantIndex warns on dupes) ---
    seen = {}
    dupes = 0
    for t in targets:
        for h in t.get("knownShaderBytecodeHashes") or []:
            key = h.strip().upper()
            if key in seen:
                dupes += 1
                if dupes <= 3:
                    print("  DUP hash %s in targets '%s' and '%s'" % (key, seen[key], t["name"]))
            else:
                seen[key] = t["name"]
    print("duplicate shader hashes across targets   : %d" % dupes)

    # --- value shape: are all define values plain literals? ---
    non_literal = []
    for t in targets:
        for d in t.get("defines") or []:
            if not re.fullmatch(r"-?\d+", str(d["value"])):
                non_literal.append("%s=%s" % (d["name"], d["value"]))
    print("define values that are not plain integers: %d" % len(non_literal))
    for item in sorted(set(non_literal))[:5]:
        print("  %s" % item)

    # --- define name set consistency ---
    name_sets = {frozenset(d["name"] for d in t.get("defines") or []) for t in targets}
    print("distinct define-name sets               : %d" % len(name_sets))

    # --- would sorting rename the blobs? ---
    on_disk = set()
    for name in os.listdir(package_dir):
        found = re.match(r"^.*\.([0-9A-F]{16})\.blob$", name)
        if found:
            on_disk.add(found.group(1))
    renamed = 0
    for t in targets:
        raw = t.get("defines") or []
        if not raw:
            continue
        written = ["%s=%s" % (d["name"], d["value"]) for d in raw]
        ordered = ["%s=%s" % (d["name"], d["value"])
                   for d in sorted(raw, key=lambda d: (d["name"], d["value"]))]
        if written != ordered and variant_hash(ordered) not in on_disk:
            renamed += 1
    print("")
    print("blobs that must be renamed for sorted naming : %d / %d" % (renamed, len(on_disk)))

    # --- stray build artifacts ---
    strays = [n for n in os.listdir(package_dir)
              if n.endswith(".compiling.log") or n.endswith("-candidate") or ".candidate." in n]
    print("stray candidate/log artifacts                : %d" % len(strays))
    for n in strays[:5]:
        print("  %s" % n)

    # --- core blob (package-level fallback path) ---
    print("")
    print("package-level blob present : %s" % os.path.isfile(os.path.join(package_dir, package["compiledBlobFile"])))
    print("source present             : %s" % os.path.isfile(os.path.join(package_dir, package["sourceFile"])))


if __name__ == "__main__":
    main()
