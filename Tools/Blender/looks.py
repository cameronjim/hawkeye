"""The enemies' looks as data (CLAUDE.md rule 2): one row per face and build, no code per thug.

Pure Python (standard library only, no bpy, no numpy) so the engine's bundled Python can test it
(`python Tools/Blender/test_looks.py`). The Blender builders read a row with `get_look` and hand its `face` to
`headbuild.build_head` and its `build` to `bodyshape.reshape_body`. A new thug look is a new row in `LOOKS`; the build
script is run with `--look <id>` and writes `built/<id>/SK_<Id>.glb`, which `Tools/Editor/import_bodies.py` picks up from
`BUILT_LOOKS`' variant rows (claude-docs/blender-pipeline.md, "Enemy heads and bodies").

Every face is an original: parameters of a sculpted base head, tuned apart by eye. None is modelled on a person, an
actor or a photograph, and a row must not be tuned toward one.

Numbers: lengths are metres in the Blender frame (X his left, -Y his front, Z up, standing at the origin); the
dimensionless ones are 1.0 at the neutral head or body, with the ranges in `FACE_RANGES` and `BUILD_RANGES`.
Colours are sRGB bytes as 0..1 (what a PNG shows), not linear.
"""

from dataclasses import dataclass, fields, replace

# --------------------------------------------------------------------------------------------------
# palettes
# --------------------------------------------------------------------------------------------------

# Skin as it shows on a PNG (sRGB 0..1), before the bake's own shading. Named so a row reads as a choice.
SKIN_TONES = {
    "fair": (0.86, 0.69, 0.60),
    "light": (0.80, 0.62, 0.51),
    "olive": (0.70, 0.52, 0.39),
    "tan": (0.62, 0.43, 0.31),
    "brown": (0.46, 0.30, 0.21),
    "dark": (0.31, 0.20, 0.15),
}

# Hair and brow colours, sRGB 0..1 (hair reads darker in the engine's light than it does on a swatch).
HAIR_COLOURS = {
    "black": (0.055, 0.050, 0.048),
    "dark_brown": (0.12, 0.075, 0.050),
    "brown": (0.22, 0.135, 0.080),
    "ginger": (0.38, 0.17, 0.075),
    "grey": (0.42, 0.41, 0.40),
    "blond": (0.52, 0.40, 0.24),
}

# Iris colours: (inner ring, outer ring) sRGB 0..1.
EYE_COLOURS = {
    "brown": ((0.30, 0.17, 0.08), (0.14, 0.08, 0.04)),
    "dark_brown": ((0.17, 0.10, 0.05), (0.07, 0.04, 0.025)),
    "hazel": ((0.42, 0.33, 0.14), (0.20, 0.14, 0.06)),
    "green": ((0.25, 0.36, 0.20), (0.10, 0.16, 0.09)),
    "blue": ((0.30, 0.45, 0.60), (0.11, 0.20, 0.32)),
    "grey": ((0.45, 0.50, 0.52), (0.18, 0.22, 0.25)),
}

# Headwear and cloth colours, sRGB 0..1.
CLOTH_COLOURS = {
    "black": (0.05, 0.05, 0.055),
    "charcoal": (0.16, 0.16, 0.17),
    "navy": (0.06, 0.09, 0.17),
    "maroon": (0.30, 0.06, 0.07),
    "grey": (0.40, 0.40, 0.41),
}

HAIR_STYLES = ("bald", "buzz", "crop", "short")
HEADWEAR = ("none", "beanie", "cap")
FACIAL_HAIR = ("none", "stubble", "goatee", "beard")

# --------------------------------------------------------------------------------------------------
# faces
# --------------------------------------------------------------------------------------------------

# name -> (lowest, highest) allowed. Neutral is 1.0 unless the name says otherwise.
FACE_RANGES = {
    "head_width": (0.92, 1.10),    # scales the skull's width
    "head_depth": (0.94, 1.08),    # and its length front to back
    "jaw_width": (0.84, 1.22),     # how far out the jaw angle sits
    "jaw_square": (0.0, 1.0),      # 0 round chin and jaw line, 1 square
    "chin": (0.60, 1.45),          # projection and size
    "brow": (0.0, 1.6),            # brow ridge heaviness
    "eye_spacing": (0.88, 1.14),
    "eye_size": (0.90, 1.10),
    "eye_sink": (0.5, 1.6),        # socket depth
    "cheek": (0.4, 1.6),           # cheekbone prominence
    "cheek_hollow": (0.0, 1.0),    # the hollow under the cheekbone
    "nose_proj": (0.80, 1.30),     # how far the tip stands out
    "nose_width": (0.84, 1.24),    # the wings
    "nose_bridge": (-0.6, 1.2),    # negative a dip, positive a hump
    "lips": (0.55, 1.55),          # fullness
    "mouth_width": (0.90, 1.12),
    "ear_size": (0.90, 1.18),
    "ear_flare": (14.0, 36.0),     # degrees the ear stands off the head
    "age": (0.0, 1.0),             # lines, bags, hollows, greying; 0 about twenty, 1 about sixty
    "neck_radius": (0.050, 0.068),  # metres, half width of the neck
    "skin_ruddy": (0.0, 1.0),      # flush on the cheeks and nose
    "hairline": (0.72, 0.88),      # forehead height in head heights
    "hair_fade": (0.0, 1.0),       # 0 the hair stops at one height all round, 1 a skin fade to the ear and nape
    "stubble": (0.0, 1.0),         # beard density (what the facial_hair region shows)
}


@dataclass(frozen=True)
class FaceSpec:
    """One face. Defaults are the neutral male head; a row overrides what makes its face its own."""

    id: str
    head_width: float = 1.0
    head_depth: float = 1.0
    jaw_width: float = 1.0
    jaw_square: float = 0.4
    chin: float = 1.0
    brow: float = 0.6
    eye_spacing: float = 1.0
    eye_size: float = 1.0
    eye_sink: float = 1.0
    cheek: float = 1.0
    cheek_hollow: float = 0.3
    nose_proj: float = 1.0
    nose_width: float = 1.0
    nose_bridge: float = 0.0
    lips: float = 1.0
    mouth_width: float = 1.0
    ear_size: float = 1.0
    ear_flare: float = 24.0
    age: float = 0.3
    neck_radius: float = 0.058
    skin: str = "light"
    skin_ruddy: float = 0.3
    eye_colour: str = "brown"
    hair_style: str = "buzz"
    hair_colour: str = "black"
    hairline: float = 0.80
    hair_fade: float = 0.5
    headwear: str = "none"
    headwear_colour: str = "black"
    facial_hair: str = "none"
    stubble: float = 0.0
    seed: int = 1

    def problems(self):
        """A list of what is wrong with this row (empty when it is fine)."""
        out = []
        for name, (lo, hi) in FACE_RANGES.items():
            value = getattr(self, name)
            if not isinstance(value, (int, float)) or isinstance(value, bool):
                out.append("{0}.{1} is not a number: {2!r}".format(self.id, name, value))
            elif not lo <= value <= hi:
                out.append("{0}.{1} = {2} is outside {3}..{4}".format(self.id, name, value, lo, hi))
        for name, table in (("skin", SKIN_TONES), ("eye_colour", EYE_COLOURS), ("hair_colour", HAIR_COLOURS),
                            ("headwear_colour", CLOTH_COLOURS)):
            if getattr(self, name) not in table:
                out.append("{0}.{1} = {2!r} is not one of {3}".format(self.id, name, getattr(self, name),
                                                                       sorted(table)))
        for name, choices in (("hair_style", HAIR_STYLES), ("headwear", HEADWEAR), ("facial_hair", FACIAL_HAIR)):
            if getattr(self, name) not in choices:
                out.append("{0}.{1} = {2!r} is not one of {3}".format(self.id, name, getattr(self, name), choices))
        if self.facial_hair == "none" and self.stubble > 0.0:
            out.append("{0}: stubble {1} with facial_hair 'none' (it would not show)".format(self.id, self.stubble))
        if not isinstance(self.seed, int) or isinstance(self.seed, bool):
            out.append("{0}.seed is not an int".format(self.id))
        return out

    def skin_rgb(self):
        return SKIN_TONES[self.skin]

    def hair_rgb(self):
        return HAIR_COLOURS[self.hair_colour]


# --------------------------------------------------------------------------------------------------
# bodies
# --------------------------------------------------------------------------------------------------

# The pack's Superhero_Male is a bodybuilder (shoulders 54 cm across the deltoids, a 27 cm deep chest, thighs 13 cm
# in radius). The clothes are built round a reshaped copy of it, under the cloth only: the skeleton, its rest pose and
# the weights are untouched. Each factor scales the body's own distance from its bone line (or torso axis).
BUILD_RANGES = {
    "shoulder": (0.78, 1.0),   # torso width above the chest, toward the spine axis
    "chest": (0.78, 1.0),      # chest depth, front and back
    "waist": (0.84, 1.04),     # torso width at the waist (above 1.0 a belly's sides)
    "belly": (0.0, 1.0),       # how far the stomach pushes forward under the jacket (0 none, 1 about 6 cm)
    "arm": (0.74, 1.0),        # upper arm and forearm girth
    "thigh": (0.76, 1.0),      # thigh and calf girth
    "hip": (0.88, 1.0),        # hips and seat, width
    "ease": (0.0, 0.035),      # extra metres of room in the cloth over the body (hang, not hug)
    "ankle": (0.050, 0.085),   # trouser radius at the ankle, metres
    "trouser_taper": (0.0, 1.0),  # 0 straight tube from the thigh, 1 tapered to the ankle radius
}


@dataclass(frozen=True)
class BodyBuild:
    id: str
    shoulder: float = 1.0
    chest: float = 1.0
    waist: float = 1.0
    belly: float = 0.0
    arm: float = 1.0
    thigh: float = 1.0
    hip: float = 1.0
    ease: float = 0.012
    ankle: float = 0.062
    trouser_taper: float = 0.8

    def problems(self):
        out = []
        for name, (lo, hi) in BUILD_RANGES.items():
            value = getattr(self, name)
            if not isinstance(value, (int, float)) or isinstance(value, bool):
                out.append("{0}.{1} is not a number: {2!r}".format(self.id, name, value))
            elif not lo <= value <= hi:
                out.append("{0}.{1} = {2} is outside {3}..{4}".format(self.id, name, value, lo, hi))
        return out


BUILDS = {
    # Wiry: a runner's frame under a loose suit.
    "lean": BodyBuild("lean", shoulder=0.84, chest=0.82, waist=0.90, belly=0.0, arm=0.78, thigh=0.80, hip=0.92,
                      ease=0.016, ankle=0.056),
    # An ordinary adult man who is not a gym regular.
    "average": BodyBuild("average", shoulder=0.88, chest=0.86, waist=0.94, belly=0.15, arm=0.82, thigh=0.84, hip=0.94,
                         ease=0.014, ankle=0.060),
    # Thick through the middle, a soft stomach: the suit hangs from it.
    "stocky": BodyBuild("stocky", shoulder=0.92, chest=0.90, waist=1.0, belly=0.45, arm=0.88, thigh=0.88, hip=0.96,
                        ease=0.012, ankle=0.064),
    # The heavy: broad and big, but fat and muscle under a puffer, not the pack's abs.
    "heavy": BodyBuild("heavy", shoulder=0.96, chest=0.94, waist=1.03, belly=0.7, arm=0.94, thigh=0.92, hip=0.98,
                       ease=0.010, ankle=0.068),
}


# Heights (metres) the torso's scale profiles are defined at, bottom to top.
PROFILE_Z = (0.78, 0.92, 1.04, 1.16, 1.30, 1.44, 1.54)


def torso_scale_profiles(build):
    """The torso's scale factors at PROFILE_Z: (x, front depth, back depth), each a tuple of len(PROFILE_Z). 1.0 is the
    pack's body. Hips and waist set the width low down, shoulder and chest high up; both return to 1.0 at the neck,
    which is replaced anyway."""
    hip, waist, shoulder, chest = build.hip, build.waist, build.shoulder, build.chest
    mid = 0.5 * (waist + shoulder)
    x = (hip, 0.5 * (hip + waist), waist, mid, shoulder, shoulder, 1.0)
    half = 0.5 * (1.0 + chest)
    front = (1.0, 1.0, 1.0, half, chest, half, 1.0)
    back = (1.0, 1.0, 1.0, half, half, half + 0.1 * (1.0 - chest), 1.0)
    return x, front, back


# --------------------------------------------------------------------------------------------------
# looks: the rows
# --------------------------------------------------------------------------------------------------


@dataclass(frozen=True)
class Look:
    """A built enemy: its id (the folder under built/ and the mesh name), who wears it, a face and a build."""

    id: str
    family: str          # "thug" (the tracksuit) or "heavy" (tracksuit, vest, mask, boots)
    face: FaceSpec
    build: BodyBuild

    def problems(self):
        out = []
        if self.family not in FAMILIES:
            out.append("{0}.family = {1!r} is not one of {2}".format(self.id, self.family, FAMILIES))
        if not self.id or not self.id.replace("_", "").isalnum() or self.id != self.id.lower():
            out.append("{0}: ids are lowercase letters, digits and underscores".format(self.id))
        elif not self.id.startswith(self.family):
            out.append("{0}: the id of a {1} look starts with '{1}'".format(self.id, self.family))
        out += self.face.problems() + self.build.problems()
        return out


FAMILIES = ("thug", "heavy")


def _row(look_id, family, build, **face):
    return Look(look_id, family, FaceSpec(look_id, **face), BUILDS[build])


LOOKS = {look.id: look for look in (
    # thug: the default (what built/thug has always been). Broad skull, heavy brow, small deep eyes, a short wide nose,
    # a square jaw, stubble, a buzz cut. Mid-thirties.
    _row("thug", "thug", "stocky", head_width=1.04, jaw_width=1.10, jaw_square=0.75, chin=1.05, brow=1.2,
         eye_spacing=0.97, eye_size=0.94, eye_sink=1.3, cheek=0.9, cheek_hollow=0.15, nose_proj=0.9, nose_width=1.1,
         nose_bridge=-0.1, lips=0.85, mouth_width=1.02, ear_size=1.0, ear_flare=26.0, age=0.5, neck_radius=0.064,
         skin="olive", skin_ruddy=0.35, eye_colour="dark_brown", hair_style="buzz", hair_colour="black", hairline=0.79,
         hair_fade=0.6, facial_hair="stubble", stubble=0.55, seed=11),
    # thug_b: long and narrow, a hooked nose, high cheekbones, close-set eyes, a charcoal beanie low over the brow.
    _row("thug_b", "thug", "lean", head_width=0.96, head_depth=1.03, jaw_width=0.90, jaw_square=0.2, chin=0.9,
         brow=0.5, eye_spacing=0.93, eye_size=1.0, eye_sink=1.0, cheek=1.35, cheek_hollow=0.7, nose_proj=1.2,
         nose_width=0.9, nose_bridge=0.9, lips=0.8, mouth_width=0.97, ear_size=1.1, ear_flare=30.0, age=0.3,
         neck_radius=0.052, skin="fair", skin_ruddy=0.5, eye_colour="grey", hair_style="short", hair_colour="brown",
         hairline=0.82, hair_fade=0.2, headwear="beanie", headwear_colour="charcoal", facial_hair="stubble",
         stubble=0.3, seed=23),
    # thug_c: round and soft, full cheeks and lips, a snub nose, wide-set eyes, a black cap, a short dark beard.
    _row("thug_c", "thug", "stocky", head_width=1.06, head_depth=0.98, jaw_width=1.0, jaw_square=0.15, chin=0.75,
         brow=0.3, eye_spacing=1.07, eye_size=1.05, eye_sink=0.7, cheek=1.5, cheek_hollow=0.0, nose_proj=0.82,
         nose_width=1.15, nose_bridge=-0.5, lips=1.4, mouth_width=1.05, ear_size=0.95, ear_flare=20.0, age=0.25,
         neck_radius=0.063, skin="brown", skin_ruddy=0.2, eye_colour="dark_brown", hair_style="crop",
         hair_colour="black", hairline=0.77, hair_fade=0.7, headwear="cap", headwear_colour="black",
         facial_hair="beard", stubble=0.8, seed=37),
    # thug_d: a shaved head, a long square jaw, a straight nose, hooded eyes, older, a goatee going grey.
    _row("thug_d", "thug", "average", head_width=1.0, head_depth=1.06, jaw_width=1.16, jaw_square=1.0, chin=1.3,
         brow=0.9, eye_spacing=1.0, eye_size=0.92, eye_sink=1.45, cheek=0.7, cheek_hollow=0.85, nose_proj=1.05,
         nose_width=0.95, nose_bridge=0.2, lips=0.65, mouth_width=1.0, ear_size=1.12, ear_flare=18.0, age=0.8,
         neck_radius=0.060, skin="tan", skin_ruddy=0.55, eye_colour="hazel", hair_style="bald", hair_colour="grey",
         hairline=0.80, hair_fade=0.0, facial_hair="goatee", stubble=0.75, seed=53),
    # heavy: mostly under a balaclava; the eyes and brow are what show. Heavy brow, deep small eyes, broad.
    _row("heavy", "heavy", "heavy", head_width=1.08, head_depth=1.05, jaw_width=1.18, jaw_square=0.9, chin=1.2,
         brow=1.5, eye_spacing=1.02, eye_size=0.92, eye_sink=1.5, cheek=1.1, cheek_hollow=0.1, nose_proj=1.0,
         nose_width=1.2, nose_bridge=0.3, lips=0.9, mouth_width=1.06, ear_size=1.05, ear_flare=22.0, age=0.55,
         neck_radius=0.068, skin="tan", skin_ruddy=0.4, eye_colour="brown", hair_style="bald", hair_colour="black",
         hairline=0.78, hair_fade=0.0, facial_hair="none", stubble=0.0, seed=71),
)}

# Looks a Blueprint picks from, in order: BP_Thug takes the thug family (its own BodyLook is the first), BP_Thug_Heavy the heavy.
BLUEPRINT_LOOKS = {
    "BP_Thug": ("thug", "thug_b", "thug_c", "thug_d"),
    "BP_Thug_Heavy": ("heavy",),
}


def get_look(look_id, default="thug"):
    """The row for an id ('' means default). SystemExit-friendly: raises KeyError naming the choices."""
    key = look_id or default
    if key not in LOOKS:
        raise KeyError("no look '{0}'; the rows are {1}".format(key, ", ".join(sorted(LOOKS))))
    return LOOKS[key]


def look_ids(family=None):
    return [i for i, look in LOOKS.items() if family is None or look.family == family]


def all_problems():
    """Every problem in every row and in the tables, for the tests and for the builder to refuse a bad row."""
    out = []
    for look in LOOKS.values():
        out += look.problems()
    for build in BUILDS.values():
        out += build.problems()
    for bp, ids in BLUEPRINT_LOOKS.items():
        for look_id in ids:
            if look_id not in LOOKS:
                out.append("{0} names the look '{1}', which is not a row".format(bp, look_id))
    for name in _all_colours():
        if not all(0.0 <= c <= 1.0 for c in name[1]):
            out.append("colour {0} has a channel outside 0..1: {1}".format(*name))
    return out


def _all_colours():
    for table in (SKIN_TONES, HAIR_COLOURS, CLOTH_COLOURS):
        for key, rgb in table.items():
            yield key, rgb
    for key, pair in EYE_COLOURS.items():
        for rgb in pair:
            yield key, rgb


def with_face(look, **changes):
    """A copy of a look with some face parameters changed (the builder's --tune and the tests)."""
    allowed = {f.name for f in fields(FaceSpec)}
    unknown = set(changes) - allowed
    if unknown:
        raise KeyError("not face parameters: {0}".format(sorted(unknown)))
    return replace(look, face=replace(look.face, **changes))
