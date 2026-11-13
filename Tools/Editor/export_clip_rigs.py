"""Export the two driver mannequins and their idles to FBX for Tools/Blender/build_clips.py.

The authored clips (get-ups, hit reactions, the bow) are keyed in Blender on the skeletons that run clips in the game:
the UE4 mannequin the thugs wear and the UEFN mannequin under Kate and Clint (claude-docs/animation.md, "Authored
clips"). Blender needs those skeletons, and the idles the clips start and end on, so this writes them out once:

    <assets root>/built/clips/rigs/A_Thug_Idle.fbx            ThirdPersonIdle on the thugs' UE4 mannequin: the skeleton,
                                                              and the pose a thug's clips start and end on
    <assets root>/built/clips/rigs/A_Kate_Idle.fbx            the sample's neutral standing idle on Kate's UEFN mannequin
    <assets root>/built/clips/rigs/A_Thug_KnockedOut.fbx      the thug's clip knockdown, whose last frame his get-up
                                                              from the back starts on

An animation's FBX carries its skeleton, which is all Blender needs; the meshes are not exported (the skeletal mesh
exporter asserts in a headless editor). Epic's content stays outside the repo. Run headless:

    UnrealEditor-Cmd.exe Hawkeye.uproject -run=pythonscript -script="Tools\\Editor\\export_clip_rigs.py" -unattended -nullrhi

Safe to run twice: an existing file is left alone unless HAWKEYE_FORCE=1.
"""

import os
import sys

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import _common as c  # noqa: E402

DEFAULT_ASSETS_ROOT = r"C:\Users\camer\code\hawkeye-assets"

EXPORTS = (
    ("/Game/Mannequin/Animations/ThirdPersonIdle", "A_Thug_Idle.fbx"),
    ("/Game/Characters/UEFN_Mannequin/Animations/Idle/M_Neutral_Stand_Idle_Loop", "A_Kate_Idle.fbx"),
    # The thug's clip knockdown (Mixamo's Knocked Out, retargeted by Tools/import-anims.ps1): the get-up from his back
    # starts on its last held frame.
    ("/Game/Mannequin/Animations/Combat/A_Knockdown_KnockedOut", "A_Thug_KnockedOut.fbx"),
)


def rigs_folder():
    root = os.environ.get("HAWKEYE_ASSETS_ROOT", "") or DEFAULT_ASSETS_ROOT
    return os.path.join(root, "built", "clips", "rigs")


def export(asset_path, filename):
    if os.path.isfile(filename) and os.environ.get("HAWKEYE_FORCE") != "1":
        c.log("exists", filename)
        return True
    asset = c.load_or_none(asset_path)
    if asset is None:
        c.log("skipped", asset_path, "not in Content (the sample or the mannequin is not imported)")
        return False
    options = unreal.FbxExportOption()
    c.set_props(options, [("ascii", False), ("collision", False), ("level_of_detail", False),
                          ("vertex_color", False), ("export_morph_targets", False),
                          ("map_skeletal_motion_to_root", False), ("export_local_time", True)], "fbx export")
    task = unreal.AssetExportTask()
    task.object = asset
    task.filename = filename
    task.automated = True
    task.prompt = False
    task.replace_identical = True
    task.options = options
    ok = unreal.Exporter.run_asset_export_task(task)
    c.log("created" if ok and os.path.isfile(filename) else "FAILED", filename, asset_path)
    return ok


def run():
    folder = rigs_folder()
    os.makedirs(folder, exist_ok=True)
    for asset_path, name in EXPORTS:
        export(asset_path, os.path.join(folder, name))


run()
