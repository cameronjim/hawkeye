"""The combat clip sets and who wears them (claude-docs/animation.md).

    /Game/Blueprints/Animation/DA_AnimSet_Kate     UCombatAnimSet: every role filled from the
    /Game/Blueprints/Animation/DA_AnimSet_Clint    AM_<Role>_<Variant> montages import_combat_anims.py
    /Game/Blueprints/Animation/DA_AnimSet_Thug     made for that character (their HawkeyeCharacters
    /Game/Blueprints/Animation/DA_AnimSet_Archer   tag), a role with none left empty (the fallback)

Then CombatAnimSet on BP_Kate, BP_Clint, BP_Thug (the gunner and the heavy inherit it) and BP_Archer.

Which montages fill a role when there are several: every clip for that role and character in
Tools/Data/Anims/manifest.json whose montage exists, in manifest order, then any others (hand-made ones)
by name. The first goes in the role's slot (variant 0); the rest go in MoreVariants for that role, in
that order, and the melee component cycles through them per swing. A manifest clip with
"in_set": false is imported but left out of the set (the thugs' run-jump attack).
Safe with no clips at all: the sets are made empty and stay that way. Idempotent: a set or a
Blueprint is saved only when a slot actually changes. Runs after create_bow_ik (create_all.py), whose
graph has the clip slots these montages play in.
"""

import json
import os
import sys

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import _common as c  # noqa: E402

SET_PATH = "/Game/Blueprints/Animation"
MANIFEST = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "Data", "Anims", "manifest.json")
CHARACTERS_TAG = "HawkeyeCharacters"

ROLES = ["Light1", "Light2", "Light3", "Heavy", "Kick", "Parry", "DodgeForward", "DodgeBack", "DodgeLeft", "DodgeRight",
         "HitFront", "HitBack", "HitLeft", "HitRight", "Knockdown", "GetUp", "FinisherAttacker", "FinisherVictim",
         "FinisherBow", "BowDraw", "BowAimIdle", "BowFire", "BowNock", "GetUpFront", "HitHeavy"]

# (character, data asset, the folder its montages are in, the Blueprints that wear it)
CHARACTERS = [
    ("kate", "DA_AnimSet_Kate", "/Game/Characters/UEFN_Mannequin/Animations/Combat", [("/Game/Blueprints/Player", "BP_Kate")]),
    ("clint", "DA_AnimSet_Clint", "/Game/Characters/UEFN_Mannequin/Animations/Combat", [("/Game/Blueprints/Player", "BP_Clint")]),
    ("thug", "DA_AnimSet_Thug", "/Game/Mannequin/Animations/Combat", [("/Game/Blueprints/AI", "BP_Thug")]),
    ("archer", "DA_AnimSet_Archer", "/Game/Mannequin/Animations/Combat", [("/Game/Blueprints/Bosses", "BP_Archer")]),
]


def property_name(role):
    """Light1 -> light1, DodgeForward -> dodge_forward, FinisherBow -> finisher_bow (Python's names)."""
    out = ""
    for i, ch in enumerate(role):
        if ch.isupper() and i > 0 and not role[i - 1].isdigit():
            out += "_"
        out += ch.lower()
    return out


def clip_characters(clip):
    wanted = clip.get("characters", "all")
    if wanted == "all":
        return None
    return [wanted] if isinstance(wanted, str) else list(wanted)


def manifest_order():
    """Per role, the montages in manifest order and who each clip is for:
    {role: [(AM_Role_Variant, [characters] or None for all, in_set), ...]}. One montage can come from
    several clip lines (Jab To Elbow is Kate's Light3 and a thug's Light1; they land in different folders)."""
    try:
        with open(MANIFEST, encoding="utf-8") as handle:
            clips = json.load(handle).get("clips", [])
    except (OSError, ValueError):
        return {}
    order = {}
    for clip in clips:
        variant = "".join(ch for ch in clip.get("variant", "Clip") if ch.isalnum()) or "Clip"
        order.setdefault(clip["role"], []).append(
            ("AM_{0}_{1}".format(clip["role"], variant), clip_characters(clip), clip.get("in_set", True)))
    return order


def montages_in(folder):
    """{name: montage} for every AnimMontage directly in folder (not SelfTest/ or Rigs/)."""
    found = {}
    if not unreal.EditorAssetLibrary.does_directory_exist(folder):
        return found
    for path in unreal.EditorAssetLibrary.list_assets(folder, recursive=False, include_folder=False):
        asset_path = path.split(".")[0]
        name = asset_path.rsplit("/", 1)[-1]
        if not name.startswith("AM_"):
            continue
        montage = c.load_or_none(asset_path)
        if isinstance(montage, unreal.AnimMontage):
            found[name] = montage
    return found


def worn_by(montage, character):
    tag = unreal.EditorAssetLibrary.get_metadata_tag(montage, CHARACTERS_TAG) or ""
    return not tag or character in [t.strip() for t in tag.split(",")]


def pick_all(role, character, montages, order):
    """Every montage for role that character wears, in manifest order, then hand-made ones by name."""
    ranked, left_out = [], set()
    for name, characters, in_set in order.get(role, []):
        if characters is not None and character not in characters:
            continue
        if not in_set:
            left_out.add(name)
        elif name in montages and name not in ranked:
            ranked.append(name)
    listed = set(n for n, _, _ in order.get(role, []))
    ranked += sorted(n for n in montages if n.startswith("AM_{0}_".format(role)) and n not in listed)
    return [montages[n] for n in ranked if n not in left_out and worn_by(montages[n], character)]


def variants_of(anim_set):
    """{role name: [montage path, ...]} as the set holds MoreVariants now."""
    current = {}
    try:
        entries = anim_set.get_editor_property("more_variants") or []
    except Exception as exc:  # noqa: BLE001
        c.log_error("{0}.more_variants".format(anim_set.get_name()), exc)
        return None
    for entry in entries:
        role = entry.get_editor_property("role")
        key = getattr(role, "name", None) or str(role).split(".")[-1].split(":")[0].strip(" <>")
        current[key.replace("_", "").upper()] = [soft_path(m) for m in entry.get_editor_property("montages")]
    return current


def role_enum(role):
    """ECombatAnimRole value for a role name (Python spells DodgeForward DODGE_FORWARD)."""
    for attr in dir(unreal.CombatAnimRole):
        if attr.replace("_", "").upper() == role.upper() and attr.isupper():
            return getattr(unreal.CombatAnimRole, attr)
    raise KeyError(role)


def soft_path(value):
    if value is None:
        return ""
    for getter in ("get_path_name", "export_text"):
        if hasattr(value, getter):
            try:
                text = str(getattr(value, getter)())
                return "" if text in ("None", "") else text.split(".")[0]
            except Exception:  # noqa: BLE001
                continue
    return str(value)


def ensure_set(name):
    cls = c.find_class("CombatAnimSet", "/Script/Hawkeye.CombatAnimSet")
    if cls is None:
        c.log("FAILED", c.asset_path(SET_PATH, name), "UCombatAnimSet not found; build the module")
        return None, False
    factory = c.new_factory("DataAssetFactory")
    c.set_props(factory, [("data_asset_class", cls)], "DataAssetFactory")
    asset, created = c.create_asset(name, SET_PATH, cls, factory, quiet=True)
    return asset, created


def fill_set(anim_set, character, folder, order):
    montages = montages_in(folder)
    changed, filled = [], []
    more = []
    for role in ROLES:
        prop = property_name(role)
        picked = pick_all(role, character, montages, order)
        montage = picked[0] if picked else None
        if len(picked) > 1:
            more.append((role, picked[1:]))
        try:
            current = soft_path(anim_set.get_editor_property(prop))
        except Exception as exc:  # noqa: BLE001
            c.log_error("{0}.{1}".format(anim_set.get_name(), prop), exc)
            continue
        wanted = montage.get_path_name().split(".")[0] if montage else ""
        if montage:
            filled.append(role)
        if current == wanted:
            continue
        anim_set.set_editor_property(prop, montage)
        changed.append(role)

    wanted = dict((role.upper(), [m.get_path_name().split(".")[0] for m in extra]) for role, extra in more)
    if variants_of(anim_set) != wanted:
        entries = []
        for role, extra in more:
            entry = unreal.CombatAnimVariants()
            entry.set_editor_property("role", role_enum(role))
            entry.set_editor_property("montages", extra)
            entries.append(entry)
        anim_set.set_editor_property("more_variants", entries)
        changed.append("MoreVariants")
    for role, extra in more:
        filled[filled.index(role)] = "{0} (+{1})".format(role, ", ".join(m.get_name() for m in extra))
    return changed, filled


def assign(folder, bp_name, anim_set):
    full = c.asset_path(folder, bp_name)
    bp = c.load_or_none(full)
    cdo = c.blueprint_cdo(bp) if bp is not None else None
    if cdo is None:
        c.log("skipped", full + ".CombatAnimSet", "no Blueprint")
        return
    current = cdo.get_editor_property("combat_anim_set")
    if current is not None and current.get_path_name() == anim_set.get_path_name():
        c.log("exists", full + ".CombatAnimSet", anim_set.get_name())
        return
    cdo.set_editor_property("combat_anim_set", anim_set)
    c.compile_blueprint(bp)
    c.save(bp)
    c.log("updated", full + ".CombatAnimSet", anim_set.get_name())


def run():
    order = manifest_order()
    c.ensure_directory(SET_PATH)
    sets = {}
    for character, name, folder, users in CHARACTERS:
        anim_set, created = ensure_set(name)
        if anim_set is None:
            continue
        changed, filled = fill_set(anim_set, character, folder, order)
        full = c.asset_path(SET_PATH, name)
        if created or changed:
            c.save(anim_set)
            c.log("created" if created else "updated", full, "{0} role(s) filled{1}{2}".format(
                len(filled), ": " + ", ".join(filled) if filled else " (no clips yet: every role on its fallback)",
                "; changed " + ", ".join(changed) if changed and not created else ""))
        else:
            c.log("exists", full, "{0} role(s) filled".format(len(filled)))
        for bp_folder, bp_name in users:
            assign(bp_folder, bp_name, anim_set)
        sets[character] = anim_set
    return sets


if __name__ == "__main__":
    run()
    c.print_summary("combat anim sets")
