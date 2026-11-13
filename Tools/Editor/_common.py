"""Shared helpers for the Hawkeye headless content-creation scripts.

Every script that creates starter content imports this module. The rules all of them
follow:

  * check ``unreal.EditorAssetLibrary.does_asset_exist`` before creating anything
  * only save what was actually created or changed
  * print exactly one line per asset: ``created`` / ``exists`` / ``updated`` / ``FAILED``

Run from ``UnrealEditor-Cmd.exe Hawkeye.uproject -run=pythonscript -script=...``.
"""

import os
import traceback

import unreal

# --------------------------------------------------------------------------------------
# logging
# --------------------------------------------------------------------------------------

_SUMMARY = []


def log(action, path, extra=""):
    """One line per asset. ``action`` is created/exists/updated/skipped/FAILED."""
    line = "[Hawkeye] {0:<8} {1}".format(action, path)
    if extra:
        line += "  ({0})".format(extra)
    # unreal.log goes to LogPython, which is what Tools/create-content.ps1 greps for.
    unreal.log(line)
    _SUMMARY.append((action, path, extra))
    return line


def log_error(context, exc):
    line = "[Hawkeye] FAILED   {0}  ({1}: {2})".format(context, type(exc).__name__, exc)
    unreal.log_error(line)
    unreal.log_error(traceback.format_exc())
    _SUMMARY.append(("FAILED", context, str(exc)))
    return line


def summary():
    """List of (action, path, extra) tuples recorded so far."""
    return list(_SUMMARY)


def print_summary(title):
    counts = {}
    for action, _path, _extra in _SUMMARY:
        counts[action] = counts.get(action, 0) + 1
    parts = ", ".join("{0}={1}".format(k, counts[k]) for k in sorted(counts))
    unreal.log("[Hawkeye] ==== {0}: {1} ====".format(title, parts or "nothing to do"))
    return counts


def reset_summary():
    del _SUMMARY[:]


# --------------------------------------------------------------------------------------
# paths / directories
# --------------------------------------------------------------------------------------


def script_dir():
    """Directory holding these scripts, so sibling modules can be imported."""
    try:
        return os.path.dirname(os.path.abspath(__file__))
    except NameError:
        return os.path.join(unreal.Paths.project_dir(), "Tools", "Editor")


def project_dir():
    return os.path.abspath(unreal.Paths.project_dir())


def ensure_directory(package_path):
    """Make sure a /Game/... directory exists. Returns True when it exists afterwards."""
    try:
        if not unreal.EditorAssetLibrary.does_directory_exist(package_path):
            unreal.EditorAssetLibrary.make_directory(package_path)
        return unreal.EditorAssetLibrary.does_directory_exist(package_path)
    except Exception as exc:  # noqa: BLE001 - be loud, never abort the whole script
        log_error("ensure_directory " + package_path, exc)
        return False


def asset_path(package_path, name):
    return "{0}/{1}".format(package_path.rstrip("/"), name)


def object_path(package_path, name):
    """/Game/Foo/Bar.Bar - what load_asset / load_class want."""
    return "{0}/{1}.{1}".format(package_path.rstrip("/"), name)


def generated_class_path(package_path, name):
    """/Game/Foo/BP_X.BP_X_C - the Blueprint's generated class."""
    return "{0}/{1}.{1}_C".format(package_path.rstrip("/"), name)


# --------------------------------------------------------------------------------------
# asset creation / loading / saving
# --------------------------------------------------------------------------------------


def asset_tools():
    return unreal.AssetToolsHelpers.get_asset_tools()


def exists(full_path):
    try:
        return unreal.EditorAssetLibrary.does_asset_exist(full_path)
    except Exception as exc:  # noqa: BLE001
        log_error("does_asset_exist " + full_path, exc)
        return False


def load_or_none(full_path):
    """Load /Game/Foo/Bar, or return None when it does not exist."""
    try:
        if unreal.EditorAssetLibrary.does_asset_exist(full_path):
            return unreal.EditorAssetLibrary.load_asset(full_path)
    except Exception as exc:  # noqa: BLE001
        log_error("load_asset " + full_path, exc)
    return None


def save(asset_or_path, only_if_dirty=False):
    """Save one asset. Accepts a /Game/... path or a loaded UObject."""
    try:
        if isinstance(asset_or_path, str):
            path = asset_or_path
        else:
            path = asset_or_path.get_path_name().split(".")[0]
        return unreal.EditorAssetLibrary.save_asset(path, only_if_is_dirty=only_if_dirty)
    except Exception as exc:  # noqa: BLE001
        log_error("save " + str(asset_or_path), exc)
        return False


def create_asset(name, path, asset_class, factory, quiet=False):
    """Idempotent asset creation.

    Returns ``(asset, created)``. ``created`` is False when the asset already existed
    (or when creation failed, in which case ``asset`` is None).
    """
    full = asset_path(path, name)
    try:
        existing = load_or_none(full)
        if existing is not None:
            if not quiet:
                log("exists", full)
            return existing, False

        ensure_directory(path)
        asset = asset_tools().create_asset(name, path, asset_class, factory)
        if asset is None:
            log("FAILED", full, "create_asset returned None")
            return None, False
        if not quiet:
            log("created", full)
        return asset, True
    except Exception as exc:  # noqa: BLE001
        log_error("create_asset " + full, exc)
        return None, False


def new_factory(*class_names):
    """First factory class that exists in this build, instantiated. None if none do."""
    for cls_name in class_names:
        cls = getattr(unreal, cls_name, None)
        if cls is not None:
            try:
                return cls()
            except Exception as exc:  # noqa: BLE001
                log_error("instantiate factory " + cls_name, exc)
    return None


def find_class(*paths_or_names):
    """Resolve a UClass by unreal.<Name> or by /Script/... path. None when missing."""
    for entry in paths_or_names:
        if "/" in entry:
            try:
                cls = unreal.load_class(None, entry)
                if cls is not None:
                    return cls
            except Exception:  # noqa: BLE001 - a missing class is expected here
                pass
        else:
            cls = getattr(unreal, entry, None)
            if cls is not None:
                return cls
    return None


def class_name(cls):
    """Readable name for a UClass. ``cls.get_name()`` is unbound on Python type objects."""
    if cls is None:
        return "None"
    name = getattr(cls, "__name__", None)
    if name:
        return name
    try:
        return cls.get_name()
    except Exception:  # noqa: BLE001
        return str(cls)


def safe_name(obj):
    try:
        return obj.get_name()
    except Exception:  # noqa: BLE001 - UClasses and structs have no bound get_name
        return class_name(type(obj))


def set_props(obj, values, context=""):
    """set_editor_property for each name->value. Missing properties are reported, not fatal.

    Returns the list of property names that were actually set.
    """
    applied = []
    for prop, value in values:
        try:
            obj.set_editor_property(prop, value)
            applied.append(prop)
        except Exception as exc:  # noqa: BLE001
            unreal.log_warning(
                "[Hawkeye] skipped   {0}.{1}  ({2}: {3})".format(
                    context or safe_name(obj), prop, type(exc).__name__, exc
                )
            )
    return applied


# --------------------------------------------------------------------------------------
# materials
# --------------------------------------------------------------------------------------


def _find_expression(material, expr_class):
    """First material expression of ``expr_class`` in ``material``'s graph, or None."""
    try:
        expressions = unreal.MaterialEditingLibrary.get_material_expressions(material)
    except Exception:  # noqa: BLE001
        return None
    for expr in expressions:
        if isinstance(expr, expr_class):
            return expr
    return None


def ensure_constant_color_material(name, path, base_color_rgb, roughness):
    """Idempotent opaque material: a Constant3Vector base colour + a Constant roughness.

    ``base_color_rgb`` is (r, g, b) in 0..1. Returns the material, or None on failure.

    When the material already exists, its Constant3Vector base-colour node is compared
    against ``base_color_rgb`` (roughness is left alone - only the colour is tuning knob
    callers have needed so far). A mismatch updates the constant, recompiles, and saves;
    an exact match is a no-op so re-running this doesn't dirty the asset every time.
    """
    full = asset_path(path, name)
    existing = load_or_none(full)
    if existing is not None:
        color_expr = _find_expression(existing, unreal.MaterialExpressionConstant3Vector)
        if color_expr is None:
            log("exists", full, "no Constant3Vector node to check")
            return existing

        try:
            current = color_expr.get_editor_property("constant")
            target = unreal.LinearColor(
                base_color_rgb[0], base_color_rgb[1], base_color_rgb[2], 1.0
            )
            matches = (
                abs(current.r - target.r) < 1e-4
                and abs(current.g - target.g) < 1e-4
                and abs(current.b - target.b) < 1e-4
            )
            if matches:
                log("exists", full)
                return existing

            color_expr.set_editor_property("constant", target)
            unreal.MaterialEditingLibrary.recompile_material(existing)
            save(existing)
            log("updated", full, "base colour -> {0}".format(base_color_rgb))
            return existing
        except Exception as exc:  # noqa: BLE001
            log_error("ensure_constant_color_material update " + full, exc)
            return existing

    try:
        ensure_directory(path)
        factory = new_factory("MaterialFactoryNew")
        if factory is None:
            log("FAILED", full, "MaterialFactoryNew unavailable")
            return None
        material = asset_tools().create_asset(name, path, unreal.Material, factory)
        if material is None:
            log("FAILED", full, "create_asset returned None")
            return None

        color_expr = unreal.MaterialEditingLibrary.create_material_expression(
            material, unreal.MaterialExpressionConstant3Vector
        )
        color_expr.set_editor_property(
            "constant",
            unreal.LinearColor(base_color_rgb[0], base_color_rgb[1], base_color_rgb[2], 1.0),
        )
        unreal.MaterialEditingLibrary.connect_material_property(
            color_expr, "", unreal.MaterialProperty.MP_BASE_COLOR
        )

        rough_expr = unreal.MaterialEditingLibrary.create_material_expression(
            material, unreal.MaterialExpressionConstant
        )
        rough_expr.set_editor_property("r", roughness)
        unreal.MaterialEditingLibrary.connect_material_property(
            rough_expr, "", unreal.MaterialProperty.MP_ROUGHNESS
        )

        unreal.MaterialEditingLibrary.recompile_material(material)
        save(material)
        log("created", full)
        return material
    except Exception as exc:  # noqa: BLE001
        log_error("ensure_constant_color_material " + full, exc)
        return None


def assign_mesh_material(actor, material, slot=0):
    """set_material(slot, material) on a StaticMeshActor's mesh component."""
    if material is None:
        return False
    try:
        component = actor.get_editor_property("static_mesh_component")
        component.set_material(slot, material)
        return True
    except Exception as exc:  # noqa: BLE001
        log_error("assign_mesh_material " + safe_name(actor), exc)
        return False


# --------------------------------------------------------------------------------------
# lights
# --------------------------------------------------------------------------------------


# --------------------------------------------------------------------------------------
# blueprints
# --------------------------------------------------------------------------------------


def set_first_prop(obj, names, value, context=""):
    """Set the first property name that exists on obj. Returns the name used, or None.

    Used where the C++ is still settling on a name (ObjectiveId vs ObjectiveTag).
    """
    for prop in names:
        try:
            obj.set_editor_property(prop, value)
            return prop
        except Exception:  # noqa: BLE001 - try the next spelling
            continue
    unreal.log_warning(
        "[Hawkeye] skipped   {0}: none of {1} exist".format(
            context or safe_name(obj), ", ".join(names)
        )
    )
    return None


def compile_blueprint(bp):
    lib = getattr(unreal, "BlueprintEditorLibrary", None)
    if lib is None or not hasattr(lib, "compile_blueprint"):
        return False
    try:
        lib.compile_blueprint(bp)
        return True
    except Exception as exc:  # noqa: BLE001
        log_error("compile_blueprint " + bp.get_name(), exc)
        return False


def blueprint_cdo(bp):
    """Class default object of a Blueprint, or None."""
    try:
        gen = bp.generated_class()
        if gen is None:
            return None
        return unreal.get_default_object(gen)
    except Exception as exc:  # noqa: BLE001
        log_error("blueprint_cdo " + bp.get_name(), exc)
        return None


def load_generated_class(package_path, name):
    try:
        return unreal.load_class(None, generated_class_path(package_path, name))
    except Exception as exc:  # noqa: BLE001
        log_error("load_class " + generated_class_path(package_path, name), exc)
        return None


# --------------------------------------------------------------------------------------
# levels / actors
# --------------------------------------------------------------------------------------


def level_editor_subsystem():
    getter = getattr(unreal, "get_editor_subsystem", None)
    if getter is not None and hasattr(unreal, "LevelEditorSubsystem"):
        try:
            return getter(unreal.LevelEditorSubsystem)
        except Exception:  # noqa: BLE001
            pass
    if hasattr(unreal, "LevelEditorSubsystem"):
        try:
            return unreal.LevelEditorSubsystem()
        except Exception as exc:  # noqa: BLE001
            log_error("LevelEditorSubsystem()", exc)
    return None


def editor_actor_subsystem():
    getter = getattr(unreal, "get_editor_subsystem", None)
    if getter is not None and hasattr(unreal, "EditorActorSubsystem"):
        try:
            return getter(unreal.EditorActorSubsystem)
        except Exception:  # noqa: BLE001
            pass
    if hasattr(unreal, "EditorActorSubsystem"):
        try:
            return unreal.EditorActorSubsystem()
        except Exception as exc:  # noqa: BLE001
            log_error("EditorActorSubsystem()", exc)
    return None


def spawn_actor(actor_class, location, rotation=None, label=None):
    rotation = rotation or unreal.Rotator(0.0, 0.0, 0.0)
    loc = location if isinstance(location, unreal.Vector) else unreal.Vector(*location)
    actor = None
    subsystem = editor_actor_subsystem()
    if subsystem is not None:
        actor = subsystem.spawn_actor_from_class(actor_class, loc, rotation)
    elif hasattr(unreal, "EditorLevelLibrary"):
        actor = unreal.EditorLevelLibrary.spawn_actor_from_class(actor_class, loc, rotation)
    if actor is not None and label:
        try:
            actor.set_actor_label(label)
        except Exception:  # noqa: BLE001 - labels are cosmetic
            pass
    return actor


def all_level_actors():
    subsystem = editor_actor_subsystem()
    if subsystem is not None:
        return subsystem.get_all_level_actors()
    if hasattr(unreal, "EditorLevelLibrary"):
        return unreal.EditorLevelLibrary.get_all_level_actors()
    return []


def editor_world():
    getter = getattr(unreal, "get_editor_subsystem", None)
    if getter is not None and hasattr(unreal, "UnrealEditorSubsystem"):
        try:
            return getter(unreal.UnrealEditorSubsystem).get_editor_world()
        except Exception:  # noqa: BLE001
            pass
    if hasattr(unreal, "EditorLevelLibrary"):
        try:
            return unreal.EditorLevelLibrary.get_editor_world()
        except Exception:  # noqa: BLE001
            pass
    return None


def world_settings():
    """The current level's AWorldSettings actor, or None."""
    for actor in all_level_actors():
        if isinstance(actor, unreal.WorldSettings):
            return actor
    world = editor_world()
    if world is not None:
        try:
            found = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.WorldSettings)
            if found:
                return found[0]
        except Exception as exc:  # noqa: BLE001
            log_error("get_all_actors_of_class(WorldSettings)", exc)
    return None
