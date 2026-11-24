"""Reads both driver skeletons and the body's and prints the hand frames _handpose sees (headless, nothing is saved).

    UnrealEditor-Cmd.exe Hawkeye.uproject -run=pythonscript -script=Tools\\Editor\\probe_handpose.py -unattended -nullrhi
"""

import os
import sys

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import _handpose as hp  # noqa: E402

MESHES = [("uefn", "/Game/Characters/UEFN_Mannequin/Meshes/SKM_UEFN_Mannequin"),
          ("ue4", "/Game/Mannequin/Character/Mesh/SK_Mannequin"),
          ("male", "/Game/Characters/Bodies/SK_Body_Male"),
          ("female", "/Game/Characters/Bodies/SK_Body_Female")]


def log(text):
    unreal.log("[HandProbe] " + text)


def run():
    skels = {}
    for key, path in MESHES:
        mesh = unreal.load_asset(path)
        if mesh is None:
            log("{0}: not found".format(key))
            continue
        log("{0}: reading".format(key))
        skel = hp.read_skeleton(mesh)
        skels[key] = skel
        log("{0}: {1} bones".format(key, len(skel.pos)))
        for side in hp.SIDES:
            h, n, p, s = hp.palm_frame(skel, side)
            log("{0} {1}: hand dir ({2:.2f}, {3:.2f}, {4:.2f}), palm normal ({5:.2f}, {6:.2f}, {7:.2f}), into palm ({8:.2f}, {9:.2f}, {10:.2f}), tip axis {11}".format(
                key, side, h[0], h[1], h[2], n[0], n[1], n[2], p[0], p[1], p[2], hp.tip_axis_sign(skel, side)))
            d = hp.directions(skel, side)
            for bone in ("upperarm_" + side, "lowerarm_" + side, "hand_" + side, "index_01_" + side, "index_03_" + side, "thumb_01_" + side):
                v = d[bone]
                log("{0} {1}: dir ({2:.2f}, {3:.2f}, {4:.2f})".format(key, bone, v[0], v[1], v[2]))
    for target in ("male", "female"):
        for source in ("uefn", "ue4"):
            if target in skels and source in skels:
                _offsets, report = hp.solve(skels[source], skels[target])
                log("{0} <- {1}: worst direction error {2:.2f} deg".format(
                    target, source, max(max(v.values()) for v in report.values())))


run()
