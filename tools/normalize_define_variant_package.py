"""Rewrite an existing define-variant package into the canonical structure the injector expects.

Two things must agree for a package to be usable:

  * every target's ``defines`` must be sorted by name then value, because ShaderInjector's
    LoadPackageVariants sorts them before building variants;
  * every variant blob must be named ``<compiled blob without extension>.<FNV-1a of the sorted
    "NAME=VALUE" list joined by ';'>.blob``.

Packages generated before the generator canonicalized its output have both in wrapper/layout order,
so the injector would treat all their precompiled blobs as missing. This script sorts the JSON and
renames the blobs, verifying that the mapping is one to one before touching anything.

    python tools/normalize_define_variant_package.py <package-dir> [--dry-run]
"""

import argparse
import json
import os
import re
import sys

VARIANT_BLOB_RE = re.compile(r"^(?P<base>.+)\.(?P<hash>[0-9A-F]{16})\.blob$")


def fnv1a64(data):
    value = 14695981039346656037
    for byte in data:
        value = ((value ^ byte) * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return value


def format_hash(value):
    return "%016X" % value


def define_key(define):
    return "%s=%s" % (define["name"], define["value"])


def variant_hash(defines):
    return format_hash(fnv1a64(";".join(defines).encode("utf-8")))


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("package_dir", help="package folder containing the package JSON and variant blobs")
    parser.add_argument("--dry-run", action="store_true", help="report the changes without writing anything")
    args = parser.parse_args()

    package_dir = os.path.abspath(args.package_dir)
    json_names = [name for name in os.listdir(package_dir) if name.endswith(".json")]
    if len(json_names) != 1:
        sys.exit("expected exactly one package json in %s, found %d" % (package_dir, len(json_names)))
    json_path = os.path.join(package_dir, json_names[0])

    with open(json_path, encoding="utf-8-sig") as handle:
        package = json.load(handle)

    compiled_blob_path = os.path.join(package_dir, package["compiledBlobFile"])
    blob_base, _ = os.path.splitext(compiled_blob_path)

    on_disk = {}
    for name in os.listdir(package_dir):
        found = VARIANT_BLOB_RE.match(name)
        if found:
            on_disk[found.group("hash")] = os.path.join(package_dir, name)

    renames = {}
    unsorted_targets = 0
    missing = []

    for target in package["targets"]:
        raw = target.get("defines") or []
        if not raw:
            continue

        ordered = sorted(raw, key=define_key)
        if ordered != raw:
            unsorted_targets += 1
            target["defines"] = ordered

        old_hash = variant_hash([define_key(item) for item in raw])
        new_hash = variant_hash([define_key(item) for item in ordered])
        if old_hash == new_hash:
            continue
        if old_hash not in on_disk:
            missing.append((target["name"], old_hash))
            continue
        renames[old_hash] = new_hash

    print("package            : %s" % json_names[0])
    print("targets            : %d" % len(package["targets"]))
    print("targets re-sorted  : %d" % unsorted_targets)
    print("variant blobs found: %d" % len(on_disk))
    print("blobs to rename    : %d" % len(renames))

    if missing:
        print("")
        print("targets whose blob is missing on disk (cannot be renamed):")
        for name, blob_hash in missing[:10]:
            print("  %s -> %s" % (name, blob_hash))
        sys.exit("refusing to modify the package while %d targets have no blob" % len(missing))

    if len(set(renames.values())) != len(renames):
        sys.exit("two targets map to the same variant blob; refusing to modify the package")

    if args.dry_run:
        print("")
        print("dry run: no changes written")
        return

    # Rename through a temporary suffix so a source name can never collide with a destination that
    # has not been moved yet.
    staged = []
    for old_hash, new_hash in renames.items():
        source = on_disk[old_hash]
        staged_path = "%s.renaming" % source
        os.replace(source, staged_path)
        staged.append((staged_path, os.path.join(package_dir, "%s.%s.blob" % (blob_base, new_hash))))

    for staged_path, destination in staged:
        os.replace(staged_path, destination)

    with open(json_path, "w", encoding="utf-8") as handle:
        json.dump(package, handle, indent=4)

    print("")
    print("rewrote %s and renamed %d variant blobs" % (json_names[0], len(renames)))


if __name__ == "__main__":
    main()
