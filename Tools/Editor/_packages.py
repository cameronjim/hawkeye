"""Hard-reference closure of .uasset packages in another project's Content folder, for copying.

import_gasp.py reads the import table of UE 5.8 packages; the Paragon packs on Fab were last saved
by UE 4.19 to 4.27 and use the older summary layout (no SoftObjectPaths block, 28-byte imports up to
VER_UE4_NON_OUTER_PACKAGE_IMPORT). This module reads both, so a copy takes only what a package really
loads instead of every /Game/ string in it. Pure Python; runs without the engine:

    python Tools\\Editor\\_packages.py <gasp root>\\Content/Game/ParagonSparrow/Characters/Heroes/Sparrow/Animations/RMB_Fire
"""

import collections
import os
import shutil
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import import_gasp  # noqa: E402

_PACKAGE_TAG = 0x9E2A83C1
_PKG_FILTER_EDITOR_ONLY = 0x80000000
# EUnrealEngineObjectUE4Version values the summary and import layout depend on.
_VER_UE4_SERIALIZE_TEXT_IN_PACKAGES = 458
_VER_UE4_ADDED_PACKAGE_SUMMARY_LOCALIZATION_ID = 515
_VER_UE4_NON_OUTER_PACKAGE_IMPORT = 519


def _ue4_hard_imports(data):
    """Package names in the import table of a UE4 (legacy -6 / -7) package, or None."""
    if struct.unpack_from("<I", data, 0)[0] != _PACKAGE_TAG:
        return None
    legacy = struct.unpack_from("<i", data, 4)[0]
    if legacy not in (-6, -7):
        return None
    offset = 8 + 4                       # LegacyUE3Version
    ue4 = struct.unpack_from("<i", data, offset)[0]
    offset += 4 + 4                      # FileVersionUE4, FileVersionLicenseeUE4
    custom_versions = struct.unpack_from("<i", data, offset)[0]
    offset += 4 + custom_versions * 20
    offset += 4                          # TotalHeaderSize
    _folder, offset = import_gasp._fstring(data, offset)
    flags, name_count, name_offset = struct.unpack_from("<Iii", data, offset)
    offset += 12
    if ue4 >= _VER_UE4_ADDED_PACKAGE_SUMMARY_LOCALIZATION_ID and not flags & _PKG_FILTER_EDITOR_ONLY:
        _localization, offset = import_gasp._fstring(data, offset)
    if ue4 >= _VER_UE4_SERIALIZE_TEXT_IN_PACKAGES:
        offset += 8                      # GatherableTextDataCount, GatherableTextDataOffset
    _ec, _eo, import_count, import_offset = struct.unpack_from("<4i", data, offset)
    stride = 7 * 4 + (8 if ue4 >= _VER_UE4_NON_OUTER_PACKAGE_IMPORT and not flags & _PKG_FILTER_EDITOR_ONLY else 0)
    if not (0 < name_count < 1 << 20 and 0 < name_offset < len(data)):
        return None
    if not (0 <= import_count < 1 << 20 and 0 < import_offset and import_offset + stride * import_count <= len(data)):
        return None
    names = []
    cursor = name_offset
    for _ in range(name_count):
        text, cursor = import_gasp._fstring(data, cursor)
        cursor += 4                      # hashes (VER_UE4_NAME_HASHES_SERIALIZED)
        names.append(text)
    hard = set()
    for index in range(import_count):
        fields = struct.unpack_from("<7i", data, import_offset + stride * index)
        class_name, outer, object_name, object_number = fields[2], fields[4], fields[5], fields[6]
        if not (0 <= class_name < name_count and 0 <= object_name < name_count):
            return None
        if outer == 0 and names[class_name] == "Package":
            package = names[object_name]
            if object_number:
                package += "_{0}".format(object_number - 1)
            hard.add(package)
    return hard


def hard_references(path):
    """(set of /Game/ packages this file hard-references, parsed_ok), for UE 5.8 and UE4 packages."""
    with open(path, "rb") as handle:
        data = handle.read()
    hard = None
    for reader in (import_gasp._hard_imports, _ue4_hard_imports):
        try:
            hard = reader(data)
        except (struct.error, UnicodeDecodeError, IndexError):
            hard = None
        if hard is not None:
            break
    if hard is None:
        return set(m.decode("latin-1") for m in import_gasp._GAME_PATH.findall(data)), False
    return set(p for p in hard if p.startswith("/Game/")), True


def plan(source_content, roots):
    """The hard closure of roots under source_content: {"files", "missing", "unparsed", "bytes"}."""
    files = collections.OrderedDict()
    missing = set()
    unparsed = []
    queue = collections.deque(roots)
    while queue:
        package = queue.popleft()
        if package in files or package in missing:
            continue
        path = import_gasp.package_file(source_content, package)
        if path is None or path.endswith(".umap"):
            missing.add(package)
            continue
        files[package] = path
        refs, parsed = hard_references(path)
        if not parsed:
            unparsed.append(package)
        for ref in sorted(refs):
            if ref not in files:
                queue.append(ref)
    return {
        "files": files,
        "missing": sorted(missing),
        "unparsed": unparsed,
        "bytes": sum(os.path.getsize(p) for p in files.values()),
    }


def copy(result, destination_content):
    """Copies every planned file not already at the same path. Returns (copied, present) package lists."""
    copied, present = [], []
    for package, source_path in result["files"].items():
        ext = os.path.splitext(source_path)[1]
        target = os.path.join(destination_content, package[len("/Game/"):].replace("/", os.sep) + ext)
        if os.path.exists(target):
            present.append(package)
            continue
        os.makedirs(os.path.dirname(target), exist_ok=True)
        shutil.copy2(source_path, target)
        copied.append(package)
    return copied, present


if __name__ == "__main__":
    RESULT = plan(sys.argv[1], sys.argv[2:])
    print("{0} packages, {1:.1f} MB, {2} unparsed, missing: {3}".format(
        len(RESULT["files"]), RESULT["bytes"] / 1e6, len(RESULT["unparsed"]), RESULT["missing"][:10]))
    for NAME in list(RESULT["files"])[:40]:
        print("  " + NAME)
