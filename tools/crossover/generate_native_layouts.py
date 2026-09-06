#!/usr/bin/env python3
"""Reproduce the supported D3DMetal layout fingerprints from a local binary."""

import argparse
import hashlib
from pathlib import Path
import struct
import sys

EXPECTED_SHA256 = "05a7beaed4494a4f5f53d3f626a82fffc3b70146436a908b7048a0632a49e1a8"
# Family, advertised COM length, native prefix length, full native length,
# address point in the supported image. These boundaries were established from
# live QueryInterface probes and native constructor/vtable disassembly; they
# must be re-established, not copied blindly, for another D3DMetal release.
LAYOUTS = (
    ("Device", 83, 7, 87, 0x369BE8),
    ("CommandList", 86, 6, 88, 0x3814E0),
    ("Queue", 19, 6, 22, 0x36EE60),
    ("PipelineLibrary", 14, 6, 16, 0x3691F0),
)


def load_image(path):
    data = path.read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    if digest != EXPECTED_SHA256:
        raise ValueError(f"Unsupported D3DMetal SHA256: {digest}; expected {EXPECTED_SHA256}")
    if len(data) < 32 or struct.unpack_from("<II", data) != (0xFEEDFACF, 0x01000007):
        raise ValueError("Expected a little-endian x86-64 Mach-O image")
    command_count, command_bytes = struct.unpack_from("<II", data, 16)
    command_end = 32 + command_bytes
    if command_end > len(data):
        raise ValueError("Truncated Mach-O load commands")
    segments = []
    offset = 32
    for _ in range(command_count):
        if offset + 8 > command_end:
            raise ValueError("Truncated Mach-O load command")
        command, size = struct.unpack_from("<II", data, offset)
        if size < 8 or offset + size > command_end:
            raise ValueError("Invalid Mach-O load command size")
        if command == 0x19:  # LC_SEGMENT_64
            if size < 72:
                raise ValueError("Truncated LC_SEGMENT_64")
            vm_address, _, file_offset, file_size = struct.unpack_from("<QQQQ", data, offset + 24)
            if file_offset + file_size > len(data):
                raise ValueError("Segment exceeds the image")
            segments.append((vm_address, file_offset, file_size))
        offset += size
    return data, segments


def image_word(data, segments, address):
    for vm_address, file_offset, file_size in segments:
        if vm_address <= address and address + 8 <= vm_address + file_size:
            return struct.unpack_from("<Q", data, file_offset + address - vm_address)[0]
    raise ValueError(f"Vtable address 0x{address:x} is not backed by image bytes")


def fingerprint(data, segments, prefix, entries, point):
    values = []
    for index in range(-prefix, entries):
        value = image_word(data, segments, point + index * 8)
        if index >= -1:
            # The supported image uses DYLD_CHAINED_PTR_64_OFFSET rebases:
            # bind=0, high8=0, target VM offset in the low 36 bits.
            if value >> 63 or (value >> 36) & 0x7FFF:
                raise ValueError(f"Unexpected chained pointer at 0x{point + index * 8:x}")
            value = (value & ((1 << 36) - 1)) - point
        elif value >> 63:
            value -= 1 << 64  # Signed Itanium virtual-base displacement.
        values.append(value)
    return values


def generate(data, segments):
    lines = [
        "#pragma once", "#include <cstddef>", "#include <cstdint>", "",
        "// Compatibility fingerprints for the verified CrossOver 26.3 D3DMetal build.",
        "// Native Itanium virtual-base metadata and private methods extend the COM ABI.",
        "// SHA256: " + EXPECTED_SHA256,
        "// Values are relative to the vtable address point, so ASLR does not affect them.",
        "// Prefix virtual-base displacements (before the RTTI pointer) remain literal.",
        "// These describe primary D3D12 vtables only; other layouts are rejected.",
        "namespace NativeVTableHooks {",
        "enum class Family { Device, CommandList, Queue, PipelineLibrary };",
        "struct NativeLayout { size_t advertised; size_t prefix; size_t entries; const intptr_t* words; };",
    ]
    for name, advertised, prefix, entries, point in LAYOUTS:
        values = fingerprint(data, segments, prefix, entries, point)
        lines.append(f"inline constexpr intptr_t {name}NativeWords[] = {{")
        for offset in range(0, len(values), 8):
            lines.append("    " + ", ".join(map(str, values[offset:offset + 8])) + ",")
        lines.extend([
            "};",
            f"inline constexpr NativeLayout {name}NativeLayout = "
            f"{{{advertised}, {prefix}, {entries}, {name}NativeWords}};",
        ])
    for name, *_ in LAYOUTS:
        lines.extend([
            f"static_assert(sizeof({name}NativeWords) / sizeof(*{name}NativeWords) ==",
            f'    {name}NativeLayout.prefix + {name}NativeLayout.entries, "Incomplete {name} native ABI fingerprint");',
        ])
    lines.extend(["inline const NativeLayout& LayoutFor(Family family) {", "    switch (family) {"])
    for name, *_ in LAYOUTS:
        lines.append(f"        case Family::{name}: return {name}NativeLayout;")
    lines.extend(["    }", "    return DeviceNativeLayout;", "}", "}"])
    return "\n".join(lines) + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path, help="Locally installed D3DMetal Mach-O binary")
    action = parser.add_mutually_exclusive_group()
    action.add_argument("--output", type=Path, help="Write generated header (default: stdout)")
    action.add_argument("--check", type=Path, help="Compare with an existing header without changing it")
    args = parser.parse_args()
    try:
        generated = generate(*load_image(args.binary))
        if args.check:
            if args.check.read_text() != generated:
                raise ValueError(f"Generated fingerprints differ from {args.check}")
            print(f"All four native layouts match {args.check}")
        elif args.output:
            args.output.write_text(generated)
        else:
            sys.stdout.write(generated)
    except (OSError, ValueError, struct.error) as error:
        parser.exit(1, f"error: {error}\n")


if __name__ == "__main__":
    main()
