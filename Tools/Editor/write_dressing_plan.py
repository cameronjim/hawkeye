"""Write the hero block's December dressing plan to Tools/Data/dressing/<block>.json.

The plan comes from the committed OpenStreetMap records and the planners that read them (_dressing.py, about two and
a half minutes, the chase's planning world), so it is written once from plain Python and committed; generate_city.py
reads it back to draw the block (_dressing_build.py), and test_dressing.py fails when the committed file is not what
the planner makes from the committed data. Re-run this after changing the records or any planner the dressing reads.

    python -B Tools/Editor/write_dressing_plan.py

Plain Python, no editor: the generator is imported through test_city_golden's ``unreal`` stub, as the tests do.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import _dressing as dr  # noqa: E402
import _dressing_kit as kit  # noqa: E402


def main():
    import test_city_golden as golden  # noqa: PLC0415 - the stub that lets generate_city import without the editor
    gen = golden.import_generator()
    scene = dr.gather_scene(gen.District())
    block = dr.hero_block_id(scene)
    plan = dr.plan_scene_dressing(scene, block)
    path = kit.plan_path(block)
    if not os.path.isdir(os.path.dirname(path)):
        os.makedirs(os.path.dirname(path))
    dr.write_plan(plan, path)
    print(dr.describe(plan))
    for kind in dr.KIND_ORDER:
        if kind in plan["rejected"]:
            print("  {0} turned away: {1}".format(kind, ", ".join(
                "{0} {1}".format(k, v) for k, v in sorted(plan["rejected"][kind].items()))))
    counts = kit.group_counts(kit.load_plan(path))
    print("  {0} instances in {1} groups: {2}".format(sum(counts.values()), len(counts), ", ".join(
        "{0} {1}".format(k[len(kit.GROUP_PREFIX):], v) for k, v in counts.items())))
    print("wrote " + path)


if __name__ == "__main__":
    main()
