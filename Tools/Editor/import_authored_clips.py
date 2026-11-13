"""Import the clips we author ourselves (Tools/Blender/build_clips.py) into the game: a create_all.py step.

The get-ups and hit reactions are keyed in Blender on the drivers' own skeletons and written outside the repo, like the
built bodies (<assets root>/built/clips/ue4 for the thugs, .../uefn for Kate and Clint). This runs the combat clip
pipeline (import_combat_anims.py) on just those two manifest sources, AuthoredThug and AuthoredKate: FBX import, IK
retarget onto each target, AM_ montages. create_combat_anims (the next step) then puts the montages in the anim sets.

Idempotent: an FBX whose size and time are unchanged is not imported again, and its retargets and montages are kept;
a rebuilt one is imported again and everything downstream of it redone. No files there (a fresh clone, or the clips
not built): one "missing" line per source and the game plays what it did before (the fallback rule, animation.md).
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import _common as c  # noqa: E402
import import_combat_anims  # noqa: E402

SOURCES = ("AuthoredThug", "AuthoredKate")


def run():
    for name in SOURCES:
        try:
            import_combat_anims.run(args={"force": False, "only": name})
        except Exception as exc:  # noqa: BLE001 - one source must not stop the other
            c.log_error("authored clips " + name, exc)
