"""Build every particle effect headless, with nothing downloaded.

    /Game/VFX/Materials/M_Vfx_Smoke      lit translucent soft sprite broken up by the engine's T_Noise01
                                         (smoke, dust, snow puffs, chimney wisps)
    /Game/VFX/Materials/M_Vfx_Glow       additive unlit soft dot (fire, sparks, flashes, the EMP)
    /Game/VFX/Materials/M_Vfx_Beam       additive unlit, soft across a ribbon's width (trails, the zip
                                         line, tracers)
    /Game/VFX/Materials/M_Vfx_Snow       translucent soft dot with a little self light (falling snow)
    /Game/VFX/Materials/M_Vfx_Putty      translucent unlit blob (putty splat)
    /Game/VFX/Materials/M_Decal_Scorch   deferred decal, a ragged sooty patch that fades with the decal's lifetime
    /Game/VFX/Materials/M_PP_EmpAberration  post process: red and blue pulled apart from the centre by
                                         Intensity (the EMP's screen pulse)
    /Game/VFX/NS_*                       the Niagara systems (EFFECTS below)

Route (claude-docs/workflow.md, "use what exists first"): every emitter starts as a copy of one of
the Niagara plugin's own template emitters under /Niagara/DefaultAssets/Templates/Emitters
(OmnidirectionalBurst, DirectionalBurst, Fountain, SimpleSpriteBurst, DynamicBeam,
RecycleParticlesInView). Niagara exposes nothing to Python for editing emitters, so the module
inputs, renderer and emitter properties are set through UHawkeyeVfxBuilder (C++, the Niagara
editor's stack view model) from the recipes here.

Idempotent: each asset carries a HawkeyeVfxHash metadata tag (its recipe plus VFX_BUILD). An
unchanged recipe is left alone; a changed one is rebuilt in place at the same path.
"""

import hashlib
import os
import sys

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import _common as c  # noqa: E402
import _materials as m  # noqa: E402

VFX_PATH = "/Game/VFX"
MATERIALS_PATH = VFX_PATH + "/Materials"
HASH_TAG = "HawkeyeVfxHash"
VFX_BUILD = "vfx-1"      # bump to rebuild everything (a change to the helpers below)

TEMPLATES = "/Niagara/DefaultAssets/Templates/Emitters/"
MODULES = "/Niagara/Modules/"
NOISE_TEXTURE = "/Engine/MaterialTemplates/Textures/T_Noise01.T_Noise01"

NIAGARA_USAGE = ["used_with_niagara_sprites", "used_with_niagara_ribbons", "used_with_niagara_mesh_particles"]


def _mat(name):
    return "{0}/{1}.{1}".format(MATERIALS_PATH, name)


def _hash(text):
    return hashlib.sha1((VFX_BUILD + "\n" + text).encode("utf-8")).hexdigest()[:16]


def _tag_of(asset):
    try:
        return unreal.EditorAssetLibrary.get_metadata_tag(asset, HASH_TAG) if asset is not None else None
    except Exception:  # noqa: BLE001
        return None


# --------------------------------------------------------------------------------------
# materials
# --------------------------------------------------------------------------------------


def _uv(material, x, y):
    return m.expr(material, "MaterialExpressionTextureCoordinate", x, y, None, "TexCoord")


def _centre_distance(material, x, y):
    """0 at the sprite's centre, 1 at the middle of each edge."""
    half = m.expr(material, "MaterialExpressionConstant2Vector", x, y + 120, [("r", 0.5), ("g", 0.5)], "Half")
    dist = m.expr(material, "MaterialExpressionDistance", x + 200, y, None, "Distance")
    m.connect(_uv(material, x, y), "", dist, "A")
    m.connect(half, "", dist, "B")
    return m.multiply(material, dist, None, x + 380, y, const_b=2.0)


def _soft_dot(material, exponent, x, y):
    """saturate(1 - d) ^ exponent: a round sprite with a soft edge."""
    inside = m.clamp01(material, m.subtract(material, m.constant(material, 1.0, x + 400, y - 60),
                                            _centre_distance(material, x - 200, y), x + 550, y), x + 700, y)
    power = m.expr(material, "MaterialExpressionPower", x + 850, y, [("const_exponent", float(exponent))], "Power")
    m.connect(inside, "", power, "Base")
    return power


def _particle_color(material, x, y):
    return m.expr(material, "MaterialExpressionParticleColor", x, y, None, "ParticleColor")


def _rgb(material, node, x, y):
    return m.component_mask(material, node, r=True, g=True, b=True, x=x, y=y)


def _alpha(material, node, x, y):
    """The particle colour's alpha: its own A output (the default output is RGB only)."""
    alpha = m.expr(material, "MaterialExpressionMultiply", x, y, [("const_b", 1.0)], "Alpha")
    m.connect(node, "A", alpha, "A")
    return alpha


def _setup(material, blend, lit=False, two_sided=True):
    props = [("blend_mode", blend), ("two_sided", two_sided)]
    if not lit:
        props.append(("shading_model", unreal.MaterialShadingModel.MSM_UNLIT))
    c.set_props(material, props, material.get_name())


def _build_smoke(material):
    """Lit translucent: BaseColor is the particle colour, a little self light so it reads between
    lamps, opacity a soft dot broken up by panning noise, faded where it meets geometry."""
    _setup(material, unreal.BlendMode.BLEND_TRANSLUCENT, lit=True)
    color = _particle_color(material, -1600, -300)
    rgb = _rgb(material, color, -1350, -300)
    m.connect_property(rgb, unreal.MaterialProperty.MP_BASE_COLOR)
    glow = m.multiply(material, rgb, m.scalar_param(material, "SelfLight", 0.15, -1350, -150), -1100, -250)
    m.connect_property(m.multiply(material, glow, None, -900, -250, const_b=m.EMISSIVE_INTENSITY_FACTOR),
                       unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    # Noise: the sprite's UVs scaled and nudged by the particle's random so no two puffs match, panning
    # slowly so the cloud churns.
    rand = m.expr(material, "MaterialExpressionParticleRandom", -1900, 300, None, "ParticleRandom")
    uv = m.multiply(material, _uv(material, -1900, 150), None, -1700, 150, const_b=0.6)
    shifted = m.add(material, uv, m.multiply(material, rand, None, -1700, 300, const_b=0.37), -1500, 200)
    panner = m.expr(material, "MaterialExpressionPanner", -1300, 200, [("speed_x", 0.03), ("speed_y", 0.05)], "Panner")
    m.connect(shifted, "", panner, "Coordinate")
    sample = m.expr(material, "MaterialExpressionTextureSample", -1100, 200,
                    [("texture", unreal.load_asset(NOISE_TEXTURE))], "Noise")
    m.connect(panner, "", sample, "UVs")
    breakup = m.clamp01(material, m.add(material, m.multiply(material, sample, None, -900, 200, const_b=1.3),
                                        None, -750, 200, const_b=-0.15), -600, 200)
    breakup_r = m.component_mask(material, breakup, r=True, x=-450, y=200)
    mask = m.multiply(material, _soft_dot(material, 1.6, -1900, 500), breakup_r, -300, 350)
    opacity = m.multiply(material, mask, _alpha(material, color, -1350, -50), -150, 250)
    depth = m.expr(material, "MaterialExpressionDepthFade", 50, 250, [("fade_distance_default", 60.0)], "DepthFade")
    m.connect(opacity, "", depth, "Opacity")
    m.connect_property(depth, unreal.MaterialProperty.MP_OPACITY)
    m.set_scalar_property(material, 1.0, unreal.MaterialProperty.MP_ROUGHNESS, -150, 500)


def _build_glow(material):
    """Additive unlit: particle colour times alpha times a soft dot. HDR colours make it burn."""
    _setup(material, unreal.BlendMode.BLEND_ADDITIVE)
    color = _particle_color(material, -1300, -200)
    dot = _soft_dot(material, 2.2, -1500, 200)
    strength = m.multiply(material, dot, _alpha(material, color, -1050, 50), -500, 100)
    glow = m.multiply(material, _rgb(material, color, -1050, -200), strength, -300, 0)
    m.connect_property(m.multiply(material, glow, None, -120, 0, const_b=m.EMISSIVE_INTENSITY_FACTOR),
                       unreal.MaterialProperty.MP_EMISSIVE_COLOR)


def _build_beam(material):
    """Additive unlit for ribbons: bright down the middle of the width (V), fading at the sides and
    along the length toward the tail (U, 0 at the head)."""
    _setup(material, unreal.BlendMode.BLEND_ADDITIVE)
    color = _particle_color(material, -1300, -250)
    uv = _uv(material, -1700, 150)
    v = m.component_mask(material, uv, g=True, x=-1500, y=150)
    across = m.absolute(material, m.multiply(material, m.subtract(material, v, None, -1350, 150, const_b=0.5),
                                             None, -1200, 150, const_b=2.0), -1050, 150)
    core = m.clamp01(material, m.subtract(material, m.constant(material, 1.0, -950, 50), across, -850, 150), -700, 150)
    soft = m.expr(material, "MaterialExpressionPower", -550, 150, [("const_exponent", 1.5)], "Power")
    m.connect(core, "", soft, "Base")
    strength = m.multiply(material, soft, _alpha(material, color, -1050, -50), -350, 50)
    glow = m.multiply(material, _rgb(material, color, -1050, -250), strength, -200, -100)
    m.connect_property(m.multiply(material, glow, None, -50, -100, const_b=m.EMISSIVE_INTENSITY_FACTOR),
                       unreal.MaterialProperty.MP_EMISSIVE_COLOR)


def _build_snow(material):
    """A flake: translucent, lit by the lamps, with enough self light to read in the dark."""
    _setup(material, unreal.BlendMode.BLEND_TRANSLUCENT, lit=True)
    color = _particle_color(material, -1300, -250)
    rgb = _rgb(material, color, -1050, -250)
    m.connect_property(rgb, unreal.MaterialProperty.MP_BASE_COLOR)
    glow = m.multiply(material, rgb, m.scalar_param(material, "SelfLight", 0.9, -1050, -100), -850, -200)
    m.connect_property(m.multiply(material, glow, None, -650, -200, const_b=m.EMISSIVE_INTENSITY_FACTOR),
                       unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    opacity = m.multiply(material, _soft_dot(material, 1.5, -1500, 200), _alpha(material, color, -1050, 50), -400, 100)
    m.connect_property(opacity, unreal.MaterialProperty.MP_OPACITY)


def _build_putty(material):
    """Unlit translucent blob, a hard-ish edge and a lighter middle: wet purple putty."""
    _setup(material, unreal.BlendMode.BLEND_TRANSLUCENT)
    color = _particle_color(material, -1300, -250)
    rgb = _rgb(material, color, -1050, -250)
    shine = m.add(material, m.multiply(material, _soft_dot(material, 4.0, -1700, -50), None, -700, -50, const_b=0.8),
                  None, -550, -50, const_b=1.0)
    m.connect_property(m.multiply(material, m.multiply(material, rgb, shine, -400, -200), None, -250, -200,
                                  const_b=m.EMISSIVE_INTENSITY_FACTOR * 0.5), unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    edge = m.clamp01(material, m.multiply(material, _soft_dot(material, 0.6, -1700, 300), None, -600, 300,
                                          const_b=3.0), -450, 300)
    m.connect_property(m.multiply(material, edge, _alpha(material, color, -1050, 50), -300, 200),
                       unreal.MaterialProperty.MP_OPACITY)


def _build_scorch(material):
    """Deferred decal: soot, darkest at the centre, ragged at the edge, fading with the decal's life."""
    c.set_props(material, [("material_domain", unreal.MaterialDomain.MD_DEFERRED_DECAL),
                           ("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)], material.get_name())
    m.connect_property(m.constant3(material, (0.015, 0.012, 0.01), -600, -200), unreal.MaterialProperty.MP_BASE_COLOR)
    m.set_scalar_property(material, 0.95, unreal.MaterialProperty.MP_ROUGHNESS, -600, -50)
    sample = m.expr(material, "MaterialExpressionTextureSample", -1400, 250,
                    [("texture", unreal.load_asset(NOISE_TEXTURE))], "Noise")
    m.connect(m.multiply(material, _uv(material, -1700, 250), None, -1550, 250, const_b=1.7), "", sample, "UVs")
    ragged = m.component_mask(material, sample, r=True, x=-1200, y=250)
    dot = _soft_dot(material, 0.8, -2100, 450)
    body = m.clamp01(material, m.multiply(material, m.subtract(material, m.add(material, dot, None, -900, 400, const_b=0.0),
                                                               m.multiply(material, ragged, None, -1000, 250, const_b=0.45),
                                                               -800, 350), None, -650, 350, const_b=2.2), -500, 350)
    # The decal component's own fade (UDecalComponent::SetFadeOut) reaches the material through this.
    fade = m.expr(material, "MaterialExpressionDecalLifetimeOpacity", -650, 550, None, "DecalLifetimeOpacity")
    m.connect_property(m.multiply(material, m.multiply(material, body, fade, -350, 400), None, -200, 400, const_b=0.9),
                       unreal.MaterialProperty.MP_OPACITY)


def _scene_sample(material, uv_node, x, y):
    node = m.expr(material, "MaterialExpressionSceneTexture", x, y, [
        ("scene_texture_id", unreal.SceneTextureId.PPI_POST_PROCESS_INPUT0)], "SceneTexture")
    m.connect(uv_node, "", node, "UVs")
    return node


def _build_aberration(material):
    """Post process: the frame's red pulled outward and blue inward from the centre by Intensity (0 is
    a no-op), with a cold tint. Driven by AHawkeyeCharacter::PlayScreenPulse."""
    c.set_props(material, [("material_domain", unreal.MaterialDomain.MD_POST_PROCESS)], material.get_name())
    try:
        material.set_editor_property("blendable_location", unreal.BlendableLocation.BL_SCENE_COLOR_AFTER_TONEMAPPING)
    except Exception:  # noqa: BLE001 - the enum name differs between versions; the default place works too
        pass
    screen = m.expr(material, "MaterialExpressionScreenPosition", -2000, 0, None, "ScreenPosition")
    intensity = m.scalar_param(material, "Intensity", 0.0, -2000, 300)
    from_centre = m.subtract(material, screen, m.expr(material, "MaterialExpressionConstant2Vector", -1900, 150,
                                                      [("r", 0.5), ("g", 0.5)], "Half"), -1750, 50)
    offset = m.multiply(material, m.multiply(material, from_centre, intensity, -1550, 100), None, -1400, 100,
                        const_b=0.018)
    red_uv = m.add(material, screen, offset, -1200, -150)
    blue_uv = m.subtract(material, screen, offset, -1200, 250)
    red = m.component_mask(material, _scene_sample(material, red_uv, -1000, -150), r=True, x=-750, y=-150)
    green = m.component_mask(material, _scene_sample(material, screen, -1000, 50), g=True, x=-750, y=50)
    blue = m.component_mask(material, _scene_sample(material, blue_uv, -1000, 250), b=True, x=-750, y=250)
    rgb = m.append(material, m.append(material, red, green, -550, -50), blue, -400, 50)
    tint = m.lerp(material, rgb, m.multiply(material, rgb, m.constant3(material, (0.85, 0.95, 1.2), -550, 250),
                                             -400, 200),
                  m.clamp01(material, m.multiply(material, intensity, None, -550, 400, const_b=0.3), -400, 400), -200, 100)
    m.connect_property(tint, unreal.MaterialProperty.MP_EMISSIVE_COLOR)


MATERIALS = [
    ("M_Vfx_Smoke", _build_smoke, True),
    ("M_Vfx_Glow", _build_glow, True),
    ("M_Vfx_Beam", _build_beam, True),
    ("M_Vfx_Snow", _build_snow, True),
    ("M_Vfx_Putty", _build_putty, True),
    ("M_Decal_Scorch", _build_scorch, False),
    ("M_PP_EmpAberration", _build_aberration, False),
]


def ensure_vfx_material(name, build_fn, niagara):
    full = c.asset_path(MATERIALS_PATH, name)
    import inspect  # noqa: PLC0415
    helpers = "".join(inspect.getsource(fn) for fn in (_uv, _centre_distance, _soft_dot, _particle_color, _rgb, _alpha,
                                                       _setup, _scene_sample))
    wanted = _hash(inspect.getsource(build_fn) + helpers + str(niagara))
    existing = c.load_or_none(full)
    if existing is not None and _tag_of(existing) == wanted:
        c.log("exists", full)
        return existing
    material = m.ensure_material(full, build_fn, rebuild=existing is not None)
    if material is None:
        return None
    if niagara:
        for flag in NIAGARA_USAGE:
            try:
                material.set_editor_property(flag, True)
            except Exception as exc:  # noqa: BLE001
                c.log_error("{0} {1}".format(flag, full), exc)
        unreal.MaterialEditingLibrary.recompile_material(material)
    unreal.EditorAssetLibrary.set_metadata_tag(material, HASH_TAG, wanted)
    c.save(material)
    return material


# --------------------------------------------------------------------------------------
# recipe helpers
# --------------------------------------------------------------------------------------


def f(value):
    return "{0:.4f}".format(float(value))


def vec(x, y, z):
    return "(X={0},Y={1},Z={2})".format(f(x), f(y), f(z))


def vec2(x, y):
    return "(X={0},Y={1})".format(f(x), f(y))


def col(r, g, b, a=1.0):
    return "(R={0},G={1},B={2},A={3})".format(f(r), f(g), f(b), f(a))


def curve(*points):
    """di: value for a float curve data interface: (time, value) pairs."""
    keys = ",".join("(Time={0},Value={1})".format(f(t), f(v)) for t, v in points)
    return "di:Curve=(Keys=({0}))".format(keys)


class Emitter(object):
    """One emitter of a system: a template to copy and the edits to make on the copy."""

    def __init__(self, name, template):
        self.name = name
        self.template = TEMPLATES + template + "." + template
        self.ops = []

    def i(self, module, name, value):
        self.ops.append(("in", module, name, str(value)))
        return self

    def mod(self, stage, script):
        self.ops.append(("mod", stage, MODULES + script + "." + script.rsplit("/", 1)[-1]))
        return self

    def off(self, module):
        self.ops.append(("off", module))
        return self

    def em(self, prop, value):
        self.ops.append(("em", prop, str(value)))
        return self

    def r(self, index, prop, value):
        self.ops.append(("r", index, prop, str(value)))
        return self

    def ren(self, cls_name):
        self.ops.append(("ren", cls_name))
        return self

    # common edits ----------------------------------------------------------------------

    def burst(self, count, time=0.0):
        return self.i("Spawn Burst Instantaneous", "Spawn Count", int(count)).i(
            "Spawn Burst Instantaneous", "Spawn Time", time)

    def life(self, lo, hi):
        return self.i("Initialize Particle", "Lifetime Min", lo).i("Initialize Particle", "Lifetime Max", hi)

    def color(self, r, g, b, a=1.0):
        return self.i("Initialize Particle", "Color", col(r, g, b, a))

    def size(self, lo, hi):
        return self.i("Initialize Particle", "Sprite Size Mode", "Random Uniform").i(
            "Initialize Particle", "Uniform Sprite Size Min", lo).i("Initialize Particle", "Uniform Sprite Size Max", hi)

    def streak(self, w_lo, l_lo, w_hi, l_hi):
        """Non-uniform sprites (width, length) for velocity-aligned sparks."""
        return self.i("Initialize Particle", "Sprite Size Mode", "Random Non-Uniform").i(
            "Initialize Particle", "Sprite Size Min", vec2(w_lo, l_lo)).i(
            "Initialize Particle", "Sprite Size Max", vec2(w_hi, l_hi))

    def speed(self, lo, hi):
        return self.i("Add Velocity", "Velocity Speed>Minimum", lo).i("Add Velocity", "Velocity Speed>Maximum", hi)

    def gravity(self, x, y, z):
        return self.i("Gravity Force", "Gravity", vec(x, y, z))

    def drag(self, value):
        return self.i("Drag", "Drag", value)

    def drag_range(self, value):
        """DirectionalBurst's drag is a random range."""
        return self.i("Drag", "Drag>Minimum", value).i("Drag", "Drag>Maximum", value)

    def fade(self, *points):
        return self.i("Scale Color", "Scale Alpha>FloatCurve", curve(*points))

    def grow(self, *points):
        return self.i("Scale Sprite Size", "Scale Factor>Value>FloatCurve", curve(*points))

    def limit(self, speed):
        return self.i("Solve Forces and Velocity", "Speed Limit", speed)

    def material(self, name, index=0):
        return self.r(index, "Material", _mat(name))

    def no_shadow(self, index=0):
        return self.r(index, "bCastShadows", "False")

    def once(self, duration):
        return self.i("Emitter State", "Loop Behavior", "Once").i("Emitter State", "Loop Duration", duration)

    def cone(self, x, y, z, angle):
        return self.i("Add Velocity", "Cone Axis", vec(x, y, z)).i("Add Velocity", "Cone Angle", angle)


def burst_sprites(name, material, count, life, color, size, radius, speed, gravity=(0.0, 0.0, 0.0), drag=1.0,
                  fade=((0.0, 1.0), (1.0, 0.0)), grow=((0.0, 1.0), (1.0, 1.0)), delay=0.0):
    """OmnidirectionalBurst: a burst from a sphere, flying out from the centre."""
    e = Emitter(name, "OmnidirectionalBurst")
    e.burst(count, delay).life(*life).color(*color).size(*size)
    e.i("Shape Location", "Sphere Radius", radius).speed(*speed)
    e.i("Add Velocity", "Origin Offset", vec(0, 0, 0))
    e.gravity(*gravity).drag(drag).fade(*fade).grow(*grow).limit(20000)
    e.once(life[1] + delay + 0.05)
    return e.material(material).no_shadow()


def spark_streaks(name, count, life, color, streak, axis, angle, speed, gravity=-980.0, drag=0.5, delay=0.0):
    """DirectionalBurst: velocity-aligned streaks in a cone about axis (local space of the system)."""
    e = Emitter(name, "DirectionalBurst")
    e.burst(count, delay).life(*life).color(*color).streak(*streak)
    e.cone(axis[0], axis[1], axis[2], angle).speed(*speed)
    e.gravity(0.0, 0.0, gravity).drag_range(drag).fade((0.0, 1.0), (0.7, 0.8), (1.0, 0.0)).limit(20000)
    e.once(life[1] + delay + 0.05)
    return e.material("M_Vfx_Glow").no_shadow()


def flash(name, life, color, size, delay=0.0, direct_life=False):
    """SimpleSpriteBurst: one sprite that blinks and fades.

    direct_life: set the lifetime the way the template reads it (Lifetime Mode Direct Set). Without it the
    template keeps its own lifetime and Lifetime Min/Max go unread: NS_MeleeSpark's 0.08 s core was still
    on screen 1.5 s later (knockdown_dust.png, 2026-09-29). Every short flash a shot checks sets it (the
    melee, arrow hit, parry, muzzle, anchor, EMP and explosion flashes).
    """
    e = Emitter(name, "SimpleSpriteBurst")
    e.burst(1, delay).life(life, life).color(*color).size(size, size)
    if direct_life:
        e.i("Initialize Particle", "Lifetime Mode", "Direct Set").i("Initialize Particle", "Lifetime", life)
    e.fade((0.0, 1.0), (1.0, 0.0)).once(life + delay + 0.05)
    return e.material("M_Vfx_Glow").no_shadow()


def ribbon_trail(name, width, life, color, rate):
    """A world-space ribbon that the moving system draws behind it: the Fountain template with its
    motion modules off, a ribbon renderer in place of the sprites, interpolated spawning so a fast
    arrow still leaves an even line."""
    e = Emitter(name, "Fountain")
    for module in ("Shape Location", "Add Velocity", "Gravity Force", "Drag"):
        e.off(module)
    e.i("Spawn Rate", "SpawnRate", rate).life(life, life).color(*color)
    e.i("Initialize Particle", "Ribbon Width Mode", "Direct Set").i("Initialize Particle", "Ribbon Width", width)
    e.fade((0.0, 1.0), (1.0, 0.0))
    e.em("InterpolatedSpawnMode", "Interpolation")
    e.r(0, "bIsEnabled", "False")
    e.ren("NiagaraRibbonRendererProperties").material("M_Vfx_Beam", index=1)
    return e


def beam(name, width, color, life, segments=16):
    """DynamicBeam: a beam between User.BeamStart and User.BeamEnd, both world positions."""
    e = Emitter(name, "DynamicBeam")
    e.i("Beam Emitter Setup", "Beam Start", "=User.BeamStart").i("Beam Emitter Setup", "Absolute Beam Start", "True")
    e.i("Beam Emitter Setup", "Beam End", "=User.BeamEnd").i("Beam Emitter Setup", "Absolute Beam End", "True")
    e.i("Beam Emitter Setup", "Use Beam Tangents", "False")
    e.burst(segments).once(life + 0.05)
    e.i("Initialize Particle", "Lifetime Mode", "Direct Set").i("Initialize Particle", "Lifetime", life)
    e.i("Beam Width", "Beam Width>FloatCurve", curve((0.0, 1.0), (1.0, 1.0))).i("Beam Width", "Beam Width>Scale Curve", width)
    e.i("Color", "Color", col(*color)).i("Color", "Scale Alpha>FloatCurve", curve((0.0, 1.0), (0.8, 1.0), (1.0, 0.0)))
    return e.material("M_Vfx_Beam")


def smoke_stream(name, rate, life, color, size, radius, speed, cone_angle, drift, grow, fade, loop=None):
    """OmnidirectionalBurst turned into a stream: its burst off, a Spawn Rate module in its place, the
    velocity a cone about +Z; soft lit smoke rising and drifting."""
    e = Emitter(name, "OmnidirectionalBurst")
    e.off("Spawn Burst Instantaneous").mod("EmitterUpdate", "Emitter/SpawnRate").i("Spawn Rate", "SpawnRate", rate)
    e.life(*life).color(*color).size(*size)
    e.i("Shape Location", "Sphere Radius", radius)
    e.i("Add Velocity", "Velocity Mode", "In Cone").speed(*speed).cone(0, 0, 1, cone_angle)
    e.i("Add Velocity", "Origin Offset", vec(0, 0, 0))
    e.gravity(*drift).drag(0.6).fade(*fade).grow(*grow).limit(20000)
    if loop is not None:
        e.once(loop)
    return e.material("M_Vfx_Smoke").no_shadow()


WHITE_SNOW = (0.85, 0.88, 0.95)

EFFECTS = {
    # The smoke arrow: a billow that fills 500 cm at once, then keeps coming for six seconds, drifting.
    "NS_SmokeCloud": [
        burst_sprites("Billow", "M_Vfx_Smoke", 26, (3.0, 4.5), (0.34, 0.35, 0.38, 0.75), (220, 340), 260,
                      (40, 160), gravity=(12, 6, 10), drag=1.4, fade=((0.0, 0.0), (0.08, 1.0), (0.7, 0.8), (1.0, 0.0)),
                      grow=((0.0, 0.6), (0.3, 1.1), (1.0, 1.5))),
        smoke_stream("Stream", 12, (2.5, 3.5), (0.32, 0.33, 0.36, 0.6), (200, 320), 300, (20, 60), 60,
                     (14, 7, 18), ((0.0, 0.8), (1.0, 1.7)), ((0.0, 0.0), (0.15, 1.0), (0.7, 0.7), (1.0, 0.0)), loop=5.5),
    ],
    # The EMP: a ring of blue light racing out to 600 cm, a flash, electric sparks.
    "NS_EmpPulse": [
        burst_sprites("Ring", "M_Vfx_Glow", 160, (0.55, 0.7), (0.3, 0.7, 1.5, 1.0), (30, 50), 25, (1350, 1450),
                      drag=1.8, fade=((0.0, 1.0), (0.6, 0.8), (1.0, 0.0)), grow=((0.0, 0.6), (1.0, 1.4)))
        .i("Shape Location", "Non Uniform Scale", vec(1, 1, 0.02)),
        flash("Flash", 0.14, (0.5, 1.0, 2.2, 1.0), 300, direct_life=True),
        spark_streaks("Sparks", 70, (0.15, 0.45), (1.5, 3.2, 6.0, 1.0), (2.0, 14.0, 3.0, 40.0), (0, 0, 1), 85,
                      (400, 1100), gravity=-400, drag=2.0),
        spark_streaks("Arcs", 40, (0.08, 0.2), (2.5, 4.0, 8.0, 1.0), (1.5, 30.0, 2.5, 70.0), (0, 0, 1), 90,
                      (1500, 2600), gravity=0, drag=4.0, delay=0.12),
    ],
    # The explosive: fireball, a hot core flash, sparks, and a smoke puff that outlives it.
    "NS_Explosion": [
        burst_sprites("Fireball", "M_Vfx_Glow", 22, (0.35, 0.75), (1.1, 0.4, 0.08, 1.0), (70, 140), 50, (200, 560),
                      gravity=(0, 0, 250), drag=3.0, fade=((0.0, 1.0), (0.4, 0.7), (1.0, 0.0)),
                      grow=((0.0, 0.5), (0.25, 1.3), (1.0, 1.7))),
        flash("Core", 0.1, (1.6, 1.1, 0.5, 1.0), 260, direct_life=True),
        spark_streaks("Sparks", 60, (0.5, 1.3), (3.0, 1.4, 0.35, 1.0), (2.0, 10.0, 3.5, 30.0), (0, 0, 1), 75,
                      (700, 1700), gravity=-980, drag=0.6),
        burst_sprites("Smoke", "M_Vfx_Smoke", 18, (2.2, 3.6), (0.09, 0.085, 0.08, 0.85), (150, 260), 90, (60, 220),
                      gravity=(10, 5, 60), drag=1.5, fade=((0.0, 0.0), (0.1, 1.0), (0.6, 0.6), (1.0, 0.0)),
                      grow=((0.0, 0.7), (1.0, 2.2)), delay=0.08),
    ],
    # The putty arrow: purple blobs thrown off the impact.
    "NS_PuttySplat": [
        burst_sprites("Blobs", "M_Vfx_Putty", 24, (0.45, 0.9), (0.42, 0.07, 0.75, 1.0), (7, 18), 8, (150, 420),
                      gravity=(0, 0, -980), drag=0.8, fade=((0.0, 1.0), (0.8, 1.0), (1.0, 0.0)),
                      grow=((0.0, 1.0), (0.2, 1.3), (1.0, 0.8))),
        burst_sprites("Splat", "M_Vfx_Putty", 5, (0.25, 0.35), (0.5, 0.1, 0.85, 0.9), (26, 40), 4, (10, 40),
                      fade=((0.0, 1.0), (1.0, 0.0)), grow=((0.0, 0.6), (1.0, 1.4))),
    ],
    # The bola in flight: a warm ribbon and a fuzz of sparks around its line.
    "NS_BolaTrail": [
        ribbon_trail("Ribbon", 7.0, 0.3, (1.6, 0.8, 0.25, 0.8), 260),
        smoke_stream("Whirl", 90, (0.2, 0.35), (2.0, 1.1, 0.4, 1.0), (4, 7), 16, (5, 20), 90, (0, 0, 0),
                     ((0.0, 1.0), (1.0, 0.3)), ((0.0, 1.0), (1.0, 0.0))).material("M_Vfx_Glow")
        .em("InterpolatedSpawnMode", "Interpolation"),
    ],
    # Every arrow in flight: a thin, faint streak.
    "NS_ArrowTrail": [
        ribbon_trail("Ribbon", 1.6, 0.18, (0.55, 0.45, 0.9, 0.35), 300),
    ],
    # Arrow impacts by surface, out along the surface normal (the system's X).
    "NS_ArrowImpact_Stone": [
        spark_streaks("Chips", 10, (0.25, 0.5), (0.22, 0.21, 0.2, 1.0), (1.5, 2.5, 3.0, 5.0), (1, 0, 0), 55,
                      (250, 650)).material("M_Vfx_Smoke"),
        burst_sprites("Dust", "M_Vfx_Smoke", 9, (0.6, 1.1), (0.3, 0.29, 0.28, 0.6), (14, 26), 4, (30, 120),
                      gravity=(0, 0, -40), drag=3.0, fade=((0.0, 1.0), (1.0, 0.0)), grow=((0.0, 0.8), (1.0, 2.2))),
    ],
    "NS_ArrowImpact_Wood": [
        spark_streaks("Splinters", 9, (0.3, 0.55), (0.3, 0.2, 0.1, 1.0), (1.2, 4.0, 2.2, 8.0), (1, 0, 0), 50,
                      (250, 600)).material("M_Vfx_Smoke"),
        burst_sprites("Dust", "M_Vfx_Smoke", 6, (0.5, 0.9), (0.32, 0.24, 0.15, 0.5), (10, 20), 3, (30, 90),
                      gravity=(0, 0, -40), drag=3.0, fade=((0.0, 1.0), (1.0, 0.0)), grow=((0.0, 0.8), (1.0, 2.0))),
    ],
    # A parry: a purple ring flashing out across the line between them (the system's X), a flash and a
    # few sparks. Small and fast, so it reads as a clash, not an explosion.
    "NS_ParryRing": [
        burst_sprites("Ring", "M_Vfx_Glow", 90, (0.22, 0.3), (1.6, 0.5, 3.2, 1.0), (10, 16), 8, (480, 520),
                      drag=2.5, fade=((0.0, 1.0), (0.5, 0.8), (1.0, 0.0)), grow=((0.0, 0.7), (1.0, 1.3)))
        .i("Shape Location", "Non Uniform Scale", vec(0.02, 1, 1)),
        flash("Flash", 0.08, (1.4, 0.55, 2.8, 1.0), 60, direct_life=True),
        spark_streaks("Sparks", 18, (0.1, 0.25), (2.5, 1.4, 5.0, 1.0), (1.5, 10.0, 2.5, 24.0), (1, 0, 0), 80,
                      (500, 1000), gravity=-300, drag=3.0),
    ],
    # An arrow into a thug: no blood, a purple-white comic spark.
    "NS_HitSpark": [
        flash("Star", 0.2, (2.2, 0.9, 4.5, 1.0), 120, direct_life=True),
        spark_streaks("Streaks", 22, (0.15, 0.3), (2.5, 1.6, 5.0, 1.0), (4.0, 20.0, 6.0, 44.0), (1, 0, 0), 75,
                      (500, 1100), gravity=0, drag=4.0),
    ],
    # Kate's fist or foot landing (2026-09-29, combat readability): small, very bright and very short, at the
    # contact point and out toward her (the system's X). The thug's own hit flash is what reads; this only
    # marks where. A core, not the arrow spark's 120 cm star. Hot gold, not white (2026-09-29, hit_spark.png:
    # a white-violet 26 cm core vanished into his white hit flash): gold reads on the flash, the red suit
    # and the snow, and stays clear of Kate's purple and the telegraph's red-orange. The core is blended, not
    # added, so its gold survives on his white flash (an added glow there only clips to white); the glow and
    # the streaks round it stay additive.
    "NS_MeleeSpark": [
        flash("Core", 0.08, (1.0, 0.62, 0.06, 0.95), 26, direct_life=True).material("M_Vfx_Putty"),
        flash("Glow", 0.08, (6.0, 3.2, 0.4, 1.0), 46, direct_life=True),
        spark_streaks("Streaks", 14, (0.08, 0.16), (8.0, 4.2, 0.6, 1.0), (1.5, 7.0, 2.5, 16.0), (1, 0, 0), 75,
                      (450, 900), gravity=0, drag=5.0),
    ],
    # The bow string's release: a tiny puff of breath-like haze.
    "NS_BowRelease": [
        burst_sprites("Puff", "M_Vfx_Smoke", 5, (0.25, 0.4), (0.7, 0.7, 0.75, 0.25), (4, 8), 2, (20, 60),
                      drag=4.0, fade=((0.0, 1.0), (1.0, 0.0)), grow=((0.0, 1.0), (1.0, 2.5))),
    ],
    # The grapple line from Kate's hand to the anchor while she zips (positions set every frame).
    "NS_ZipLine": [
        beam("Line", 2.2, (0.9, 0.75, 1.6, 0.9), 600.0),
    ],
    # Sparks where the grapple arrow bites into the parapet.
    "NS_AnchorSparks": [
        flash("Bite", 0.12, (9.0, 5.5, 1.8, 1.0), 120, direct_life=True),
        spark_streaks("Sparks", 22, (0.2, 0.5), (5.0, 3.0, 1.0, 1.0), (1.5, 6.0, 2.5, 16.0), (1, 0, 0), 60,
                      (300, 900), gravity=-980, drag=0.5),
    ],
    # A gunner's shot: flash and sparks at the muzzle (the system's X is the shot).
    "NS_MuzzleFlash": [
        flash("Flash", 0.05, (6.0, 4.2, 1.6, 1.0), 45, direct_life=True),
        spark_streaks("Sparks", 8, (0.04, 0.1), (5.0, 3.5, 1.2, 1.0), (1.5, 8.0, 2.5, 18.0), (1, 0, 0), 18,
                      (900, 1600), gravity=0, drag=2.0),
        burst_sprites("Smoke", "M_Vfx_Smoke", 3, (0.4, 0.6), (0.5, 0.5, 0.52, 0.25), (6, 10), 2, (20, 50),
                      drag=4.0, fade=((0.0, 1.0), (1.0, 0.0)), grow=((0.0, 1.0), (1.0, 3.0))),
    ],
    # The bullet's line, muzzle to hit, for a few frames.
    "NS_Tracer": [
        beam("Tracer", 1.2, (5.0, 4.0, 2.0, 0.8), 0.06, segments=4),
    ],
    # Kate's feet in the snow: a small kick behind each step (the system's X points back and up).
    "NS_FootstepSnow": [
        spark_streaks("Kick", 16, (0.4, 0.7), WHITE_SNOW + (0.9,), (8.0, 8.0, 14.0, 14.0), (1, 0, 0), 35,
                      (90, 240), gravity=-700, drag=1.0).material("M_Vfx_Snow"),
    ],
    # A landing: a ring of snow thrown out round her feet; the game scales it by the fall height.
    "NS_LandingSnow": [
        burst_sprites("Ring", "M_Vfx_Smoke", 22, (0.5, 0.9), WHITE_SNOW + (0.7,), (12, 26), 30, (150, 380),
                      gravity=(0, 0, -300), drag=3.0, fade=((0.0, 1.0), (1.0, 0.0)), grow=((0.0, 0.8), (1.0, 2.4)))
        .i("Shape Location", "Non Uniform Scale", vec(1, 1, 0.05)).i("Add Velocity", "Origin Offset", vec(0, 0, -12)),
    ],
    # A thug's body meeting the ground (2026-09-29, knockdown_dust.png: NS_LandingSnow's white puff was
    # invisible on the white street): shadowed grey-blue slush thrown up and out round him, and a few clods.
    # M_Vfx_Snow, not M_Vfx_Smoke: the smoke fades out within 60 cm of geometry (its depth fade), so a puff
    # on the street hardly draws at all.
    "NS_KnockdownDust": [
        burst_sprites("Slush", "M_Vfx_Snow", 40, (0.6, 1.0), (0.05, 0.055, 0.07, 1.0), (28, 50), 40, (260, 560),
                      gravity=(0, 0, -300), drag=3.0, fade=((0.0, 1.0), (1.0, 0.0)), grow=((0.0, 0.8), (1.0, 2.2)))
        .i("Shape Location", "Non Uniform Scale", vec(1, 1, 0.05)).i("Add Velocity", "Origin Offset", vec(0, 0, -45)),
        spark_streaks("Clods", 10, (0.4, 0.7), (0.08, 0.09, 0.11, 1.0), (5.0, 5.0, 9.0, 9.0), (0, 0, 1), 55,
                      (220, 420), gravity=-900, drag=1.0).material("M_Vfx_Snow"),
    ],
    # Light snow round the camera: 800 flakes recycled into view, drifting down in a light wind.
    "NS_Snowfall": [
        (Emitter("Flakes", "RecycleParticlesInView")
         .burst(800).life(30, 60).color(*(WHITE_SNOW + (0.9,))).size(5.0, 9.0)
         .i("View Recycler", "Near / Far Distance", vec2(120, 1400))
         .i("View Recycler", "Recycle Velocity", vec(0, 0, -70))
         .i("Wind Force", "Wind Speed", vec(45, 20, 0))
         .i("Wind Force", "Use Depth Buffer", "False")
         .gravity(0, 0, -160)
         .em("CalculateBoundsMode", "Fixed")
         .em("FixedBounds", "(Min=(X=-3500,Y=-3500,Z=-2000),Max=(X=3500,Y=3500,Z=2000),IsValid=True)")
         .material("M_Vfx_Snow").no_shadow()),
    ],
    # Kate landing in the dumpster of wrapping paper (Act 1, Night Shift's botched grapple, 2026-11-10): scraps of
    # red, gold and green thrown up out of the bin, tumbling back down slowly. Blended (M_Vfx_Putty), not added,
    # so the colours read against the night instead of clipping to white.
    "NS_PaperBurst": [
        burst_sprites("Red", "M_Vfx_Putty", 46, (1.6, 2.4), (0.8, 0.05, 0.06, 1.0), (16, 28), 50, (420, 760),
                      gravity=(0, 0, -520), drag=2.2, fade=((0.0, 1.0), (0.8, 1.0), (1.0, 0.0))),
        burst_sprites("Gold", "M_Vfx_Putty", 36, (1.6, 2.4), (1.0, 0.72, 0.12, 1.0), (14, 24), 50, (420, 760),
                      gravity=(0, 0, -520), drag=2.2, fade=((0.0, 1.0), (0.8, 1.0), (1.0, 0.0))),
        burst_sprites("Green", "M_Vfx_Putty", 36, (1.6, 2.4), (0.08, 0.55, 0.18, 1.0), (16, 28), 50, (420, 760),
                      gravity=(0, 0, -520), drag=2.2, fade=((0.0, 1.0), (0.8, 1.0), (1.0, 0.0))),
    ],
    # A chimney's wisp: a thin, slow, pale stream that leans with the wind.
    "NS_ChimneyWisp": [
        smoke_stream("Wisp", 5, (4.0, 6.0), (0.4, 0.4, 0.43, 0.45), (40, 70), 12, (40, 80), 10, (18, 6, 14),
                     ((0.0, 0.8), (1.0, 3.2)), ((0.0, 0.0), (0.15, 1.0), (0.6, 0.6), (1.0, 0.0))),
    ],
}


# --------------------------------------------------------------------------------------
# building
# --------------------------------------------------------------------------------------


def _recipe_text(emitters):
    lines = []
    for e in emitters:
        lines.append("{0} {1}".format(e.name, e.template))
        lines.extend(repr(op) for op in e.ops)
    return "\n".join(lines)


def _apply(builder, system, e):
    ok = True
    for op in e.ops:
        kind = op[0]
        if kind == "in":
            ok &= builder.set_input(system, e.name, op[1], op[2], op[3])
        elif kind == "mod":
            ok &= builder.add_module(system, e.name, op[1], op[2])
        elif kind == "off":
            ok &= builder.set_module_enabled(system, e.name, op[1], False)
        elif kind == "em":
            ok &= builder.set_emitter_property(system, e.name, op[1], op[2])
        elif kind == "r":
            ok &= builder.set_renderer_property(system, e.name, op[1], op[2], op[3])
        elif kind == "ren":
            ok &= builder.add_renderer(system, e.name, "/Script/Niagara." + op[1]) >= 0
    return ok


def ensure_system(name, emitters):
    full = c.asset_path(VFX_PATH, name)
    builder = getattr(unreal, "HawkeyeVfxBuilder", None)
    if builder is None:
        c.log("FAILED", full, "UHawkeyeVfxBuilder not exposed; build the module")
        return None
    wanted = _hash(_recipe_text(emitters))
    existing = c.load_or_none(full)
    if existing is not None and _tag_of(existing) == wanted:
        c.log("exists", full, "{0} emitters".format(len(builder.get_emitter_names(existing))))
        return existing
    c.ensure_directory(VFX_PATH)
    system = builder.create_system(full)
    if system is None:
        c.log("FAILED", full, "no system")
        return None
    ok = True
    for e in emitters:
        if not builder.add_emitter(system, e.template, e.name):
            ok = False
            continue
    for e in emitters:
        ok &= _apply(builder, system, e)
    compiled = builder.finish_system(system)
    builder.end_editing()
    if not ok or not compiled:
        c.log("FAILED", full, "an edit or the compile failed; see LogHawkeye above")
        return None
    unreal.EditorAssetLibrary.set_metadata_tag(system, HASH_TAG, wanted)
    c.save(system)
    c.log("updated" if existing is not None else "created", full,
          "{0} emitters: {1}".format(len(emitters), ", ".join(e.name for e in emitters)))
    return system


def system_path(name):
    """The object path the Blueprints and data assets point at."""
    return c.object_path(VFX_PATH, name)


def run():
    c.ensure_directory(MATERIALS_PATH)
    for name, build_fn, niagara in MATERIALS:
        ensure_vfx_material(name, build_fn, niagara)
    built = {}
    for name in sorted(EFFECTS):
        built[name] = ensure_system(name, EFFECTS[name])
    return built


if __name__ == "__main__":
    run()
    c.print_summary("vfx")
