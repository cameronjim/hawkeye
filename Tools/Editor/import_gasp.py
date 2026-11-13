"""Copy Epic's Game Animation Sample (GASP) sandbox character into Content/, at the same paths.

    source   C:\\Users\\camer\\code\\GASP\\Content   (override with HAWKEYE_GASP_CONTENT, or HAWKEYE_GASP_ROOT
             for the sample project; Tools\\create-content.ps1 sets both)
    roots    /Game/Blueprints/SandboxCharacter_CMC       the CharacterMovement sandbox character
             /Game/Blueprints/SandboxCharacter_CMC_ABP   its motion-matching AnimBP
             /Game/Input/IMC_Sandbox                     the mapping context its graph adds

Every package those roots hard-reference (the import table of each .uasset), transitively, is
copied to the same relative path so the references resolve without redirectors. Soft references
(GM_Sandbox's list of retargeted characters, for one) are not followed, which is what keeps
MetaHumans, Paragon and Echo out. A hard reference into EXCLUDED_PREFIXES is reported and
skipped rather than copied.

Never overwrites. The sample's IA_Move, IA_Look, IA_Jump, IA_Sprint, IA_Crouch, IA_Aim,
IA_Interact and IA_Takedown sit at the same /Game/Input paths as ours; ours stay, so the sandbox
graph's input events resolve to our actions. AHawkeyeCharacter drops those Blueprint bindings at
possession (see AHawkeyeCharacter::PawnClientRestart), and this step empties IMC_Sandbox so the
mapping context the graph pushes maps no keys. Copied assets that a later step changes
(SandboxCharacter_CMC after its reparent, IMC_Sandbox once emptied) are never copied over again.

Pure Python up to the copy, so the plan can be read without the engine:

    python Tools\\Editor\\import_gasp.py            # prints the plan, copies nothing
    python Tools\\Editor\\import_gasp.py --copy     # copies

Inside the editor (create_all runs it before create_blueprints) it copies, rescans the new
folders, and neutralises IMC_Sandbox.
"""

import collections
import os
import re
import shutil
import struct
import sys

try:
    import unreal  # noqa: F401 - absent when run with plain Python
except ImportError:
    unreal = None

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

DEFAULT_GASP_ROOT = r"C:\Users\camer\code\GASP"


def gasp_content_dir(environ=None):
    """The sample's Content folder: HAWKEYE_GASP_CONTENT, else HAWKEYE_GASP_ROOT\\Content, else the old default."""
    environ = os.environ if environ is None else environ
    if environ.get("HAWKEYE_GASP_CONTENT"):
        return environ["HAWKEYE_GASP_CONTENT"]
    return os.path.join(environ.get("HAWKEYE_GASP_ROOT") or DEFAULT_GASP_ROOT, "Content")


GASP_CONTENT = gasp_content_dir()

ROOTS = (
    "/Game/Blueprints/SandboxCharacter_CMC",
    "/Game/Blueprints/SandboxCharacter_CMC_ABP",
    "/Game/Input/IMC_Sandbox",
)

# Never copied, even when something hard-references it. Maps are never copied either.
EXCLUDED_PREFIXES = (
    "/Game/MetaHumans/",
    "/Game/Characters/Paragon/",
    "/Game/Characters/Echo/",
    "/Game/IsolatedExamples/",
    "/Game/Movies/",
    "/Game/Widgets/",
)

IMC_SANDBOX = "/Game/Input/IMC_Sandbox"

_PACKAGE_TAG = 0x9E2A83C1
_PKG_FILTER_EDITOR_ONLY = 0x80000000
_GAME_PATH = re.compile(rb"/Game/[A-Za-z0-9_/\-+]+")


def content_root():
    if unreal is not None:
        return os.path.join(os.path.abspath(unreal.Paths.project_dir()), "Content")
    return os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))), "Content")


def package_file(root, package):
    """The .uasset (or .umap) for a /Game/ package under root, or None."""
    if not package.startswith("/Game/"):
        return None
    base = os.path.join(root, package[len("/Game/"):].replace("/", os.sep))
    for ext in (".uasset", ".umap"):
        if os.path.isfile(base + ext):
            return base + ext
    return None


def _fstring(data, offset):
    length = struct.unpack_from("<i", data, offset)[0]
    offset += 4
    if length >= 0:
        return data[offset:offset + max(length - 1, 0)].decode("latin-1"), offset + length
    return data[offset:offset - 2 * length - 2].decode("utf-16-le"), offset - 2 * length


def _hard_imports(data):
    """Package names in the import table of a UE 5.8 package, or None if it does not parse."""
    if struct.unpack_from("<I", data, 0)[0] != _PACKAGE_TAG:
        return None
    legacy = struct.unpack_from("<i", data, 4)[0]
    offset = 8
    if legacy != -4:
        offset += 4                      # LegacyUE3Version
    offset += 4                          # FileVersionUE4
    if legacy <= -8:
        offset += 4                      # FileVersionUE5
    offset += 4                          # FileVersionLicenseeUE4
    if legacy <= -9:
        offset += 20 + 4                 # SavedHash, TotalHeaderSize
    custom_versions = struct.unpack_from("<i", data, offset)[0]
    offset += 4 + custom_versions * 20
    if legacy > -9:
        offset += 4                      # TotalHeaderSize
    _name, offset = _fstring(data, offset)
    flags, name_count, name_offset = struct.unpack_from("<Iii", data, offset)
    offset += 12 + 8                     # SoftObjectPaths count and offset
    if not flags & _PKG_FILTER_EDITOR_ONLY:
        _loc, offset = _fstring(data, offset)
    _gtc, _gto, _ec, _eo, import_count, import_offset = struct.unpack_from("<6i", data, offset)
    if not (0 < name_count < 1 << 20 and 0 < name_offset < len(data)):
        return None
    if not (0 <= import_count < 1 << 20 and 0 < import_offset and import_offset + 40 * import_count <= len(data)):
        return None

    names = []
    cursor = name_offset
    for _ in range(name_count):
        text, cursor = _fstring(data, cursor)
        cursor += 4                      # hashes
        names.append(text)

    hard = set()
    for index in range(import_count):
        fields = struct.unpack_from("<10i", data, import_offset + 40 * index)
        class_name, outer, object_name, object_number = fields[2], fields[4], fields[5], fields[6]
        if not (0 <= class_name < name_count and 0 <= object_name < name_count):
            return None
        if outer == 0 and names[class_name] == "Package":
            # An FName's number is stored apart from its text: Walk_Loop_F_L_20 is the name
            # "Walk_Loop_F_L" with number 21.
            package = names[object_name]
            if object_number:
                package += "_{0}".format(object_number - 1)
            hard.add(package)
    return hard


def hard_references(path):
    """(set of /Game/ packages this file hard-references, parsed_ok)."""
    with open(path, "rb") as handle:
        data = handle.read()
    try:
        hard = _hard_imports(data)
    except (struct.error, UnicodeDecodeError, IndexError):
        hard = None
    if hard is None:
        # Unparsed (a small data asset with an unusual summary): every /Game/ name counts.
        return set(m.decode("latin-1") for m in _GAME_PATH.findall(data)), False
    return set(p for p in hard if p.startswith("/Game/")), True


def plan(source=GASP_CONTENT, roots=ROOTS):
    """Walk the hard-reference closure. Returns a dict of lists and sizes, copies nothing."""
    files = collections.OrderedDict()
    excluded = set()
    missing = set()
    unparsed = []
    queue = collections.deque(roots)
    while queue:
        package = queue.popleft()
        if package in files or package in excluded or package in missing:
            continue
        if any(package.startswith(prefix) for prefix in EXCLUDED_PREFIXES):
            excluded.add(package)
            continue
        path = package_file(source, package)
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
        "excluded": sorted(excluded),
        # Packages the sample itself references but does not ship; harmless, reported only.
        "missing": sorted(missing),
        "unparsed": unparsed,
        "bytes": sum(os.path.getsize(p) for p in files.values()),
    }


def is_redirector(path):
    """True for a package that only redirects to another (the sample ships a few after renames)."""
    with open(path, "rb") as handle:
        return b"ObjectRedirector" in handle.read(65536)


def copy(result, destination):
    """Copies what is not there yet. Returns (copied, identical, kept_ours) package lists.

    The sample's redirectors are copied only alongside a package that may still point through
    them. fixup_redirectors.py resaves those referencers and deletes the
    redirectors, so a later run that copies nothing else must not bring them back, or every
    create-content run would copy them and the next delete them again."""
    copied, identical, kept = [], [], []
    redirectors = []
    for package, source_path in result["files"].items():
        ext = os.path.splitext(source_path)[1]
        target = os.path.join(destination, package[len("/Game/"):].replace("/", os.sep) + ext)
        if os.path.exists(target):
            if os.path.getsize(target) == os.path.getsize(source_path):
                identical.append(package)
            else:
                # Ours at the same path (the eight IA_ collisions), or a copy a later step changed.
                kept.append(package)
            continue
        if is_redirector(source_path):
            redirectors.append((package, source_path, target))
            continue
        os.makedirs(os.path.dirname(target), exist_ok=True)
        shutil.copy2(source_path, target)
        copied.append(package)
    if copied:
        for package, source_path, target in redirectors:
            os.makedirs(os.path.dirname(target), exist_ok=True)
            shutil.copy2(source_path, target)
            copied.append(package)
    return copied, identical, kept


def neutralise_imc_sandbox():
    """Empty IMC_Sandbox: the sandbox graph pushes it on possession, and it must map nothing."""
    import _common as c  # noqa: PLC0415

    imc = c.load_or_none(IMC_SANDBOX)
    if imc is None:
        c.log("FAILED", IMC_SANDBOX, "not found after the copy")
        return False
    try:
        count = len(imc.get_editor_property("default_key_mappings").get_editor_property("mappings"))
        overrides = len(imc.get_editor_property("mapping_profile_overrides") or {})
    except Exception as exc:  # noqa: BLE001
        c.log_error(IMC_SANDBOX + " read mappings", exc)
        return False
    if count == 0 and overrides == 0:
        c.log("exists", IMC_SANDBOX, "already empty")
        return True
    imc.unmap_all()
    if overrides:
        imc.set_editor_property("mapping_profile_overrides", {})
    c.save(imc)
    c.log("updated", IMC_SANDBOX, "emptied {0} mapping(s); Hawkeye input owns every key".format(count))
    return True


def run():
    import _common as c  # noqa: PLC0415

    if not os.path.isdir(GASP_CONTENT):
        c.log("skipped", "GASP import", "no sample at " + GASP_CONTENT)
        return None

    result = plan()
    destination = content_root()
    copied, identical, kept = copy(result, destination)
    megabytes = result["bytes"] / 1e6
    c.log("copied" if copied else "exists", "GASP sandbox character",
          "{0} package(s) copied, {1} already present, {2} kept as ours; closure {3} packages, {4:.0f} MB".format(
              len(copied), len(identical), len(kept), len(result["files"]), megabytes))
    for package in kept:
        c.log("kept", package, "ours (or changed after import); not overwritten")
    for package in result["excluded"]:
        c.log("skipped", package, "hard-referenced but excluded")

    if copied:
        # The editor has not seen these files; the registry has to before anything can load them.
        folders = sorted(set("/".join(p.split("/")[:-1]) for p in copied))
        registry = unreal.AssetRegistryHelpers.get_asset_registry()
        registry.scan_paths_synchronous(folders, True)

    neutralise_imc_sandbox()
    return result


def _main(argv):
    result = plan()
    total = result["bytes"] / 1e6
    folders = collections.Counter()
    for package, path in result["files"].items():
        folders["/".join(package.split("/")[:4])] += os.path.getsize(path)
    print("closure: {0} packages, {1:.1f} MB from {2}".format(len(result["files"]), total, GASP_CONTENT))
    for folder, size in sorted(folders.items(), key=lambda item: -item[1]):
        print("  {0:9.1f} MB  {1}".format(size / 1e6, folder))
    destination = content_root()
    collisions = [p for p in result["files"] if package_file(destination, p)]
    print("already in Content/ (never overwritten): {0}".format(len(collisions)))
    for package in collisions:
        print("  " + package)
    print("excluded: {0}".format(result["excluded"]))
    print("referenced but not in the sample: {0}".format(len(result["missing"])))
    print("unparsed summaries (all names treated as references): {0}".format(result["unparsed"]))
    if "--copy" in argv:
        copied, identical, kept = copy(result, destination)
        print("copied {0}, identical {1}, kept {2}".format(len(copied), len(identical), len(kept)))


if __name__ == "__main__" and unreal is None:
    _main(sys.argv[1:])
