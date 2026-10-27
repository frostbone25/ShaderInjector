#!/usr/bin/env python3
"""Generate a ShaderInjector modified-shader package that replaces many shader permutations
from one shared HLSL source, using per-target preprocessor defines.

Input is a RenoDX-style folder: a shared core shader plus hash-named wrappers
(``0x<CRC32>.<profile>.hlsl``) that only ``#define`` permutation values and ``#include`` the core.
Each wrapper becomes one package target:

    knownShaderBytecodeHashes = [<ShaderInjector FNV-1a hash of the original bytecode>]
    defines                   = [the wrapper's #define lines as "NAME=VALUE"]

The injector compiles the core once per distinct define set and gives every matched game shader
the blob built for its own permutation.

Layout mode (``--permutation-layout`` + ``--source-file``): no wrappers are needed. One target is
generated per hash-map row; its defines come from the row's permutation column decoded through a
JSON layout (bit fields in order, then constants).

CRC32 -> FNV mapping comes from a CSV with ``crc32`` and ``shaderinjector_fnv`` columns.

Variant blobs are optionally precompiled here, with the same DXC arguments and file naming the
injector uses (``<compiled blob without extension>.<FNV-1a of defines joined by ';'>.blob``),
so the first game launch does not compile every permutation.
"""

import argparse
import csv
import json
import os
import re
import shutil
import struct
import subprocess
import sys

WRAPPER_RE = re.compile(r"^0x([0-9A-Fa-f]{8})\.([a-z]{2}_\d_\d)\.hlsl$")
WRAPPER_PROFILE_RE = re.compile(r"\.([a-z]{2}_\d_\d)\.hlsl$")
DEFINE_RE = re.compile(r"^\s*#\s*define\s+([A-Za-z_][A-Za-z0-9_]*)(?:\s+(.*?))?\s*$")
INCLUDE_RE = re.compile(r'^\s*#\s*include\s+"([^"]+)"')

SHADER_TYPES = {"vs": 1, "hs": 2, "ds": 3, "ps": 4, "gs": 5, "cs": 6}


def fnv1a64(data):
    value = 14695981039346656037
    for byte in data:
        value = ((value ^ byte) * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return value


def format_hash(value):
    return "%016X" % value


def parse_wrapper(path):
    """Return (defines, include) for a wrapper that only defines values and includes the core."""
    defines = []
    include = None
    with open(path, encoding="utf-8-sig") as handle:
        for line in handle:
            stripped = line.split("//", 1)[0].strip()
            if not stripped:
                continue
            define = DEFINE_RE.match(stripped)
            if define:
                name, value = define.group(1), define.group(2)
                defines.append("%s=%s" % (name, "1" if value in (None, "") else value))
                continue
            included = INCLUDE_RE.match(stripped)
            if included:
                include = included.group(1)
                continue
            raise ValueError("%s: wrapper contains code other than #define/#include: %s" % (path, stripped))
    if not include:
        raise ValueError("%s: wrapper does not include a core shader" % path)
    return defines, include


def load_hash_map(path):
    mapping = {}
    with open(path, newline="", encoding="utf-8-sig") as handle:
        for row in csv.DictReader(handle):
            crc = row["crc32"].strip().upper().replace("0X", "")
            mapping[crc] = row
    return mapping


def canonical_defines(defines):
    """Sort "NAME=VALUE" defines by name then value.

    ShaderInjector's LoadPackageVariants sorts every target's defines before building variants, so a
    variant blob file name is the FNV-1a of this order. Canonicalizing here keeps the precompiled blob
    names identical to the ones the injector looks for; wrapper or layout order would make every
    precompiled blob a cache miss on first launch.
    """
    return sorted(defines, key=lambda define: define.split("=", 1))


def define_objects(defines):
    """"NAME=VALUE" strings -> the package JSON form {"name": ..., "value": ...}."""
    objects = []
    for define in defines:
        name, value = define.split("=", 1)
        objects.append({"name": name, "value": value})
    return objects


def target_json(target):
    entry = {key: value for key, value in target.items() if not key.startswith("_")}
    entry["defines"] = define_objects(target["defines"])
    return entry


def layout_defines(layout, row):
    value = int(str(row[layout.get("column", "permutation")]).strip(), 0)
    defines = []
    for bit in layout["bits"]:
        field = (value >> bit["shift"]) & ((1 << bit["width"]) - 1)
        defines.append("%s=%d" % (bit["name"], field))
    for name, constant in layout.get("constants", {}).items():
        defines.append("%s=%s" % (name, constant))
    return defines


# Shader Configuration parses "#define NAME VALUE" and commented-out "// #define NAME" lines in every .hlsl under ModifiedShaders.
CONFIG_DEFINE_RE = re.compile(r"^\s*(?://\s*)?#\s*define\s+([A-Za-z_][A-Za-z0-9_]*)(?!\()")


def hide_defines(path, should_hide):
    """Tag the defines selected by ``should_hide(name)`` with ``// [NO CONFIG]``.

    ShaderInjector's Shader Configuration UI lists every define it finds in .hlsl files and rewrites them on Apply.
    """
    with open(path, encoding="utf-8-sig", newline="") as handle:
        lines = handle.read().splitlines(True)
    if not lines:
        return 0
    newline = "\r\n" if lines[0].endswith("\r\n") else "\n"

    tagged = []
    count = 0
    for index, line in enumerate(lines):
        define = CONFIG_DEFINE_RE.match(line)
        previous = lines[index - 1].rstrip() if index else ""
        if (define and should_hide(define.group(1)) and
                "[NO CONFIG]" not in previous.upper() and not previous.endswith("\\")):
            tagged.append(line[:len(line) - len(line.lstrip())] + "// [NO CONFIG]" + newline)
            count += 1
        tagged.append(line)

    if count:
        with open(path, "w", encoding="utf-8", newline="") as handle:
            handle.write("".join(tagged))
    return count


def tag_package_configurations(output_dir, core_path, layout):
    """Prepare the package for the Shader Configuration UI.

    The layout's ``configurations`` become tagged defines at the top of the core. In the core only the permutation
    switches are hidden (they are per-target compiler defines); its other defines keep the default behavior.
    Included shaders are library code, so all of their defines are hidden.
    """
    configuration_names = {config["name"] for config in layout.get("configurations", [])}
    permutation_names = {bit["name"] for bit in layout["bits"]} | set(layout.get("constants", {}))
    hidden = 0
    for directory, _, files in os.walk(output_dir):
        for name in files:
            path = os.path.join(directory, name)
            if not name.lower().endswith(".hlsl"):
                continue
            if os.path.samefile(path, core_path):
                hidden += hide_defines(path, lambda define: define in permutation_names)
            else:
                hidden += hide_defines(path, lambda define: True)

    with open(core_path, encoding="utf-8", newline="") as handle:
        core = handle.read()
    newline = "\r\n" if "\r\n" in core else "\n"

    header = []
    for config in layout.get("configurations", []):
        for description in config.get("description", "").splitlines():
            header.append("// %s" % description)
        header.append("// [CONFIG TYPE]: %s" % config["type"])
        header.append("// [CONFIG DEFAULT]: %s" % config["value"])
        if config.get("range"):
            header.append("// [CONFIG RANGE]: %s" % config["range"])
        header.append("#define %s %s" % (config["name"], config["value"]))
        header.append("")

    with open(core_path, "w", encoding="utf-8", newline="") as handle:
        handle.write("".join(item + newline for item in header) + core)
    print("configurations: %d exposed, %d other defines hidden" % (len(configuration_names), hidden))


def load_rows(path):
    with open(path, newline="", encoding="utf-8-sig") as handle:
        return list(csv.DictReader(handle))


def variant_blob_path(compiled_blob_path, defines):
    joined = ";".join(defines).encode("utf-8")
    base, _ = os.path.splitext(compiled_blob_path)
    return "%s.%s.blob" % (base, format_hash(fnv1a64(joined)))


def output_signature(dxc, blob_path):
    """Semantic/index/register triples of the output signature, from dxc -dumpbin."""
    text = subprocess.run([dxc, "-dumpbin", blob_path], capture_output=True, text=True).stdout
    section = text.split("; Output signature:", 1)
    if len(section) < 2:
        return None
    rows = []
    for line in section[1].splitlines()[3:]:
        parts = line.lstrip("; ").split()
        if len(parts) < 4 or not parts[1].isdigit():
            break
        rows.append((parts[0].upper(), int(parts[1]), parts[3]))
    return rows


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--source-dir", required=True, help="folder with the core shader, its includes and the 0x<CRC32> wrappers")
    parser.add_argument("--source-file", help="core shader file name inside --source-dir (required with --permutation-layout)")
    parser.add_argument("--permutation-layout", help="JSON file mapping the hash-map permutation column to defines; enables hash-map (wrapper-less) mode")
    parser.add_argument("--profile", help="shader profile (layout mode; default: taken from the hash-map file column, e.g. ps_6_6)")
    parser.add_argument("--hash-map", required=True, help="CSV with crc32 and shaderinjector_fnv columns")
    parser.add_argument("--output-dir", required=True, help="package folder to create (normally <game>/ShaderInjector/ModifiedShaders/<name>)")
    parser.add_argument("--name", required=True, help="package name; also used for file names")
    parser.add_argument("--id", help="package id (default: ModifiedShader_<name>)")
    parser.add_argument("--entry", default="main", help="shader entry point")
    parser.add_argument("--extra-include", action="append", default=[], metavar="SRC=DEST",
                        help="copy include tree SRC into <package>/DEST (repeatable)")
    parser.add_argument("--rewrite-include", action="append", default=[], metavar="OLD=NEW",
                        help="rewrite #include \"OLD\" to \"NEW\" in copied sources (repeatable)")
    parser.add_argument("--compiler-argument", action="append", default=[], metavar="ARG",
                        help="extra DXC argument stored in the package and used for precompiling (repeatable), e.g. -enable-16bit-types")
    parser.add_argument("--dxc", help="dxc.exe used to precompile variant blobs (skipped when omitted)")
    parser.add_argument("--originals-dir", help="optional folder of original <crc>.cso / named .cso files to compare output signatures")
    parser.add_argument("--originals-column", default="file", help="hash-map column naming the original .cso (with --originals-dir)")
    parser.add_argument("--force", action="store_true", help="overwrite an existing output folder")
    args = parser.parse_args()

    source_dir = os.path.abspath(args.source_dir)
    output_dir = os.path.abspath(args.output_dir)
    hash_map = load_hash_map(args.hash_map)
    layout_mode = bool(args.permutation_layout)

    wrappers = []
    for name in sorted(os.listdir(source_dir)):
        match = WRAPPER_RE.match(name)
        if match and not layout_mode:
            wrappers.append((match.group(1).upper(), match.group(2), os.path.join(source_dir, name)))
    if not wrappers and not layout_mode:
        sys.exit("no 0x<CRC32>.<profile>.hlsl wrappers found in %s" % source_dir)

    targets = []
    core_include = None
    missing = []
    if layout_mode:
        if not args.source_file:
            sys.exit("--permutation-layout requires --source-file")
        with open(args.permutation_layout, encoding="utf-8-sig") as handle:
            layout = json.load(handle)
        core_include = os.path.basename(args.source_file)
        profiles = set()
        for row in load_rows(args.hash_map):
            crc = row["crc32"].strip().upper().replace("0X", "")
            parts = [row.get("group", "").strip(), row.get("flags", "").strip(), "0x%s" % crc]
            found = WRAPPER_PROFILE_RE.search(row.get("file", ""))
            if found:
                profiles.add(found.group(1))
            defines = canonical_defines(layout_defines(layout, row))
            targets.append({
                "name": "_".join(part for part in parts if part),
                "targetApplication": "",
                "gameVersion": "",
                "knownShaderBytecodeHashes": [row["shaderinjector_fnv"].strip().upper()],
                "originalShaderBytecodeLength": "",
                "defines": defines,
                "_crc": crc,
                "_row": row,
            })
        if args.profile:
            profiles = {args.profile}
        if not targets:
            sys.exit("hash map %s has no rows" % args.hash_map)
    else:
        profiles = {profile for _, profile, _ in wrappers}
    if len(profiles) != 1:
        sys.exit("expected exactly one shader profile, found: %s" % sorted(profiles))
    profile = profiles.pop()

    for crc, _, path in ([] if layout_mode else wrappers):
        defines, include = parse_wrapper(path)
        include_name = os.path.basename(include)
        if core_include is None:
            core_include = include_name
        elif include_name != core_include:
            sys.exit("%s includes %s, expected %s" % (path, include_name, core_include))
        row = hash_map.get(crc)
        if not row:
            missing.append(crc)
            continue
        targets.append({
            "name": "0x%s" % crc,
            "targetApplication": "",
            "gameVersion": "",
            "knownShaderBytecodeHashes": [row["shaderinjector_fnv"].strip().upper()],
            "originalShaderBytecodeLength": "",
            "defines": canonical_defines(defines),
            "_crc": crc,
            "_row": row,
        })
    if missing:
        sys.exit("no hash-map entry for wrappers: %s" % ", ".join("0x" + crc for crc in missing))

    if os.path.exists(output_dir):
        if not args.force:
            sys.exit("%s exists (use --force)" % output_dir)
        shutil.rmtree(output_dir)
    os.makedirs(output_dir)

    # Copy the core and every non-wrapper source file.
    rewrites = [item.split("=", 1) for item in args.rewrite_include]
    for name in os.listdir(source_dir):
        path = os.path.join(source_dir, name)
        if not os.path.isfile(path) or WRAPPER_RE.match(name):
            continue
        if not name.lower().endswith((".hlsl", ".hlsli", ".h", ".fxh", ".usf", ".ush")):
            continue
        with open(path, encoding="utf-8-sig", newline="") as handle:
            text = handle.read()
        for old, new in rewrites:
            text = text.replace('"%s"' % old, '"%s"' % new)
        with open(os.path.join(output_dir, name), "w", encoding="utf-8", newline="") as handle:
            handle.write(text)

    for item in args.extra_include:
        src, dest = item.split("=", 1)
        shutil.copytree(src, os.path.join(output_dir, dest))

    if not os.path.isfile(os.path.join(output_dir, core_include)):
        sys.exit("core shader %s was not found in %s" % (core_include, source_dir))

    if layout_mode:
        tag_package_configurations(output_dir, os.path.join(output_dir, core_include), layout)

    package_id = args.id or "ModifiedShader_%s" % args.name
    compiled_blob_file = "%s_Compiled.blob" % args.name
    package = {
        "enabled": True,
        "id": package_id,
        "name": args.name,
        "shaderType": SHADER_TYPES[profile[:2]],
        "shaderProfile": profile,
        "shaderEntryPoint": args.entry,
        "sourceFile": core_include,
        "compiledBlobFile": compiled_blob_file,
        "compilerArguments": args.compiler_argument,
        "targets": [target_json(target) for target in targets],
    }
    with open(os.path.join(output_dir, "%s.json" % args.name), "w", encoding="utf-8") as handle:
        json.dump(package, handle, indent=4)

    print("package: %s (%d targets, profile %s, source %s)" % (output_dir, len(targets), profile, core_include))

    if not args.dxc:
        return

    compiled_blob_path = os.path.join(output_dir, compiled_blob_file)
    core_path = os.path.join(output_dir, core_include)
    built = {}
    failures = 0
    mismatches = 0
    for target in targets:
        key = tuple(target["defines"])
        if key in built:
            continue
        blob_path = variant_blob_path(compiled_blob_path, target["defines"])
        command = [args.dxc, "-T", profile, "-E", args.entry, "-pack-prefix-stable", "-I", output_dir, core_path, "-Fo", blob_path]
        for define in target["defines"]:
            command += ["-D", define]
        command += args.compiler_argument
        result = subprocess.run(command, capture_output=True, text=True)
        if result.returncode != 0 or not os.path.isfile(blob_path):
            failures += 1
            print("FAILED %s %s\n%s" % (target["name"], " ".join(target["defines"]), (result.stdout + result.stderr)[-2000:]))
            continue
        built[key] = blob_path

        if args.originals_dir:
            original = os.path.join(args.originals_dir, target["_row"][args.originals_column].replace(".hlsl", ".cso"))
            if os.path.isfile(original):
                expected = output_signature(args.dxc, original)
                actual = output_signature(args.dxc, blob_path)
                if expected != actual:
                    mismatches += 1
                    print("SIGNATURE MISMATCH %s: original %s, compiled %s" % (target["name"], expected, actual))

    print("precompiled %d variant blobs, %d failed%s" % (
        len(built), failures, "" if not args.originals_dir else ", %d output-signature mismatches" % mismatches))
    if failures or mismatches:
        sys.exit(1)


if __name__ == "__main__":
    main()
