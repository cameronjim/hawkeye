"""Golden test: the city generator's placements over the committed East Village data
(Tools/Data/osm/east_village.*.json) hash to the values recorded before generate_city's pure parts
were split into _geomutil, _district, _fire_escapes, _reach and _ledges. A change here means the
generated district changes; if that is intended, say so in the commit and record the new hashes.

Plain CPython, no editor. generate_city imports ``unreal`` at the top and clutter_plan reaches
player_start_transform (unreal.Vector, unreal.Rotator), so outside the editor this installs a stub:
Vector and Rotator hold their numbers, everything else is an inert placeholder. Takes about 30 s.

    python Tools/Editor/test_city_golden.py

Hashes: sha256 of the JSON of the output with every float as repr(), first 16 hex digits. Recorded
with CPython 3.12 on Windows.
"""

import hashlib
import json
import os
import sys
import types
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

GOLDEN = {
    "facade_style": "ae9cb6a634805a36",
    "lamp_spots": "99da37accac1a571",
    "ledge_spots": "132273595296ea25",
    "fire_escape_spots": "b0342c208fc44bc0",
    "anchor_spots": "7dab19e466ea9fd2",
    "archer_placements": "3aa6f24537b5d483",
    "clutter_plan": "647ed3d9105e0e97",
}


class _Placeholder(object):
    def __init__(self, *args, **kwargs):
        pass

    def __call__(self, *args, **kwargs):
        return _Placeholder()

    def __getattr__(self, name):
        if name.startswith("__"):
            raise AttributeError(name)
        return _Placeholder()


class _Vector(object):
    def __init__(self, x=0.0, y=0.0, z=0.0):
        self.x, self.y, self.z = x, y, z


class _Rotator(object):
    def __init__(self, roll=0.0, pitch=0.0, yaw=0.0):
        self.roll, self.pitch, self.yaw = roll, pitch, yaw


class _UnrealStub(types.ModuleType):
    Vector = _Vector
    Rotator = _Rotator

    def __getattr__(self, name):
        if name.startswith("__"):
            raise AttributeError(name)
        return _Placeholder


def import_generator():
    try:
        import unreal  # noqa: F401 - in the editor, use the real one
    except ImportError:
        sys.modules["unreal"] = _UnrealStub("unreal")
    import generate_city
    return generate_city


def canon(value):
    if isinstance(value, float):
        return repr(value)
    if isinstance(value, (int, str, bool)) or value is None:
        return value
    if isinstance(value, (list, tuple)):
        return [canon(v) for v in value]
    if isinstance(value, (set, frozenset)):
        return sorted((canon(v) for v in value), key=repr)
    if isinstance(value, dict):
        return [[canon(k), canon(v)] for k, v in value.items()]
    raise TypeError("unexpected {0} in a placement".format(type(value).__name__))


def digest(value):
    return hashlib.sha256(json.dumps(canon(value)).encode("utf-8")).hexdigest()[:16]


class GoldenTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.gen = import_generator()
        cls.district = cls.gen.District()

    def check(self, name, value):
        self.assertEqual(digest(value), GOLDEN[name], name + " changed")

    def test_facade_style(self):
        self.check("facade_style", [self.gen.facade_style(rec) for rec in self.district.buildings])

    def test_lamp_spots(self):
        self.check("lamp_spots", self.gen.lamp_spots(self.district))

    def test_ledge_spots(self):
        self.check("ledge_spots", self.gen.ledge_spots(self.district))

    def test_fire_escape_spots(self):
        self.check("fire_escape_spots", self.gen.fire_escape_spots(self.district))

    def test_anchor_spots(self):
        self.check("anchor_spots", self.gen.anchor_spots(self.district))

    def test_archer_placements(self):
        self.check("archer_placements", self.gen.archer_placements(self.district))

    def test_clutter_plan(self):
        self.check("clutter_plan", self.gen.clutter_plan(self.district))


if __name__ == "__main__":
    unittest.main()
