"""The block look: procedural facades, windows with rooms behind them, and the winter ground (claude-docs/city-look.md).

Every surface here is one HLSL Custom expression fed by world position, the vertex normal and the camera vector, so
there are no textures to import and nothing to download: the graphs are built by script, like the rest of
``_materials.py``, and the same script always builds the same graph.

* ``FACADE_HLSL``: masonry at real size (brick 20 x 6.5 cm in 1 cm mortar, running bond with a header course every
  sixth; brownstone ashlar; board-marked concrete panels), filtered against the pixel footprint so it fades to its
  average instead of shimmering; per-building colour; grime at the base, a salt line, soot under the cornice and
  streaks under the sills; stone lintels and sills with snow on them; recessed double-hung windows whose reveal,
  sash, glass and the room behind are ray-cast in the shader (interior mapping); air conditioners; storefronts with
  lit shops or roll-down gates; sign bands; a cornice with dentils. The window grid is the one the dressing's lit
  window cards sit on (160 cm bays, 330 cm storeys, sill 95, 110 x 150).
* ``ROAD_HLSL`` / ``SIDEWALK_HLSL`` / ``PARK_HLSL``: wet asphalt with aggregate, patches and cracks, two wheel tracks
  per lane worn through compacted snow, slush and a plowed ridge at the kerb, a double yellow line on the avenues;
  concrete flags 5 ft square with joints, a granite kerb with a steel edge, a trodden path, salt and wet patches;
  park snow with drifts, wind ripples and the odd patch of grass. Roads and sidewalks read their street's frame
  (direction, centre line, half width) from custom primitive data the generator writes on each actor.

Plain Python except the builders at the bottom, which need ``unreal`` (and ``_materials``).
"""

# --------------------------------------------------------------------------------------------------------------------
# shared HLSL: hashes, value noise, and the box-filtered joint (Inigo Quilez, "filtering procedural textures")
# --------------------------------------------------------------------------------------------------------------------

LIB_HLSL = r"""
struct FLook
{
    // lowbias32 (Chris Wellons) and the key every facade and bay choice hashes: _facade_plan.ih / hf / key, exactly.
    uint IH(uint x)
    {
        x ^= x >> 16;
        x *= 0x7feb352du;
        x ^= x >> 15;
        x *= 0x846ca68bu;
        x ^= x >> 16;
        return x;
    }
    float HF(uint x)
    {
        return (float)(IH(x) >> 8) * (1.0 / 16777216.0);
    }
    uint Key(uint seed, uint face, uint seg, uint bay, uint salt)
    {
        return seed * 1031u + face * 7u + seg * 131071u + bay * 524287u + salt * 8191u;
    }
    // A rectangular recess |x| < hw, 0 < y < h, d deep, seen along r (x across, y up, z in) from p on the wall plane.
    // Returns 0 a jamb, 1 the head, 2 the floor, 3 the back; hit is (x, y, depth).
    int Recess(float2 p, float3 r, float hw, float h, float d, out float3 hit)
    {
        float tx = r.x > 0.0 ? (hw - p.x) / r.x : (r.x < 0.0 ? (-hw - p.x) / r.x : 1e6);
        float ty = r.y > 0.0 ? (h - p.y) / r.y : (r.y < 0.0 ? -p.y / r.y : 1e6);
        float tz = d / max(r.z, 1e-3);
        float t = min(tx, min(ty, tz));
        hit = float3(p.x + r.x * t, p.y + r.y * t, r.z * t);
        return t == tz ? 3 : (t == tx ? 0 : (r.y > 0.0 ? 1 : 2));
    }
    // Entry distance of the ray o + t r into the box [a, b], or 1e6 when it misses.
    float Box(float3 o, float3 r, float3 a, float3 b)
    {
        float3 inv = 1.0 / float3(abs(r.x) > 1e-4 ? r.x : 1e-4, abs(r.y) > 1e-4 ? r.y : 1e-4, abs(r.z) > 1e-4 ? r.z : 1e-4);
        float3 t0 = (a - o) * inv;
        float3 t1 = (b - o) * inv;
        float3 lo = min(t0, t1);
        float3 hi = max(t0, t1);
        float tn = max(max(lo.x, lo.y), lo.z);
        float tf = min(min(hi.x, hi.y), hi.z);
        return (tn <= tf && tf > 0.0) ? max(tn, 0.0) : 1e6;
    }
    float H21(float2 p)
    {
        float3 q = frac(float3(p.x, p.y, p.x) * 0.1031);
        q += dot(q, q.yzx + 33.33);
        return frac((q.x + q.y) * q.z);
    }
    float VN(float2 p)
    {
        float2 i = floor(p);
        float2 f = frac(p);
        f = f * f * (3.0 - 2.0 * f);
        return lerp(lerp(H21(i), H21(i + float2(1.0, 0.0)), f.x), lerp(H21(i + float2(0.0, 1.0)), H21(i + float2(1.0, 1.0)), f.x), f.y);
    }
    float FBM(float2 p)
    {
        return VN(p) * 0.5 + VN(p * 2.03 + 17.1) * 0.3 + VN(p * 4.07 + 3.7) * 0.2;
    }
    // Share of the pixel footprint [x - w/2, x + w/2] (cell units) that falls in the joint [0, m) of each cell.
    float Joint(float x, float w, float m)
    {
        w = max(w, 1e-4);
        float a = x - 0.5 * w;
        float b = x + 0.5 * w;
        float fa = floor(a) * m + min(frac(a), m);
        float fb = floor(b) * m + min(frac(b), m);
        return saturate((fb - fa) / w);
    }
    // Interior mapping: a box room behind a pane. g is the point on the glass (x across from the window's middle, y up
    // from its bottom), r the view ray (x across, y up, z into the wall). Returns the albedo of what the ray meets;
    // Light is how much of the room's lamp reaches it.
    float3 Room(float2 g, float3 r, float W, float Off, float Floor, float Ceil, float D, float Seed, float Shop, out float Light)
    {
        float qx = g.x - Off;
        float tx = r.x > 0.0 ? (0.5 * W - qx) / r.x : (r.x < 0.0 ? (-0.5 * W - qx) / r.x : 1e6);
        float ty = r.y > 0.0 ? (Ceil - g.y) / r.y : (r.y < 0.0 ? (Floor - g.y) / r.y : 1e6);
        float tz = D / max(r.z, 1e-3);
        float t = min(tx, min(ty, tz));
        float3 h = float3(qx + r.x * t, g.y + r.y * t, r.z * t);
        float hp = H21(float2(Seed, 4.7));
        float3 paint = lerp(float3(0.62, 0.55, 0.42), float3(0.42, 0.50, 0.52), step(0.55, hp));
        paint = lerp(paint, float3(0.60, 0.40, 0.30), step(0.80, hp));
        paint = lerp(paint, float3(0.70, 0.68, 0.62), step(0.92, hp));
        float3 c = paint;
        if (t == tz)
        {
            float fx = (H21(float2(Seed, 9.1)) - 0.5) * max(W - 200.0, 0.0);
            float fh = lerp(70.0, 115.0, H21(float2(Seed, 2.2)));
            float3 furn = lerp(float3(0.10, 0.06, 0.045), float3(0.26, 0.22, 0.20), H21(float2(Seed, 5.5)));
            if (abs(h.x - fx) < 85.0 && h.y < Floor + fh)
            {
                c = furn * (h.y < Floor + fh - 12.0 ? 0.8 : 1.0);
            }
            float px = fx + (H21(float2(Seed, 1.3)) - 0.5) * 70.0;
            if (abs(h.x - px) < 32.0 && abs(h.y - (Floor + 160.0)) < 24.0)
            {
                bool inner = abs(h.x - px) < 28.0 && abs(h.y - (Floor + 160.0)) < 20.0;
                c = inner ? lerp(float3(0.15, 0.25, 0.35), float3(0.6, 0.35, 0.15), H21(float2(Seed, 8.8))) : float3(0.05, 0.04, 0.03);
            }
            if (Shop > 0.5)
            {
                float rowv = (h.y - Floor) / 42.0;
                float item = H21(float2(floor(h.x / 6.0), floor(rowv)) + Seed * 31.0);
                float3 goods = lerp(float3(0.42, 0.36, 0.30), float3(0.30, 0.34, 0.40), item);
                goods = lerp(goods, float3(0.55, 0.22, 0.15), step(0.85, item));
                goods = lerp(goods, float3(0.62, 0.58, 0.40), step(0.93, item));
                goods *= (frac(h.x / 6.0) < 0.12 || frac(rowv) > 0.85) ? 0.35 : 1.0;
                c = rowv < 5.0 ? (frac(rowv) < 0.12 ? float3(0.30, 0.30, 0.30) : goods) : float3(0.75, 0.74, 0.70);
            }
        }
        else if (t == tx)
        {
            c = paint * 0.82;
            if (Shop > 0.5)
            {
                float rowv = (h.y - Floor) / 42.0;
                c = rowv < 5.0 ? (frac(rowv) < 0.12 ? float3(0.30, 0.30, 0.30) : lerp(float3(0.5, 0.45, 0.2), float3(0.2, 0.4, 0.3), VN(float2(h.z / 8.0, floor(rowv))))) : float3(0.70, 0.70, 0.66);
            }
        }
        else if (r.y > 0.0)
        {
            c = float3(0.72, 0.70, 0.64);
        }
        else
        {
            float2 pl = float2(h.x / 12.0, h.z / 90.0 + 0.37 * floor(h.x / 12.0));
            c = lerp(float3(0.20, 0.10, 0.045), float3(0.30, 0.17, 0.08), H21(floor(pl))) * (frac(pl.x) < 0.07 ? 0.6 : 1.0);
            if (Shop > 0.5)
            {
                c = lerp(float3(0.34, 0.34, 0.32), float3(0.48, 0.47, 0.44), step(0.5, frac(0.5 * (floor(h.x / 40.0) + floor(h.z / 40.0)))));
            }
            else if (abs(h.x) < 90.0 && abs(h.z - 0.5 * D) < 0.3 * D)
            {
                c = lerp(float3(0.35, 0.07, 0.05), float3(0.12, 0.16, 0.30), step(0.5, H21(float2(Seed, 6.6))));
            }
        }
        float3 dl = h - float3(0.0, Ceil - 15.0, 0.55 * D);
        Light = Shop > 0.5 ? 1.1 : 0.12 + 2.0 / (1.0 + dot(dl, dl) / 18000.0);
        if (t == ty && r.y > 0.0 && length(h.xz - float2(0.0, 0.55 * D)) < (Shop > 0.5 ? 0.0 : 16.0))
        {
            c = float3(1.0, 0.95, 0.85);
            Light = 5.0;
        }
        return c;
    }
};
FLook L;
"""

# --------------------------------------------------------------------------------------------------------------------
# shops: trades, their signs (signed-distance Roboto lettering from the sign atlas) and their interiors
# --------------------------------------------------------------------------------------------------------------------

# Generic trades only (no brands): (sign, interior kind). Kinds: 0 laundromat, 1 grocery, 2 food, 3 hardware,
# 4 pharmacy, 5 barber or salon, 6 cleaners, tailor or a small shop, 7 flowers.
SHOPS = [
    ("LAUNDROMAT", 0), ("DELI & GROCERY", 1), ("PIZZA", 2), ("HARDWARE", 3), ("PHARMACY", 4), ("BARBER SHOP", 5),
    ("BAKERY", 2), ("DRY CLEANERS", 6), ("LIQUORS", 1), ("NAIL SALON", 5), ("CAFE", 2), ("TAILOR", 6), ("FLOWERS", 7),
    ("DINER", 2), ("WINE & LIQUOR", 1), ("GROCERY", 1), ("BAGELS", 2), ("NOODLES", 2), ("TATTOO", 5), ("THRIFT", 6),
    ("LOCKSMITH", 3), ("SHOE REPAIR", 6), ("BOOKS", 6), ("RECORDS", 6), ("BAR", 2), ("24 HR DELI", 1),
]
SIGN_ATLAS = "T_SignAtlas"           # /Game/City/EastVillage/Look, imported from Tools/Data/look/sign_atlas.png


def shop_hlsl():
    """FShop and the tables the signs read: each trade's glyphs (indices into the sign atlas), the four faces'
    advances, the interiors' kinds."""
    import _sign_font as sf
    chars, starts, lengths = [], [], []
    for name, _kind in SHOPS:
        starts.append(len(chars))
        lengths.append(len(name))
        chars.extend(sf.GLYPHS.index(ch) for ch in name)
    kinds = [kind for _name, kind in SHOPS]

    def arr(kind, name, values, fmt="{0}"):
        return "static const {0} {1}[{2}] = {{{3}}};".format(kind, name, len(values), ", ".join(fmt.format(v) for v in values))

    tables = "\n".join([
        arr("uint", "NAME_CHARS", chars), arr("uint", "NAME_START", starts), arr("uint", "NAME_LEN", lengths),
        arr("uint", "NAME_KIND", kinds),
        arr("float", "ADV", [a for face in sf.ADVANCE for a in face], "{0:.5f}"),
        "static const uint NG = {0}u;".format(len(sf.GLYPHS)),
        "static const float GLYPH_GRID = {0:.1f};".format(sf.GRID),
        "static const float ORIGIN_F = {0:.6f};".format(sf.ORIGIN_X / float(sf.CELL)),
        "static const float CAP_F = {0:.6f};".format(sf.CAP / float(sf.CELL)),
        "static const float BASE_F = {0:.6f};".format(sf.BASELINE / float(sf.CELL)),
        "static const float SDF_PX = {0:.1f};".format(2.0 * sf.SPREAD),
        "static const float CELL_PX = {0:.1f};".format(sf.CELL),
        "static const uint SHOP_NAMES = {0}u;".format(len(SHOPS)),
    ])
    return tables + "\n" + SHOP_STRUCT


SHOP_STRUCT = r"""
struct FShop
{
    FLook L;
    // A shop interior: a box room behind the glass, furnished by kind, with what stands in it (counters, shelf
    // gondolas, a bank of machines, pendant lamps) ray-cast as boxes in front of the walls. Light is the lamp share,
    // Tint the light's colour.
    float3 Room(float2 g, float3 r, float W, float Floor, float Ceil, float D, int kind, float seed, out float Light, out float3 Tint)
    {
        float tx = r.x > 0.0 ? (0.5 * W - g.x) / r.x : (r.x < 0.0 ? (-0.5 * W - g.x) / r.x : 1e6);
        float ty = r.y > 0.0 ? (Ceil - g.y) / r.y : (r.y < 0.0 ? (Floor - g.y) / r.y : 1e6);
        float tz = D / max(r.z, 1e-3);
        float t = min(tx, min(ty, tz));
        float3 h = float3(g.x + r.x * t, g.y + r.y * t, r.z * t);
        float y = h.y - Floor;
        float hv = L.H21(float2(seed, 3.9));
        float3 wall = kind == 0 ? float3(0.62, 0.66, 0.68) : (kind == 2 ? float3(0.55, 0.35, 0.22) : (kind == 4 ? float3(0.70, 0.71, 0.70) : (kind == 5 ? float3(0.50, 0.46, 0.44) : float3(0.52, 0.48, 0.40))));
        wall *= lerp(0.8, 1.1, hv);
        bool fluor = kind == 0 || kind == 1 || kind == 3 || kind == 4 || kind == 6;
        Tint = fluor ? float3(0.88, 0.95, 1.0) : float3(1.0, 0.82, 0.60);
        Light = fluor ? 1.0 : 0.35 + 1.2 / (1.0 + dot(h.xz - float2(0.0, 0.5 * D), h.xz - float2(0.0, 0.5 * D)) / 60000.0);
        float3 c = wall;
        // what lines a wall, by kind, along a coordinate a across it
        float a = (t == tz) ? h.x : h.z;
        float item = L.H21(float2(floor(a / 7.0), floor(y / 40.0)) + seed * 17.0);
        if (t == tz || t == tx)
        {
            if (kind == 0)
            {
                float cx = frac(a / 72.0) * 72.0 - 36.0;
                float rowy = y < 95.0 ? y - 47.0 : y - 145.0;
                if (y < 190.0 && abs(cx) < 33.0)
                {
                    c = length(float2(cx, rowy)) < 20.0 ? (length(float2(cx, rowy)) < 16.0 ? float3(0.06, 0.07, 0.08) : float3(0.35, 0.36, 0.37)) : float3(0.72, 0.72, 0.70);
                }
            }
            else if (kind == 1 || kind == 3 || kind == 4)
            {
                float rowv = y / 40.0;
                float3 goods = kind == 3 ? lerp(float3(0.35, 0.18, 0.08), float3(0.30, 0.30, 0.30), item) : (kind == 4 ? lerp(float3(0.70, 0.70, 0.68), float3(0.45, 0.55, 0.65), item) : lerp(float3(0.45, 0.35, 0.25), float3(0.30, 0.35, 0.40), item));
                goods = lerp(goods, kind == 3 ? float3(0.60, 0.25, 0.05) : float3(0.55, 0.20, 0.12), step(0.86, item));
                goods *= (frac(a / 7.0) < 0.12 || frac(rowv) > 0.85) ? 0.4 : 1.0;
                c = y < 200.0 ? (frac(rowv) < 0.1 ? float3(0.28, 0.28, 0.28) : goods) : wall;
                if (kind == 1 && t == tx && y < 190.0)
                {
                    c = frac(a / 70.0) < 0.06 ? float3(0.2, 0.2, 0.2) : lerp(float3(0.75, 0.80, 0.85), goods, 0.5);
                }
            }
            else if (kind == 2)
            {
                if (y < 105.0) { c = lerp(float3(0.45, 0.45, 0.45), float3(0.30, 0.16, 0.08), step(0.5, hv)); }
                else if (t == tz && y > 150.0 && y < 230.0) { c = frac(y / 12.0) < 0.25 && frac(a / 30.0) < 0.8 ? float3(0.75, 0.72, 0.60) : float3(0.04, 0.04, 0.04); }
            }
            else if (kind == 5)
            {
                if (y > 100.0 && y < 200.0 && t == tx) { c = float3(0.55, 0.62, 0.68); }
                else if (y < 95.0 && frac(a / 110.0) < 0.5) { c = lerp(float3(0.30, 0.04, 0.04), float3(0.04, 0.04, 0.04), step(0.5, hv)); }
            }
            else if (kind == 6)
            {
                if (y > 70.0 && y < 210.0) { c = lerp(float3(0.20, 0.22, 0.30), float3(0.60, 0.58, 0.55), L.H21(float2(floor(a / 6.0), seed))) * (frac(a / 6.0) < 0.15 ? 0.5 : 1.0); }
            }
            else
            {
                float blob = L.VN(float2(a, y) / 18.0 + seed * 5.0);
                c = y < 150.0 ? lerp(float3(0.05, 0.18, 0.06), lerp(float3(0.65, 0.10, 0.15), float3(0.75, 0.65, 0.15), item), step(0.62, blob)) : wall;
            }
            c *= t == tx ? 0.8 : 1.0;
        }
        else if (r.y > 0.0)
        {
            c = float3(0.72, 0.72, 0.70);
            if (fluor && frac(h.z / 160.0) < 0.06 && abs(h.x) < 0.3 * W) { c = float3(1.0, 1.0, 1.0); Light = 4.0; }
        }
        else
        {
            float2 tile = float2(h.x, h.z) / (kind == 5 ? 30.0 : 40.0);
            float check = step(0.5, frac(0.5 * (floor(tile.x) + floor(tile.y))));
            c = kind == 5 ? lerp(float3(0.06, 0.06, 0.06), float3(0.70, 0.70, 0.68), check) : (kind == 0 || kind == 4 ? lerp(float3(0.62, 0.62, 0.60), float3(0.55, 0.56, 0.55), check) : (kind == 2 ? lerp(float3(0.45, 0.10, 0.08), float3(0.65, 0.63, 0.58), check) : lerp(float3(0.30, 0.29, 0.27), float3(0.36, 0.35, 0.33), check)));
            // the room darkens away from the lamps, toward the back and the sides
            c *= lerp(1.0, 0.7, saturate(abs(h.x) / (0.5 * W)));
        }

        // What stands in the room, in front of the walls: boxes in (x across, y up, z in) from the glass.
        float3 o = float3(g.x, g.y, 0.0);
        float best = t;
        float3 thing = c;
        float thingLight = Light;
        float F = Floor;
        float side = hv < 0.5 ? -1.0 : 1.0;
        // a counter across the room (food, pharmacy, cleaners, barber's desk), or along one side
        if (kind == 2 || kind == 4 || kind == 6 || kind == 5)
        {
            float3 a0 = kind == 2 ? float3(-0.42 * W, F, 0.42 * D) : float3(side > 0.0 ? 0.08 * W : -0.45 * W, F, 0.18 * D);
            float3 a1 = kind == 2 ? float3(0.30 * W, F + 105.0, 0.50 * D) : float3(side > 0.0 ? 0.45 * W : -0.08 * W, F + 100.0, 0.30 * D);
            float tb = L.Box(o, r, a0, a1);
            if (tb < best)
            {
                best = tb;
                float3 p = o + r * tb;
                bool top = p.y > a1.y - 1.0;
                float3 counter = lerp(float3(0.40, 0.40, 0.42), float3(0.28, 0.14, 0.07), step(0.45, hv));
                thing = top ? counter * 1.25 : counter * (frac(p.y / 25.0) < 0.06 ? 0.6 : 0.85);
                thingLight = Light;
            }
        }
        // shelf gondolas running back into the shop (grocery, hardware, pharmacy, a small shop)
        if (kind == 1 || kind == 3 || kind == 4 || (kind == 6 && hv > 0.5))
        {
            float gw = 0.5 * W / 3.0;
            [unroll] for (int gi = -1; gi <= 1; gi += 2)
            {
                float3 a0 = float3(gi * gw - 30.0, F, 0.28 * D);
                float3 a1 = float3(gi * gw + 30.0, F + (kind == 3 ? 185.0 : 150.0), 0.82 * D);
                float tb = L.Box(o, r, a0, a1);
                if (tb < best)
                {
                    best = tb;
                    float3 p = o + r * tb;
                    float rowv = (p.y - F) / 37.0;
                    float itm = L.H21(float2(floor(p.z / 9.0 + p.x / 9.0), floor(rowv)) + seed * 7.0);
                    float3 goods = kind == 3 ? lerp(float3(0.30, 0.16, 0.07), float3(0.28, 0.28, 0.28), itm) : lerp(float3(0.62, 0.45, 0.28), float3(0.25, 0.35, 0.50), itm);
                    goods = lerp(goods, float3(0.65, 0.18, 0.10), step(0.84, itm));
                    thing = p.y > a1.y - 1.0 ? float3(0.30, 0.30, 0.30) : (frac(rowv) < 0.12 ? float3(0.22, 0.22, 0.22) : goods);
                    thingLight = Light * 0.9;
                }
            }
        }
        // a bank of machines down the middle of a laundromat
        if (kind == 0)
        {
            float3 a0 = float3(-65.0, F, 0.30 * D);
            float3 a1 = float3(65.0, F + 105.0, 0.90 * D);
            float tb = L.Box(o, r, a0, a1);
            if (tb < best)
            {
                best = tb;
                float3 p = o + r * tb;
                float cz = frac(p.z / 72.0) * 72.0 - 36.0;
                bool face = abs(abs(p.x) - 65.0) < 1.0;
                thing = face && length(float2(cz, p.y - F - 52.0)) < 19.0 ? float3(0.05, 0.06, 0.07) : float3(0.74, 0.74, 0.72);
                thingLight = Light;
            }
        }
        // pendant lamps over the warm-lit shops: a disc of light on a cord
        if (!fluor)
        {
            [unroll] for (int pi = 0; pi < 3; pi++)
            {
                float3 pc = float3((pi - 1.0) * 0.28 * W, Ceil - 75.0, (0.25 + 0.2 * pi) * D);
                float3 a0 = pc - float3(13.0, 9.0, 13.0);
                float3 a1 = pc + float3(13.0, 9.0, 13.0);
                float tb = L.Box(o, r, a0, a1);
                if (tb < best)
                {
                    best = tb;
                    float3 p = o + r * tb;
                    thing = p.y < pc.y - 8.0 ? float3(1.0, 0.92, 0.75) : float3(0.08, 0.06, 0.05);
                    thingLight = p.y < pc.y - 8.0 ? 6.0 : 0.5;
                }
                float tc = L.Box(o, r, pc + float3(-0.6, 9.0, -0.6), float3(pc.x + 0.6, Ceil, pc.z + 0.6));
                if (tc < best)
                {
                    best = tc;
                    thing = float3(0.02, 0.02, 0.02);
                    thingLight = 0.3;
                }
            }
        }
        if (best < t)
        {
            c = thing;
            Light = thingLight;
        }
        return c;
    }
};
FShop S;
"""

SHOP_HLSL = shop_hlsl()

# The palette a facade cut from a long building picks from (_facade_plan.facade_style): (wall, trim, masonry, painted).
SEG_STYLES = [
    ((0.32, 0.105, 0.077), (0.55, 0.50, 0.40), 0.0, 0.0),   # red brick
    ((0.24, 0.13, 0.084), (0.50, 0.46, 0.38), 0.0, 0.0),    # brown brick
    ((0.15, 0.095, 0.085), (0.52, 0.48, 0.42), 0.0, 0.0),   # dark ironspot brick
    ((0.22, 0.13, 0.098), (0.09, 0.055, 0.04), 1.0, 0.0),   # brownstone
    ((0.24, 0.24, 0.27), (0.03, 0.03, 0.035), 0.0, 1.0),    # painted brick
    ((0.36, 0.20, 0.11), (0.30, 0.30, 0.29), 0.0, 0.0),     # buff-orange brick
    ((0.40, 0.38, 0.34), (0.26, 0.25, 0.23), 1.0, 0.0),     # limestone ashlar
]


def seg_styles_hlsl():
    def f3(v):
        return "float3({0}, {1}, {2})".format(*("{0:.4f}".format(x) for x in v))
    walls = ", ".join(f3(s[0]) for s in SEG_STYLES)
    trims = ", ".join(f3(s[1]) for s in SEG_STYLES)
    mods = ", ".join("float2({0:.1f}, {1:.1f})".format(s[2], s[3]) for s in SEG_STYLES)
    return ("static const float3 SEG_WALL[{0}] = {{{1}}};\nstatic const float3 SEG_TRIM[{0}] = {{{2}}};\n"
            "static const float2 SEG_MODE[{0}] = {{{3}}};\n").format(len(SEG_STYLES), walls, trims, mods)

# --------------------------------------------------------------------------------------------------------------------
# the facade
# --------------------------------------------------------------------------------------------------------------------

# Inputs: WP (world position, cm), N (vertex normal), V (camera vector), OP / OB (object bounds centre and size),
# Wall, Trim, LitCol (float3), LitFrac, Glow, Masonry (0 brick, 1 brownstone, 2 concrete), Painted (0..1), Day (0 at
# night, 1 by day), Card (1 on a dressing window card: always a lit window), Atlas (the sign atlas, a texture object),
# FX, FY, FA0, FA1, FB0, FB1, FSeed (the building's frame, custom primitive data: _facade_plan.frame), NightFill (the
# city's glow on the walls at night). Outputs: the base colour, Rough, Emis, NormalWS (world space; the material has
# tangent-space normals off).
FACADE_BODY = r"""
float3 n = normalize(N);
float3 up = float3(0.0, 0.0, 1.0);
float vert = saturate((0.45 - abs(n.z)) * 10.0);
float2 t2 = float2(-n.y, n.x);
float tl = max(length(t2), 1e-4);
t2 /= tl;
float3 T = float3(t2, 0.0);
float3 outN = float3(n.xy / tl, 0.0);
float u = dot(WP.xy, t2);
float dpl = dot(WP.xy, outN.xy);
float z = WP.z;
float top = OP.z + 0.5 * OB.z;
float rel = top - z;
float tall = step(700.0, top) * (1.0 - Card);
float2 bcell = floor(OP.xy / 100.0);
float bs = L.H21(bcell);
float bs2 = L.H21(bcell + 71.3);
float fwu = fwidth(u);
float fwz = fwidth(z);
float fwc = max(max(fwu, fwz), 1e-3);

// The building's frame (_facade_plan): a wall along its axis A or B spans the building's extent on that axis, cut into
// facades of their own when it is long; each facade's shop bays run from its own corner to its own corner.
float2 fa = float2(FX, FY);
bool framed = dot(fa, fa) > 0.25 && Card < 0.5;
float u0 = floor(u / 1600.0) * 1600.0;
float u1 = u0 + 1600.0;
uint face = 0u;
bool span = false;
if (framed)
{
    fa = normalize(fa);
    float ca = dot(t2, fa);
    float cb = dot(t2, float2(-fa.y, fa.x));
    if (abs(ca) > 0.97) { span = true; face = ca > 0.0 ? 0u : 1u; u0 = ca > 0.0 ? FA0 : -FA1; u1 = ca > 0.0 ? FA1 : -FA0; }
    else if (abs(cb) > 0.97) { span = true; face = cb > 0.0 ? 2u : 3u; u0 = cb > 0.0 ? FB0 : -FB1; u1 = cb > 0.0 ? FB1 : -FB0; }
}
uint seed = (uint)round(FSeed);
float s0 = u0;
float s1 = u1;
uint seg = 0u;
if (span)
{
    float len = u1 - u0;
    int nseg = clamp((int)ceil((len - 1.0) / 1300.0), 1, 8);
    float prev = u0;
    [loop] for (int ks = 1; ks < nseg; ks++)
    {
        float j = L.HF(L.Key(seed, face, (uint)ks, 0u, 1u)) - 0.5;
        float b = u0 + len * ((float)ks + j * 0.3) / (float)nseg;
        b = floor(b / 160.0 + 0.5) * 160.0;
        b = min(max(b, prev + 640.0), u1 - 640.0);
        if (u >= b) { seg = (uint)ks; s0 = b; }
        else { s1 = b; break; }
        prev = b;
    }
}
// A facade cut from a long building is a building of its own: masonry, colour, trim, cornice, shop rhythm.
float3 wallIn = Wall;
float3 trimIn = Trim;
float masonryIn = Masonry;
float paintedIn = Painted;
if (seg > 0u)
{
    int ps = (int)(L.HF(L.Key(seed, face, seg, 0u, 3u)) * 7.0) % 7;
    wallIn = SEG_WALL[ps];
    trimIn = SEG_TRIM[ps];
    masonryIn = SEG_MODE[ps].x;
    paintedIn = SEG_MODE[ps].y;
    bs = L.HF(L.Key(seed, face, seg, 0u, 11u));
    bs2 = L.HF(L.Key(seed, face, seg, 0u, 12u));
}
int trimStyle = span ? (int)(L.HF(L.Key(seed, face, seg, 0u, 4u)) * 4.0) % 4 : 0;
int corniceStyle = span ? (int)(L.HF(L.Key(seed, face, seg, 0u, 5u)) * 4.0) % 4 : 0;
float pilasterEvery = span ? floor(L.HF(L.Key(seed, face, seg, 0u, 10u)) * 4.0) : 0.0;

// The building's own colour inside its style's range.
float3 wall = wallIn * lerp(0.78, 1.2, bs);
wall *= lerp(float3(0.93, 0.96, 1.06), float3(1.08, 0.97, 0.88), bs2);

int mode = (int)round(masonryIn);
float2 pitch = mode == 0 ? float2(21.0, 7.5) : (mode == 1 ? float2(91.5, 33.0) : float2(160.0, 110.0));
float jointW = mode == 0 ? 1.0 : (mode == 1 ? 0.7 : 1.6);
float bv = z / pitch.y;
float course = floor(bv);
float header = (mode == 0 && fmod(course, 6.0) > 4.5) ? 2.0 : 1.0;
float bu = u / pitch.x * header + 0.5 * fmod(course, 2.0);
float mu = jointW * header / pitch.x;
float mv = jointW / pitch.y;
float2 fw2 = float2(fwu * header / pitch.x, fwz / pitch.y);
float ju = L.Joint(bu + 0.5 * mu, fw2.x, mu);
float jv = L.Joint(bv + 0.5 * mv, fw2.y, mv);
float mortar = 1.0 - (1.0 - ju) * (1.0 - jv);
float detail = saturate(1.6 - 2.0 * max(fw2.x, fw2.y));
float2 cell = float2(floor(bu + 0.5 * mu), course);
float hb = L.H21(cell + bs * 57.0);
float var = mode == 0 ? lerp(0.74, 1.2, hb) : lerp(0.9, 1.08, hb);
float3 unit = wall * lerp(1.0, var, detail);
if (mode == 0)
{
    unit = lerp(unit, unit * float3(0.55, 0.45, 0.55), detail * step(0.93, L.H21(cell + 5.3)));
}
float mott = L.FBM(float2(u, z) / (mode == 0 ? 3.0 : 14.0));
unit *= lerp(1.0, lerp(0.84, 1.12, mott), saturate(1.5 - fwc / 1.5));
float3 mortarCol = mode == 0 ? float3(0.36, 0.345, 0.31) : wall * 0.72;
float3 col = lerp(unit, mortarCol, mortar);
if (mode == 2)
{
    float board = frac(z / 15.0);
    col *= lerp(1.0, (board < 0.06 ? 0.9 : 1.0) * lerp(0.94, 1.04, L.H21(float2(floor(z / 15.0), floor(u / 160.0)))), detail);
}

// Paint over brick, peeling here and there.
float peel = smoothstep(0.66, 0.72, L.FBM(float2(u, z) / 45.0 + bs * 10.0));
float3 paintCol = bs < 0.3 ? float3(0.62, 0.58, 0.50) : (bs < 0.55 ? wallIn : (bs < 0.8 ? float3(0.12, 0.20, 0.15) : float3(0.55, 0.52, 0.47)));
paintCol *= (1.0 - 0.18 * mortar);
float3 under = lerp(float3(0.30, 0.11, 0.075) * var, float3(0.36, 0.345, 0.31), mortar);
col = lerp(col, lerp(paintCol, under, peel), paintedIn);

// Weather: blotches, a dirty base with a salt line, soot under the top.
col *= lerp(0.76, 1.06, L.FBM(float2(u, z) / 240.0 + bs * 3.0));
col *= lerp(0.55, 1.0, sqrt(saturate((z - 15.0) / 280.0)));
float salt = saturate(1.0 - abs(z - 48.0) / 26.0) * smoothstep(0.35, 0.6, L.VN(float2(u / 30.0, 1.7)));
col = lerp(col, float3(0.50, 0.49, 0.46), 0.4 * salt);
col *= lerp(0.72, 1.0, saturate(rel / 320.0));
float rough = lerp(0.88, 0.94, mortar);
rough = lerp(rough, 0.62, paintedIn * (1.0 - peel));

// Masonry relief: bevelled units proud of the joints, a pitted face.
float3 nrm = n;
{
    float fu = frac(bu + 0.5 * mu);
    float fv = frac(bv + 0.5 * mv);
    float bev = mode == 0 ? 0.7 : 1.2;
    float cu = pitch.x / header;
    float eu0 = (fu - mu) * cu;
    float eu1 = (1.0 - fu) * cu;
    float ev0 = (fv - mv) * pitch.y;
    float ev1 = (1.0 - fv) * pitch.y;
    float gu = fu < mu ? 0.0 : (eu0 < bev ? 1.0 : (eu1 < bev ? -1.0 : 0.0));
    float gv = fv < mv ? 0.0 : (ev0 < bev ? 1.0 : (ev1 < bev ? -1.0 : 0.0));
    float k = (mode == 2 ? 0.3 : 0.8) * saturate(1.5 - fwc / 0.8) * (1.0 - 0.6 * paintedIn);
    float2 sp = float2(u, z) / (mode == 0 ? 2.5 : 6.0);
    float s0n = L.VN(sp);
    float sun = L.VN(sp + float2(0.35, 0.0));
    float svn = L.VN(sp + float2(0.0, 0.35));
    float ks = 1.2 * saturate(1.5 - fwc / 1.5);
    float GU = gu * k + (sun - s0n) * ks;
    float GV = gv * k + (svn - s0n) * ks;
    nrm = normalize(n - (GU * T + GV * up) * vert);
}

float3 emis = float3(0.0, 0.0, 0.0);
float snow = 0.0;
float3 trim = trimIn * lerp(0.9, 1.1, bs2);
float3 rv = -normalize(V);
float3 r = float3(dot(rv, T), rv.z, max(dot(rv, -outN), 0.03));

// The seam where two facades of a long building meet, and pilasters grouping the windows in pairs or threes.
if (span && seg > 0u && abs(u - s0) < 2.5 && vert > 0.5)
{
    col *= 0.35;
}
float bayU = u / 160.0;
float bay = floor(bayU);
float lu = (frac(bayU) - 0.5) * 160.0;
if (pilasterEvery > 1.5 && tall > 0.5 && vert > 0.5 && z > 395.0 && rel > 145.0 && abs(abs(lu) - 80.0) < 13.0
    && fmod(bay + (lu > 0.0 ? 1.0 : 0.0), pilasterEvery) < 0.5 && u > s0 + 100.0 && u < s1 - 100.0)
{
    col = lerp(trim, wall * 0.9, step(0.5, bs2)) * lerp(0.85, 1.05, L.VN(float2(u, z) / 20.0));
    rough = 0.8;
    nrm = normalize(n + T * (abs(lu) > 79.0 ? 0.0 : sign(lu) * 0.6) * (abs(abs(lu) - 80.0) > 11.0 ? 1.0 : 0.0));
}

// Upper floors: the window grid (160 cm bays, 330 cm storeys: the dressing's cards, the fire escapes and the window
// shot sit on it), the window heads by the facade's trim style.
float upper = step(420.0, z) * step(165.0, rel) * step(0.5, vert);
if (Card > 0.5)
{
    upper = 1.0;
}
float storey = floor(z / 330.0);
float lz = z - storey * 330.0 - 95.0;
float wseed = L.H21(float2(bay, storey) + floor(dpl / 100.0) * 1.37);
float3 frameCol = bs2 < 0.45 ? float3(0.62, 0.60, 0.55) : (bs2 < 0.8 ? float3(0.025, 0.025, 0.028) : float3(0.05, 0.12, 0.08));
if (upper > 0.5)
{
    if (trimStyle == 1)
    {
        // a segmental arch of soldier bricks over the opening
        float arch = 16.0 + 9.0 * (1.0 - saturate((lu / 64.0) * (lu / 64.0)));
        if (abs(lu) < 64.0 && lz > 150.0 && lz < 150.0 + arch)
        {
            float rad = frac((lu + 64.0) / 8.5);
            col = wall * lerp(0.7, 0.95, L.H21(float2(floor((lu + 64.0) / 8.5), storey + bay * 3.1))) * (rad < 0.12 ? 0.55 : 1.0);
            rough = 0.88;
            nrm = n;
        }
    }
    else
    {
        float hw = trimStyle == 3 ? 74.0 : 66.0;
        float ht = trimStyle == 3 ? 182.0 : 172.0;
        if (abs(lu) < hw && lz > 150.0 && lz < ht)
        {
            col = trim * lerp(0.85, 1.05, L.VN(float2(u, z) / 8.0)) * lerp(0.7, 1.0, saturate((lz - 150.0) / 6.0));
            rough = 0.8;
            nrm = n;
            if (lz > ht - 3.5)
            {
                snow = 1.0;
                nrm = normalize(n + up * 2.0);
            }
        }
        if (trimStyle == 2 && abs(lu) > 55.0 && abs(lu) < 66.0 && lz > -10.0 && lz < 150.0)
        {
            col = trim * lerp(0.8, 1.0, L.VN(float2(u, z) / 8.0));
            rough = 0.8;
            nrm = normalize(n + T * sign(lu) * 0.3);
        }
    }
    if (abs(lu) < 64.0 && lz > -10.0 && lz < 0.0)
    {
        col = trim * 0.9;
        rough = 0.8;
        nrm = n;
        if (lz > -3.0)
        {
            snow = 1.0;
            nrm = normalize(n + up * 2.0);
        }
    }
    if (abs(lu) < 64.0 && lz > -17.0 && lz <= -10.0)
    {
        col *= 0.45;
    }
    if (abs(lu) < 52.0 && lz < -17.0 && lz > -150.0)
    {
        float s = (1.0 - (-17.0 - lz) / 133.0) * smoothstep(0.4, 0.8, L.VN(float2(u / 3.5, storey * 7.1)));
        col *= 1.0 - 0.5 * s;
    }
}
float inWin = upper * step(abs(lu), 55.0) * step(0.0, lz) * step(lz, 150.0);
float isLit = Card > 0.5 ? 1.0 : step(L.H21(float2(wseed, 3.3)), LitFrac);
float ch = L.H21(float2(wseed, 7.7));
float3 lightCol = Card > 0.5 ? LitCol : (ch < 0.7 ? LitCol : (ch < 0.9 ? float3(0.85, 0.90, 1.0) : float3(0.5, 0.6, 1.0)));
float jit = lerp(0.6, 1.0, L.H21(float2(wseed, 9.9)));
if (inWin > 0.5)
{
    float R = (mode == 2 ? 10.0 : (mode == 1 ? 22.0 : 16.0)) + 2.0 * Card;
    float tx = r.x > 0.0 ? (55.0 - lu) / r.x : (r.x < 0.0 ? (-55.0 - lu) / r.x : 1e6);
    float ty = r.y > 0.0 ? (150.0 - lz) / r.y : (r.y < 0.0 ? (0.0 - lz) / r.y : 1e6);
    float tg = R / r.z;
    if (min(tx, ty) < tg)
    {
        float dep = min(tx, ty) * r.z / R;
        float3 rc = lerp(wall, mortarCol, 0.3) * lerp(1.0, 0.7, dep);
        rough = 0.85;
        if (tx < ty)
        {
            col = rc * 0.9;
            nrm = -sign(r.x) * T;
        }
        else if (r.y > 0.0)
        {
            col = rc * 0.75;
            nrm = -up;
        }
        else
        {
            col = trim * 0.9;
            nrm = up;
            snow = 0.8 * (1.0 - dep);
        }
    }
    else
    {
        float2 g = float2(lu + r.x * tg, lz + r.y * tg);
        float pane = step(abs(g.x), 48.0) * step(6.0, g.y) * step(g.y, 144.0) * (1.0 - step(abs(g.y - 76.0), 3.5));
        float ac = (1.0 - Card) * step(L.H21(float2(wseed, 5.1)), 0.16) * step(abs(g.x), 31.0) * step(6.0, g.y) * step(g.y, 46.0);
        if (pane < 0.5)
        {
            col = frameCol;
            rough = 0.55;
            nrm = n;
        }
        else if (ac > 0.5)
        {
            float grille = step(0.5, frac(g.y / 2.2)) * step(abs(g.x), 27.0) * step(g.y, 40.0) * step(12.0, g.y);
            col = float3(0.55, 0.52, 0.46) * (1.0 - 0.45 * grille) * lerp(0.75, 1.0, L.H21(float2(wseed, 2.9)));
            rough = 0.5;
            nrm = n;
            if (g.y > 43.0)
            {
                snow = 1.0;
                nrm = normalize(n + up * 2.0);
            }
        }
        else
        {
            // Glass: dark and glossy, each sash a little out of plane so the reflections break up. The room is emissive.
            float sash = g.y > 76.0 ? 1.0 : 2.0;
            float3 tilt = (L.H21(float2(wseed, sash)) - 0.5) * 0.07 * T + (L.H21(float2(wseed, sash + 2.0)) - 0.5) * 0.07 * up;
            nrm = normalize(n + tilt);
            col = float3(0.012, 0.013, 0.015);
            rough = 0.03 + 0.07 * L.VN(float2(u, z) / 12.0);
            float blind = step(0.35, L.H21(float2(wseed, 6.1))) * L.H21(float2(wseed, 6.2)) * 0.75;
            float curtain = step(0.6, L.H21(float2(wseed, 6.3))) * lerp(8.0, 24.0, L.H21(float2(wseed, 6.4)));
            float fh = L.H21(float2(wseed, 6.5));
            float3 fabric = fh < 0.7 ? float3(0.75, 0.70, 0.58) : (fh < 0.88 ? float3(0.55, 0.12, 0.10) : float3(0.18, 0.28, 0.50));
            float3 inside = float3(0.0, 0.0, 0.0);
            float light = 1.0;
            if (g.y > 144.0 - blind * 138.0)
            {
                inside = float3(0.80, 0.77, 0.70) * (frac(g.y / 3.0) < 0.3 ? 0.8 : 1.0);
                light = 0.9;
            }
            else if (abs(g.x) > 48.0 - curtain)
            {
                inside = fabric * (0.8 + 0.2 * sin(g.x * 0.9));
                light = 0.8;
            }
            else
            {
                float W = lerp(280.0, 420.0, L.H21(float2(wseed, 8.1)));
                float off = (L.H21(float2(wseed, 8.2)) - 0.5) * 80.0;
                inside = L.Room(g, r, W, off, -95.0, 215.0, lerp(350.0, 600.0, L.H21(float2(wseed, 8.3))), wseed, 0.0, light);
            }
            float3 tint = isLit > 0.5 ? lightCol * jit * 2.5 : float3(0.75, 0.80, 0.90) * (0.03 + 0.6 * Day);
            emis = inside * light * Glow * tint;
        }
    }
}

// The ground floor of the taller buildings: each facade's shop bays from its own corner to its own corner (end piers
// END_PIER 45 cm), a fascia over them with one sign per bay, centred in it; a recessed door in each shop; one bay of
// three or more is the tenement's entrance.
float ground = tall * step(0.5, vert) * step(z, 395.0);
if (ground > 0.5)
{
    float usable = (s1 - s0) - 90.0;
    float target = (int)(L.HF(L.Key(seed, face, seg, 0u, 2u)) * 3.0) % 3 == 0 ? 300.0 : ((int)(L.HF(L.Key(seed, face, seg, 0u, 2u)) * 3.0) % 3 == 1 ? 360.0 : 420.0);
    float nb = max(1.0, floor(usable / target + 0.5));
    float pitchS = usable / nb;
    float lx = u - s0 - 45.0;
    float bayF = floor(lx / pitchS);
    bool endPier = lx < 0.0 || lx >= usable;
    uint bayI = (uint)clamp(bayF, 0.0, nb - 1.0);
    float su = lx - (bayF + 0.5) * pitchS;
    bool entrance = nb >= 3.0 && bayI == (uint)floor(nb * 0.5);
    float pier = lerp(30.0, 55.0, L.HF(L.Key(seed, face, seg, 0u, 13u)));
    float halfGlass = 0.5 * (pitchS - pier);
    uint sk = L.Key(seed, face, seg, bayI, 0u);
    float sseed = L.HF(sk) * 97.0;
    int trade = (int)(L.HF(L.Key(seed, face, seg, bayI, 8u)) * (float)SHOP_NAMES) % (int)SHOP_NAMES;
    int kind = (int)NAME_KIND[trade];
    bool vacant = L.HF(L.Key(seed, face, seg, bayI, 6u)) < 0.15;
    bool openShop = !vacant && (L.HF(L.Key(seed, face, seg, bayI, 7u)) < LitFrac + 0.2 || Day > 0.5);
    float3 fascia = lerp(float3(0.03, 0.03, 0.035), trim * 0.6, step(0.6, L.HF(L.Key(seed, face, seg, 0u, 14u))));
    if (z > 300.0)
    {
        // the fascia band, and the sign: one per shop bay, never past the bay
        col = endPier ? col : fascia * lerp(0.85, 1.05, L.VN(float2(u, z) / 25.0));
        rough = 0.5;
        nrm = n;
        float halfPanel = 0.5 * pitchS - 14.0;
        float panel = (endPier || entrance) ? 0.0 : step(abs(su), halfPanel) * step(310.0, z) * step(z, 386.0);
        if (panel > 0.5)
        {
            float tt = L.HF(L.Key(seed, face, seg, bayI, 9u));
            int treat = tt < 0.34 ? 0 : (tt < 0.56 ? 1 : (tt < 0.78 ? 2 : 3));      // painted, raised, lit box, neon
            float hc = L.HF(L.Key(seed, face, seg, bayI, 15u));
            int len = (int)NAME_LEN[trade];
            int fsel = treat == 3 ? (hc < 0.5 ? 2 : 3) : (treat == 1 ? (hc < 0.5 ? 0 : 2) : (len > 9 ? 1 : (hc < 0.7 ? 0 : 3)));
            float4 fm = fsel == 0 ? float4(1, 0, 0, 0) : (fsel == 1 ? float4(0, 1, 0, 0) : (fsel == 2 ? float4(0, 0, 1, 0) : float4(0, 0, 0, 1)));
            float3 panelCol;
            float3 inkCol;
            if (treat == 0)
            {
                panelCol = hc < 0.2 ? float3(0.40, 0.035, 0.03) : (hc < 0.4 ? float3(0.025, 0.11, 0.05) : (hc < 0.6 ? float3(0.02, 0.03, 0.11) : (hc < 0.8 ? float3(0.55, 0.52, 0.45) : float3(0.025, 0.025, 0.028))));
                inkCol = hc < 0.4 ? float3(0.80, 0.78, 0.70) : (hc < 0.6 ? float3(0.85, 0.65, 0.10) : (hc < 0.8 ? float3(0.05, 0.05, 0.05) : float3(0.75, 0.55, 0.20)));
            }
            else if (treat == 1)
            {
                panelCol = hc < 0.35 ? float3(0.02, 0.02, 0.022) : (hc < 0.7 ? float3(0.12, 0.06, 0.03) : wall * 0.8);
                inkCol = hc < 0.5 ? float3(0.70, 0.52, 0.20) : float3(0.78, 0.76, 0.72);
            }
            else if (treat == 2)
            {
                panelCol = hc < 0.5 ? float3(0.80, 0.80, 0.76) : (hc < 0.75 ? float3(0.75, 0.62, 0.10) : float3(0.10, 0.25, 0.60));
                inkCol = hc < 0.25 ? float3(0.45, 0.02, 0.02) : (hc < 0.5 ? float3(0.02, 0.06, 0.25) : (hc < 0.75 ? float3(0.03, 0.03, 0.03) : float3(0.85, 0.85, 0.82)));
            }
            else
            {
                panelCol = float3(0.015, 0.015, 0.017);
                inkCol = hc < 0.3 ? float3(1.0, 0.15, 0.35) : (hc < 0.55 ? float3(0.25, 0.55, 1.0) : (hc < 0.8 ? float3(1.0, 0.30, 0.10) : float3(0.30, 1.0, 0.45)));
            }
            // The trade's name: Roboto from the sign atlas, laid out on its advances, centred and fitted to the panel.
            uint st = NAME_START[trade];
            float wsum = 0.0;
            [loop] for (int ia = 0; ia < len; ia++) { wsum += ADV[(uint)fsel * NG + NAME_CHARS[st + (uint)ia]]; }
            float panelH = 76.0;
            float kc = min(min(0.46 * panelH, 34.0) / CAP_F, 0.84 * 2.0 * halfPanel / max(wsum, 0.1));   // cm per cell
            float readDir = dot(V, n) >= 0.0 ? -1.0 : 1.0;
            float qx = readDir * su / kc + 0.5 * wsum;
            float qy = 0.5 - (z - 348.0) / kc + (BASE_F - 0.5 * CAP_F - 0.5);
            float pxc = max(fwu, fwz) / kc * CELL_PX;                 // atlas texels per pixel
            float lod = log2(max(pxc, 1.0));
            float pen = 0.0;
            int ci = len;
            [loop] for (int ib = 0; ib < len; ib++)
            {
                float a = ADV[(uint)fsel * NG + NAME_CHARS[st + (uint)ib]];
                if (qx < pen + a) { ci = ib; break; }
                pen += a;
            }
            float sd = 0.0;
            float sh = 0.0;
            [unroll] for (int jx = -1; jx <= 1; jx++)
            {
                int idx = ci + jx;
                if (idx >= 0 && idx < len)
                {
                    uint gch = NAME_CHARS[st + (uint)idx];
                    float pj = jx < 0 ? pen - ADV[(uint)fsel * NG + gch] : (jx > 0 ? pen + ADV[(uint)fsel * NG + NAME_CHARS[st + (uint)ci]] : pen);
                    float2 c = float2(qx - pj + ORIGIN_F, qy);
                    float2 gc = float2(fmod((float)gch, GLYPH_GRID), floor((float)gch / GLYPH_GRID));
                    if (c.x >= 0.0 && c.x <= 1.0 && c.y >= 0.0 && c.y <= 1.0)
                    {
                        sd = max(sd, dot(Atlas.SampleLevel(AtlasSampler, (gc + c) / GLYPH_GRID, lod), fm));
                    }
                    float2 cs = c - float2(0.03, 0.045);
                    if (treat == 1 && cs.x >= 0.0 && cs.x <= 1.0 && cs.y >= 0.0 && cs.y <= 1.0)
                    {
                        sh = max(sh, dot(Atlas.SampleLevel(AtlasSampler, (gc + cs) / GLYPH_GRID, lod), fm));
                    }
                }
            }
            float aa = 0.5 * pxc / SDF_PX + 0.01;
            float ink = smoothstep(0.5 - aa, 0.5 + aa, sd);
            float farF = saturate(pxc / 40.0 - 1.0);
            ink = lerp(ink, 0.22, farF);
            float halo = saturate((sd - 0.12) / 0.38) * (1.0 - ink) * (1.0 - farF);
            col = panelCol;
            if (treat == 2)
            {
                // the box sign's frame
                float edge = min(halfPanel - abs(su), min(z - 310.0, 386.0 - z));
                col = edge < 4.0 ? float3(0.05, 0.05, 0.055) : col;
                emis += (edge < 4.0 ? 0.0 : 1.0) * lerp(panelCol, inkCol, ink) * Glow * lerp(3.2, 0.25, Day);
            }
            if (treat == 1)
            {
                col *= 1.0 - 0.55 * smoothstep(0.5 - aa, 0.5 + aa, sh) * (1.0 - ink);
                emis += inkCol * halo * halo * Glow * 1.6 * (1.0 - Day) * step(0.5, hc);
            }
            if (treat == 3)
            {
                float3 tube = lerp(float3(0.55, 0.52, 0.50), inkCol, 0.25);
                col = lerp(col, tube, ink);
                emis += (inkCol * ink * lerp(9.0, 1.2, Day) + inkCol * halo * halo * lerp(2.5, 0.0, Day)) * Glow;
                rough = lerp(0.6, 0.15, ink);
            }
            else
            {
                col = lerp(col, inkCol, ink);
                rough = treat == 1 ? lerp(0.6, 0.3, ink) : 0.45;
            }
            if (treat == 0)
            {
                emis += col * Glow * 0.6 * (1.0 - Day) * step(0.45, hc);
            }
        }
        if (z > 391.0)
        {
            snow = 1.0;
            nrm = normalize(n + up * 2.0);
        }
    }
    else if (!endPier && entrance && abs(su) < 62.0 && z > 15.0 && z < 300.0)
    {
        // the tenement's door, 60 cm in, a lit transom over it
        float3 hit;
        int part = L.Recess(float2(su, z - 15.0), r, 62.0, 285.0, 60.0, hit);
        if (part == 3)
        {
            bool transom = hit.y > 240.0;
            bool frameD = abs(hit.x) > 54.0 || (hit.y > 232.0 && hit.y < 240.0) || hit.y < 6.0;
            bool glass = !transom && abs(hit.x) < 40.0 && hit.y > 120.0 && hit.y < 215.0;
            col = frameD ? float3(0.05, 0.03, 0.02) : (transom || glass ? float3(0.015, 0.016, 0.018) : float3(0.09, 0.045, 0.025) * (frac(hit.y / 40.0) < 0.05 ? 0.6 : 1.0));
            rough = transom || glass ? 0.05 : 0.5;
            emis += (transom || glass) ? float3(1.0, 0.8, 0.55) * Glow * lerp(1.2, 0.2, Day) * (transom ? 1.0 : 0.5) : float3(0.0, 0.0, 0.0);
            nrm = n;
        }
        else
        {
            col = part == 2 ? float3(0.30, 0.30, 0.29) : lerp(wall, mortarCol, 0.3) * (part == 1 ? 0.5 : 0.75);
            nrm = part == 0 ? -sign(r.x) * T : (part == 1 ? -up : up);
            rough = 0.85;
        }
    }
    else if (!endPier && !entrance && abs(su) < halfGlass && z > 15.0 && z < 300.0)
    {
        float doorSide = L.HF(L.Key(seed, face, seg, bayI, 16u)) < 0.5 ? -1.0 : 1.0;
        float dc = doorSide * (halfGlass - 58.0);
        bool inDoor = abs(su - dc) < 52.0 && z < 255.0 && halfGlass > 120.0;
        if (!openShop)
        {
            // the roll-down gate, ribbed, tagged, over glass and door
            float rib = frac((z - 15.0) / 7.0);
            col = lerp(float3(0.30, 0.30, 0.31), float3(0.20, 0.21, 0.22), L.H21(float2(sseed, 4.4))) * lerp(0.7, 1.05, L.VN(float2(su / 40.0, z / 60.0) + sseed * 9.0)) * (rib < 0.15 ? 0.6 : 1.0);
            nrm = normalize(n + up * (rib < 0.15 ? -0.6 : (rib > 0.85 ? 0.6 : 0.0)));
            rough = 0.45;
            float tag = smoothstep(0.6, 0.66, L.FBM(float2(su, z) / 35.0 + sseed * 20.0));
            col = lerp(col, lerp(float3(0.45, 0.08, 0.35), float3(0.08, 0.25, 0.55), L.H21(float2(sseed, 3.0))), tag * 0.8);
            if (z > 285.0) { col = float3(0.12, 0.12, 0.13); }
        }
        else if (inDoor)
        {
            // the shop door, recessed 85 cm: tiled floor, jambs, a glass door in an aluminium frame, a push bar
            float3 hit;
            int part = L.Recess(float2(su - dc, z - 15.0), r, 52.0, 240.0, 85.0, hit);
            if (part == 3)
            {
                bool frameD = abs(hit.x) > 44.0 || hit.y < 14.0 || hit.y > 228.0 || abs(hit.y - 105.0) < 3.0;
                col = frameD ? float3(0.42, 0.42, 0.42) : float3(0.012, 0.013, 0.015);
                rough = frameD ? 0.35 : 0.05;
                float lt;
                float3 tintD;
                float3 inside = S.Room(float2(hit.x + dc * 0.3, hit.y - 25.0), r, 2.0 * halfGlass + 60.0, -25.0, 260.0, 500.0, kind, sseed, lt, tintD);
                emis += frameD ? float3(0.0, 0.0, 0.0) : inside * lt * Glow * 1.1 * tintD * lerp(1.0, 0.4, Day);
                nrm = n;
            }
            else
            {
                float3 tileC = lerp(float3(0.55, 0.53, 0.50), float3(0.12, 0.12, 0.12), step(0.5, frac(0.5 * (floor(hit.x / 15.0) + floor(hit.z / 15.0)))));
                col = part == 2 ? tileC : (part == 1 ? float3(0.05, 0.05, 0.05) : lerp(float3(0.35, 0.35, 0.36), float3(0.10, 0.10, 0.10), step(0.5, sseed / 97.0)));
                nrm = part == 0 ? -sign(r.x) * T : (part == 1 ? -up : up);
                rough = part == 2 ? 0.3 : 0.6;
                emis += part == 2 ? tileC * Glow * 0.15 * (1.0 - Day) : float3(0.0, 0.0, 0.0);
            }
        }
        else if (z < 55.0)
        {
            // the bulkhead under the glass: tile, steel or painted wood
            float bh = L.H21(float2(sseed, 4.9));
            col = bh < 0.4 ? float3(0.06, 0.06, 0.065) * (frac(z / 15.0) < 0.08 ? 0.5 : 1.0) : (bh < 0.7 ? float3(0.35, 0.35, 0.36) : float3(0.10, 0.18, 0.12));
            rough = 0.4;
            nrm = n;
        }
        else
        {
            float tg = 8.0 / r.z;
            float2 g = float2(su + r.x * tg, (z - 55.0) + r.y * tg);
            float hw = halfGlass - 6.0;
            float mull = L.H21(float2(sseed, 7.3)) < 0.5 ? 0.0 : hw * (L.H21(float2(sseed, 7.4)) - 0.5);
            float transom = lerp(180.0, 205.0, L.H21(float2(sseed, 7.5)));
            float pane = step(abs(g.x), hw) * step(4.0, g.y) * step(g.y, 241.0) * (1.0 - step(abs(g.x - mull), 3.0)) * (1.0 - step(abs(g.y - transom), 3.0));
            pane *= 1.0 - step(abs(g.x - dc), 58.0) * step(g.y, 200.0);
            nrm = n;
            if (pane < 0.5)
            {
                col = L.H21(float2(sseed, 7.6)) < 0.6 ? float3(0.03, 0.03, 0.03) : float3(0.25, 0.25, 0.24);
                rough = 0.4;
            }
            else
            {
                col = float3(0.012, 0.013, 0.015);
                rough = 0.05;
                float light = 1.0;
                float3 tintS;
                float3 inside = S.Room(float2(g.x, g.y + 15.0), r, 2.0 * hw + 60.0, -40.0, 245.0, lerp(450.0, 800.0, L.H21(float2(sseed, 8.1))), kind, sseed, light, tintS);
                // By day the shop is darker than the street and its colours wash out.
                inside = lerp(inside, dot(inside, float3(0.3, 0.6, 0.1)).xxx, 0.4 * Day);
                emis = inside * light * Glow * 1.3 * tintS * lerp(1.0, 0.45, Day);
                // painted lettering on the glass of a few shops
            }
        }
    }
}
// Cornice under the parapet, by the facade's style: stepped stone with dentils, brackets under a slab, a plain
// coping, or a deep pressed-metal cornice; its shadow under it.
if (tall > 0.5 && vert > 0.5 && rel > 90.0 && rel < 145.0)
{
    float k = (rel - 90.0) / 55.0;
    if (corniceStyle == 0)
    {
        float dent = step(0.75, k) * step(0.45, frac(u / 16.0));
        col = trim * lerp(1.0, 0.7, k) * (1.0 - 0.35 * dent) * lerp(0.85, 1.05, L.VN(float2(u / 30.0, rel / 10.0)));
        rough = 0.7;
        nrm = normalize(n + up * (k < 0.15 ? 1.5 : (k > 0.6 ? -0.8 : 0.0)));
    }
    else if (corniceStyle == 1)
    {
        float brk = step(frac(u / 70.0), 0.2) * step(0.35, k);
        col = trim * lerp(1.0, 0.75, k) * (brk > 0.5 ? 0.8 : 1.0) * lerp(0.85, 1.05, L.VN(float2(u / 30.0, rel / 10.0)));
        rough = 0.7;
        nrm = normalize(n + up * (k < 0.3 ? 1.2 : 0.0) + T * (brk > 0.5 ? (frac(u / 70.0) < 0.1 ? -0.5 : 0.5) : 0.0));
    }
    else if (corniceStyle == 2)
    {
        if (k < 0.3)
        {
            col = trim * 0.95;
            rough = 0.75;
            nrm = normalize(n + up * (k < 0.08 ? 1.5 : 0.0));
        }
    }
    else
    {
        float hm = L.HF(L.Key(seed, face, seg, 0u, 17u));
        float3 metal = hm < 0.4 ? float3(0.03, 0.06, 0.04) : (hm < 0.75 ? float3(0.02, 0.02, 0.022) : float3(0.45, 0.42, 0.36));
        float panelM = step(frac(u / 90.0), 0.06) + step(abs(k - 0.5), 0.03);
        col = metal * lerp(1.0, 0.75, k) * (1.0 - 0.35 * saturate(panelM));
        rough = 0.45;
        nrm = normalize(n + up * (k < 0.2 ? 1.3 : (k > 0.8 ? -1.0 : 0.0)));
    }
}
if (tall > 0.5 && vert > 0.5 && rel >= 145.0 && rel < 158.0 && corniceStyle != 2)
{
    col *= 0.2;
}

// Roofs: tar under the snow layer, drifts in the normal.
if (vert < 0.5)
{
    col = float3(0.05, 0.05, 0.055) * lerp(0.7, 1.3, L.VN(WP.xy / 80.0));
    float2 p = WP.xy / 300.0;
    float a = L.FBM(p);
    float ax = L.FBM(p + float2(0.05, 0.0));
    float ay = L.FBM(p + float2(0.0, 0.05));
    nrm = normalize(n + float3(a - ax, a - ay, 0.0) * 4.0);
    rough = 0.9;
}

col = lerp(col, float3(0.62, 0.62, 0.63), snow);
rough = lerp(rough, 0.6, snow);
// At night the overcast, lit orange by the city, lights the walls a little: brick reads against the sky above the
// lamps instead of going black.
emis += col * NightFill * (1.0 - Day) * vert * float3(1.0, 0.72, 0.5) * (1.0 - Card);
Rough = rough;
Emis = emis;
NormalWS = nrm;
return col;
"""

FACADE_HLSL = LIB_HLSL + SHOP_HLSL + seg_styles_hlsl() + FACADE_BODY

# --------------------------------------------------------------------------------------------------------------------
# the ground
# --------------------------------------------------------------------------------------------------------------------

# Shared by road and sidewalk: the street frame from custom primitive data (DX, DY: the street's direction; C: its
# centre line's offset along the normal; H: the carriageway's half width, all cm). No frame (0, 0) falls back to the
# world axes.
FRAME_HLSL = r"""
float2 dir = float2(DX, DY);
float hasFrame = step(0.25, dot(dir, dir));
dir = hasFrame > 0.5 ? normalize(dir) : float2(1.0, 0.0);
float2 side = float2(-dir.y, dir.x);
float along = dot(WP.xy, dir);
float across = dot(WP.xy, side) - C;
float halfW = hasFrame > 0.5 ? max(H, 100.0) : 600.0;
float a = abs(across);
float fw = max(max(fwidth(WP.x), fwidth(WP.y)), 1e-3);
float3 n = normalize(N);
float3 up = float3(0.0, 0.0, 1.0);
float3 Tw = float3(dir, 0.0);
float3 Sw = float3(side, 0.0);
float glint = step(0.9993, L.H21(floor(WP.xy / 2.5) + 0.5)) * saturate(1.0 - Depth / 1400.0);
"""

ROAD_BODY = r"""
float3 asph = float3(0.045, 0.045, 0.048) * lerp(0.75, 1.25, L.FBM(WP.xy / 150.0));
asph *= lerp(1.0, lerp(0.85, 1.18, L.H21(floor(WP.xy / 1.2))), saturate(1.0 - fw / 0.6));
float2 pc = floor(float2(along / 420.0, across / 260.0));
asph = lerp(asph, float3(0.028, 0.028, 0.03), 0.8 * step(0.82, L.H21(pc + 3.7)));
float cr = abs(L.VN(float2(along, across) / 90.0) - 0.5);
float crack = (1.0 - smoothstep(0.0, 0.01 + fw * 0.002, cr)) * saturate(1.5 - fw / 2.0) * step(0.62, L.VN(float2(along, across) / 700.0));
asph *= 1.0 - 0.6 * crack;

// Snow, as a day after a fall looks from the street: the wheel tracks worn to black wet asphalt, grey-brown slush
// between them and at the lane lines, the parking lanes packed and dirty, a plowed ridge at the kerb; clean snow only
// where nothing drives.
float park = hasFrame * step(halfW - 250.0, a);
float drive = max(halfW - 250.0, 165.0);
float laneW = 330.0;
float lanes = max(1.0, floor(2.0 * drive / laneW));
float x = across + 0.5 * lanes * laneW;
float lx = frac(x / laneW) * laneW - 0.5 * laneW;
float wob = (L.VN(float2(along / 700.0, floor(x / laneW) * 3.0)) - 0.5) * 50.0;
float trackD = min(abs(lx - wob - 82.0), abs(lx - wob + 82.0));
float track = 1.0 - smoothstep(34.0, 62.0, trackD + (L.VN(float2(along / 60.0, lx / 20.0)) - 0.5) * 22.0);
float sn = L.FBM(float2(along, across) / 120.0);
float between = saturate(0.47 + 0.5 * (sn - 0.5)) + 0.15 * (1.0 - smoothstep(30.0, 80.0, abs(abs(lx) - 165.0)));
float cover = lerp(between, 0.0, track);
float ridge = hasFrame * smoothstep(halfW - 110.0, halfW - 15.0, a);
cover = lerp(cover, saturate(0.62 + 0.5 * (sn - 0.5)), park);
cover = max(cover, ridge);
if (hasFrame < 0.5)
{
    cover = saturate(0.35 + 0.8 * (sn - 0.5));
}
float slushy = saturate(1.0 - abs(cover - 0.45) * 3.0);
float3 snowC = float3(0.56, 0.56, 0.57) * lerp(0.78, 1.0, L.VN(WP.xy / 40.0));
float3 slushC = float3(0.15, 0.14, 0.125) * lerp(0.8, 1.15, L.VN(WP.xy / 25.0));
float3 col = lerp(asph * 0.7, slushC, saturate(cover * 2.0));
col = lerp(col, lerp(snowC, float3(0.36, 0.34, 0.31), 0.35 * park), saturate(cover * 2.0 - 1.0));
col = lerp(col, float3(0.24, 0.22, 0.19), ridge * lerp(0.45, 0.9, L.VN(WP.xy / 60.0)));
float wet = 1.0 - saturate(cover * 2.0);
float rough = lerp(0.7, 0.42, wet * lerp(0.6, 1.0, L.VN(WP.xy / 300.0)));
rough = lerp(rough, 0.6, slushy);
rough = lerp(rough, 0.72, saturate(cover * 2.0 - 1.0));
float yl = step(900.0, halfW) * step(abs(abs(across) - 10.0), 5.0);
col = lerp(col, float3(0.50, 0.33, 0.03), yl * (1.0 - saturate(cover * 1.6)));

float2 q = float2(along, across) / 45.0;
float b0 = L.FBM(q);
float bx = L.FBM(q + float2(0.08, 0.0));
float by = L.FBM(q + float2(0.0, 0.08));
float k = 1.5 * saturate(cover * 1.5) + 0.6 * ridge;
float3 nrm = normalize(up + ((b0 - bx) * Tw + (b0 - by) * Sw) * k);
Rough = rough;
Emis = glint * saturate(cover * 2.0 - 1.0) * Sparkle;
NormalWS = abs(n.z) > 0.5 ? nrm : n;
return col;
"""

SIDEWALK_BODY = r"""
// H of 1e6 or more: the district's ground slab, flags on the grid, no kerb and no path.
float plaza = step(1e6, H);
float ka = plaza > 0.5 ? across : a - halfW;
float kerbTop = hasFrame * (1.0 - plaza) * step(ka, 18.0);
float2 f = float2(along / 152.4, (ka - 18.0) / 152.4);
float jx = L.Joint(f.x + 0.004, fwidth(f.x), 0.008);
float jy = L.Joint(f.y + 0.004, fwidth(f.y), 0.008);
float joint = max(jx, jy) * (1.0 - kerbTop);
float flag = L.H21(floor(f) + 2.3);
float3 conc = float3(0.36, 0.355, 0.34) * lerp(0.82, 1.1, flag) * lerp(0.85, 1.08, L.FBM(WP.xy / 60.0));
conc *= lerp(1.0, lerp(0.85, 1.15, L.H21(floor(WP.xy / 0.8))), saturate(1.5 - fw / 0.8));
float cr = abs(L.VN(WP.xy / 70.0 + flag * 13.0) - 0.5);
conc *= 1.0 - 0.35 * step(0.85, flag) * (1.0 - smoothstep(0.0, 0.004 + fw * 0.002, cr)) * saturate(1.5 - fw / 1.5);
float3 col = lerp(conc, float3(0.11, 0.11, 0.11), joint);
float3 kerbC = float3(0.30, 0.30, 0.31) * lerp(0.75, 1.15, L.H21(floor(WP.xy / 1.5)));
col = lerp(col, kerbC, kerbTop);

// Snow: a path shovelled down the middle with a ragged edge, banks of shovelled snow either side (the kerb side
// round the trees and lamps, the building side), salt and wet where it is clear.
float onWalk = hasFrame * (1.0 - plaza);
float pathIn = 75.0 + (L.VN(float2(along / 80.0, 3.1)) - 0.5) * 22.0;
float pathOut = 255.0 + (L.VN(float2(along / 95.0, 7.3)) - 0.5) * 26.0;
float path = onWalk * smoothstep(pathIn - 4.0, pathIn + 4.0, ka) * (1.0 - smoothstep(pathOut - 4.0, pathOut + 4.0, ka));
float sn = L.FBM(float2(along, ka) / 90.0);
float bank = onWalk * (1.0 - path);
float cover = lerp(saturate(0.5 + (sn - 0.5) * 1.6), saturate(0.95 + (sn - 0.5) * 0.3), bank);
cover = lerp(cover, 0.03 + 0.22 * smoothstep(0.66, 0.88, sn), path);
cover = saturate(cover - kerbTop * 0.4);
float salt = smoothstep(0.55, 0.75, L.FBM(WP.xy / 25.0 + 4.0)) * (1.0 - cover);
col = lerp(col, float3(0.56, 0.56, 0.54), salt * 0.55);
float wetc = (1.0 - cover) * smoothstep(0.3, 0.65, L.VN(WP.xy / 200.0));
col *= lerp(1.0, 0.5, wetc);
float3 snowC = float3(0.62, 0.62, 0.63) * lerp(0.82, 1.0, L.VN(WP.xy / 30.0));
float trod = smoothstep(0.3, 0.7, cover);
col = lerp(col, lerp(float3(0.42, 0.41, 0.39), snowC, smoothstep(0.6, 0.95, cover)), trod);
float rough = lerp(lerp(0.85, 0.25, wetc), 0.7, trod);

float2 q = WP.xy / 35.0;
float b0 = L.FBM(q);
float bx = L.FBM(q + float2(0.08, 0.0));
float by = L.FBM(q + float2(0.0, 0.08));
// the banks fall away into the path at its edges
float sideSign = across >= 0.0 ? 1.0 : -1.0;
float inB = onWalk * smoothstep(pathIn - 40.0, pathIn - 5.0, ka) * (1.0 - smoothstep(pathIn - 5.0, pathIn + 3.0, ka));
float outB = onWalk * smoothstep(pathOut - 3.0, pathOut + 5.0, ka) * (1.0 - smoothstep(pathOut + 5.0, pathOut + 40.0, ka));
float3 nrm = normalize(up + float3(b0 - bx, b0 - by, 0.0) * 1.6 * trod + Sw * sideSign * (inB - outB) * 0.9);
if (abs(n.z) < 0.5)
{
    // the kerb face, its steel edge at the top
    col = kerbC * 0.85 * lerp(0.6, 1.0, saturate((WP.z - 2.0) / 8.0));
    rough = 0.7;
    if (WP.z > 11.0)
    {
        col = float3(0.08, 0.05, 0.04);
        rough = 0.5;
    }
    nrm = n;
}
Rough = rough;
Emis = glint * smoothstep(0.7, 0.95, cover) * Sparkle;
NormalWS = nrm;
return col;
"""

PARK_BODY = r"""
float3 n = normalize(N);
float2 p = WP.xy;
float drift = L.FBM(p / 900.0);
float hgt = drift * 30.0 + L.VN(float2(dot(p, float2(0.8, 0.6)) / 22.0, dot(p, float2(-0.6, 0.8)) / 260.0)) * 1.2 + L.FBM(p / 60.0) * 3.0;
float e = 4.0;
float hx = L.FBM((p + float2(e, 0.0)) / 900.0) * 30.0 + L.VN(float2(dot(p + float2(e, 0.0), float2(0.8, 0.6)) / 22.0, dot(p + float2(e, 0.0), float2(-0.6, 0.8)) / 260.0)) * 1.2 + L.FBM((p + float2(e, 0.0)) / 60.0) * 3.0;
float hy = L.FBM((p + float2(0.0, e)) / 900.0) * 30.0 + L.VN(float2(dot(p + float2(0.0, e), float2(0.8, 0.6)) / 22.0, dot(p + float2(0.0, e), float2(-0.6, 0.8)) / 260.0)) * 1.2 + L.FBM((p + float2(0.0, e)) / 60.0) * 3.0;
float3 nrm = normalize(float3((hgt - hx) / e, (hgt - hy) / e, 1.0));
float grass = smoothstep(0.74, 0.8, L.FBM(p / 280.0 + 7.0));
float3 col = float3(0.64, 0.64, 0.66) * lerp(0.9, 1.0, drift);
col = lerp(col, float3(0.10, 0.11, 0.07) * lerp(0.7, 1.2, L.VN(p / 5.0)), grass * 0.85);
float glint = step(0.9993, L.H21(floor(p / 2.5) + 0.5)) * saturate(1.0 - Depth / 1400.0) * (1.0 - grass);
Rough = lerp(0.62, 0.9, grass);
Emis = glint * Sparkle;
NormalWS = abs(n.z) > 0.5 ? nrm : n;
return col;
"""

ROAD_HLSL = LIB_HLSL + FRAME_HLSL + ROAD_BODY
SIDEWALK_HLSL = LIB_HLSL + FRAME_HLSL + SIDEWALK_BODY
PARK_HLSL = LIB_HLSL + PARK_BODY

# The facade styles: name -> (masonry, painted). The colours stay in _materials.FACADE_STYLES.
MASONRY = {
    "BrickRed": (0.0, 0.0),
    "BrickBrown": (0.0, 0.0),
    "BrickPurple": (0.0, 0.0),
    "Brownstone": (1.0, 0.0),
    "Stone": (2.0, 0.0),
    "Painted": (0.0, 1.0),
}

# Custom primitive data slots on building actors (_facade_plan.frame): the frame M_Facade cuts facades and bays on.
CPD_FRAME = 0
FRAME_PARAMS = ("FrameX", "FrameY", "FrameAMin", "FrameAMax", "FrameBMin", "FrameBMax", "FacadeSeed")
FRAME_INPUTS = ("FX", "FY", "FA0", "FA1", "FB0", "FB1", "FSeed")
NIGHT_FILL = 0.06      # the facade's own glow at night (x its colour, warm): the city-lit overcast on the walls

# Custom primitive data slots on road and sidewalk actors (generate_city.street_frame).
CPD_DIR_X = 0
CPD_DIR_Y = 1
CPD_CENTRE = 2
CPD_HALF = 3


def street_frame(paths, half_width_cm):
    """(dx, dy, centre, half): a road's direction (its longest segment's, made to point +X or, if north-south, +Y), the
    offset of its centre line along the left normal (-dy, dx), and its half width. Paths in world cm."""
    best = None
    for path in paths:
        for (x0, y0), (x1, y1) in zip(path, path[1:]):
            dx, dy = x1 - x0, y1 - y0
            length = (dx * dx + dy * dy) ** 0.5
            if length > 1e-6 and (best is None or length > best[0]):
                best = (length, dx / length, dy / length)
    if best is None:
        return (0.0, 0.0, 0.0, float(half_width_cm))
    _, dx, dy = best
    if dx < -1e-6 or (abs(dx) <= 1e-6 and dy < 0.0):
        dx, dy = -dx, -dy
    points = [p for path in paths for p in path]
    centre = sum(-dy * x + dx * y for x, y in points) / float(len(points))
    return (round(dx, 5), round(dy, 5), round(centre, 1), float(half_width_cm))


# --------------------------------------------------------------------------------------------------------------------
# builders (editor only)
# --------------------------------------------------------------------------------------------------------------------


def _otype(unreal, kind):
    return {
        1: unreal.CustomMaterialOutputType.CMOT_FLOAT1,
        2: unreal.CustomMaterialOutputType.CMOT_FLOAT2,
        3: unreal.CustomMaterialOutputType.CMOT_FLOAT3,
    }[kind]


def custom(m, material, code, inputs, outputs, x=0, y=0, desc="Look"):
    """A Custom expression returning float3, ``inputs`` [(name, node)], ``outputs`` [(name, components)] extra."""
    import unreal
    node = m.expr(material, "MaterialExpressionCustom", x, y, None, "Custom " + desc)
    if node is None:
        return None
    node.set_editor_property("code", code)
    node.set_editor_property("description", desc)
    node.set_editor_property("output_type", _otype(unreal, 3))
    ins = []
    for name, _src in inputs:
        item = unreal.CustomInput()
        item.set_editor_property("input_name", name)
        ins.append(item)
    node.set_editor_property("inputs", ins)
    outs = []
    for name, comps in outputs:
        item = unreal.CustomOutput()
        item.set_editor_property("output_name", name)
        item.set_editor_property("output_type", _otype(unreal, comps))
        outs.append(item)
    node.set_editor_property("additional_outputs", outs)
    for name, src in inputs:
        if not m.connect(src, "", node, name):
            m.c.log("FAILED", desc, "could not wire input " + name)
    return node


def _world_space_normals(m, material):
    m.c.set_props(material, [("tangent_space_normal", False)], material.get_name())


def day_node(m, material, x, y):
    """0 at night, 1 by day: the sky atmosphere light's illuminance (the moon's 0.3 lux, the sun's several). A constant
    0 when the expression is not scriptable."""
    illum = m.expr(material, "MaterialExpressionSkyAtmosphereLightIlluminance", x, y, None, "SkyLightIlluminance")
    if illum is None:
        return m.constant(material, 0.0, x, y)
    lum = m.dot(material, illum, m.constant3(material, (0.3, 0.6, 0.1), x, y + 80), x + 200, y)
    return m.clamp01(material, m.multiply(material, m.add(material, lum, None, x + 350, y, const_b=-0.8), None, x + 500, y,
                                          const_b=0.5), x + 650, y)


def build_facade(m, material, card=False):
    """M_Facade (card False) or M_WindowLit (card True): FACADE_HLSL with its parameters. The facade goes on through
    MF_Snow so roofs take snow; a card does not."""
    import unreal
    _world_space_normals(m, material)
    if card:
        wall = m.constant3(material, m.FACADE_STYLES["BrickRed"][0], -1600, -600)
        trim = m.constant3(material, m.FACADE_STYLES["BrickRed"][1], -1600, -500)
        lit_color = m.vector_param(material, m.EMISSIVE_COLOR_PARAM, m.WINDOW_LIT, -1600, -400)
        lit_fraction = m.constant(material, 1.0, -1600, -300)
        glow = m.scalar_param(material, m.EMISSIVE_INTENSITY_PARAM, 1.0, -1600, -200)
        masonry = m.constant(material, 0.0, -1600, -100)
        painted = m.constant(material, 0.0, -1600, 0)
    else:
        wall = m.vector_param(material, "WallColor", m.FACADE_STYLES["BrickRed"][0], -1600, -600)
        trim = m.vector_param(material, "TrimColor", m.FACADE_STYLES["BrickRed"][1], -1600, -500)
        lit_color = m.vector_param(material, "LitColor", m.WINDOW_LIT, -1600, -400)
        lit_fraction = m.scalar_param(material, "LitFraction", 0.22, -1600, -300)
        glow = m.scalar_param(material, "WindowGlow", m.WINDOW_GLOW * m.EMISSIVE_INTENSITY_FACTOR, -1600, -200)
        masonry = m.scalar_param(material, "Masonry", 0.0, -1600, -100)
        painted = m.scalar_param(material, "Painted", 0.0, -1600, 0)
    inputs = [
        ("WP", m.world_position(material, -1600, 100)),
        ("N", m.expr(material, "MaterialExpressionVertexNormalWS", -1600, 200)),
        ("V", m.expr(material, "MaterialExpressionCameraVectorWS", -1600, 300)),
        ("OP", m.expr(material, "MaterialExpressionObjectPositionWS", -1600, 400)),
        ("OB", m.expr(material, "MaterialExpressionObjectBounds", -1600, 500)),
        ("Wall", wall), ("Trim", trim), ("LitCol", lit_color), ("LitFrac", lit_fraction), ("Glow", glow),
        ("Masonry", masonry), ("Painted", painted),
        ("Day", day_node(m, material, -2300, 700)),
        ("Card", m.constant(material, 1.0 if card else 0.0, -1600, 800)),
        ("Atlas", sign_atlas_node(m, material, -1600, 900)),
    ]
    for i, name in enumerate(FRAME_PARAMS):
        node = m.constant(material, 0.0, -1600, 1000 + 100 * i) if card else _cpd_param(m, material, name, CPD_FRAME + i,
                                                                                         -1600, 1000 + 100 * i)
        inputs.append((FRAME_INPUTS[i], node))
    inputs.append(("NightFill", m.constant(material, 0.0, -1600, 1800) if card else
                   m.scalar_param(material, "NightFill", NIGHT_FILL, -1600, 1800)))
    node = custom(m, material, FACADE_HLSL, inputs, [("Rough", 1), ("Emis", 3), ("NormalWS", 3)], -1000, 0, "Facade")
    m.connect_property(node, unreal.MaterialProperty.MP_NORMAL, "NormalWS")
    if card:
        m.connect_property(node, unreal.MaterialProperty.MP_BASE_COLOR)
        m.connect_property(node, unreal.MaterialProperty.MP_ROUGHNESS, "Rough")
        m.connect_property(node, unreal.MaterialProperty.MP_EMISSIVE_COLOR, "Emis")
        return
    coverage = m._patchy(material, 0.004, -0.2, 2.2, -1000, 400)
    snow = m.function_call(material, m.ensure_snow_function(), -500, 0)
    m.connect(node, "", snow, "BaseColor")
    m.connect(node, "Rough", snow, "Roughness")
    m.connect(coverage, "", snow, "Coverage")
    total = m.add(material, None, None, -200, 200)
    m.connect(node, "Emis", total, "A")
    m.connect(snow, "Emissive", total, "B")
    m.connect_property(snow, unreal.MaterialProperty.MP_BASE_COLOR, "BaseColor")
    m.connect_property(snow, unreal.MaterialProperty.MP_ROUGHNESS, "Roughness")
    m.connect_property(total, unreal.MaterialProperty.MP_EMISSIVE_COLOR)


def sign_atlas_node(m, material, x, y):
    """A TextureObject of T_SignAtlas (imported by ensure_sign_atlas) for the facade's Custom node."""
    texture = ensure_sign_atlas(m)
    node = m.expr(material, "MaterialExpressionTextureObject", x, y, None, "TextureObject SignAtlas")
    if node is not None and texture is not None:
        m.c.set_props(node, [("texture", texture)], "TextureObject SignAtlas")
    return node


def sign_atlas_stamp():
    import hashlib
    import os
    path = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "Data", "look", "sign_atlas.png")
    with open(path, "rb") as source:
        return path, "atlas-1|" + hashlib.sha1(source.read()).hexdigest()[:16]


def ensure_sign_atlas(m):
    """/Game/Materials/T_SignAtlas from Tools/Data/look/sign_atlas.png (bake_sign_atlas.py): linear RGBA8, uncompressed
    (a signed distance per face), imported again only when the file's content changes."""
    import unreal
    path, stamp = sign_atlas_stamp()
    full = m.MATERIALS_PATH + "/" + SIGN_ATLAS
    existing = m.c.load_or_none(full)
    if existing is not None and unreal.EditorAssetLibrary.get_metadata_tag(existing, m.LOOK_TAG) == stamp:
        return existing
    task = unreal.AssetImportTask()
    task.filename = path
    task.destination_path = m.MATERIALS_PATH
    task.destination_name = SIGN_ATLAS
    task.automated = True
    task.replace_existing = True
    task.save = False
    m.c.asset_tools().import_asset_tasks([task])
    texture = m.c.load_or_none(full)
    if texture is None:
        m.c.log("FAILED", full, "the import made no texture")
        return None
    texture.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_VECTOR_DISPLACEMENTMAP)
    texture.set_editor_property("srgb", False)
    texture.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_WORLD)
    unreal.EditorAssetLibrary.set_metadata_tag(texture, m.LOOK_TAG, stamp)
    m.c.save(texture)
    m.c.log("imported", full, stamp)
    return texture


def _cpd_param(m, material, name, index, x, y):
    node = m.scalar_param(material, name, 0.0, x, y)
    if node is not None:
        m.c.set_props(node, [("use_custom_primitive_data", True), ("primitive_data_index", index)], name)
    return node


def build_ground(m, material, kind):
    """M_StreetAsphalt (road), M_SidewalkSnow (sidewalk) or M_ParkSnow (park)."""
    import unreal
    _world_space_normals(m, material)
    sparkle = m.SNOW_SPARKLE * m.EMISSIVE_INTENSITY_FACTOR
    inputs = [
        ("WP", m.world_position(material, -1600, 0)),
        ("N", m.expr(material, "MaterialExpressionVertexNormalWS", -1600, 100)),
        ("Depth", m.expr(material, "MaterialExpressionPixelDepth", -1600, 200)),
        ("Sparkle", m.constant(material, sparkle, -1600, 300)),
    ]
    if kind == "park":
        code = PARK_HLSL
    else:
        code = ROAD_HLSL if kind == "road" else SIDEWALK_HLSL
        inputs += [("DX", _cpd_param(m, material, "StreetDirX", CPD_DIR_X, -1600, 400)),
                   ("DY", _cpd_param(m, material, "StreetDirY", CPD_DIR_Y, -1600, 500)),
                   ("C", _cpd_param(m, material, "StreetCentre", CPD_CENTRE, -1600, 600)),
                   ("H", _cpd_param(m, material, "StreetHalfWidth", CPD_HALF, -1600, 700))]
    node = custom(m, material, code, inputs, [("Rough", 1), ("Emis", 3), ("NormalWS", 3)], -900, 0, kind.title())
    m.connect_property(node, unreal.MaterialProperty.MP_BASE_COLOR)
    m.connect_property(node, unreal.MaterialProperty.MP_ROUGHNESS, "Rough")
    m.connect_property(node, unreal.MaterialProperty.MP_EMISSIVE_COLOR, "Emis")
    m.connect_property(node, unreal.MaterialProperty.MP_NORMAL, "NormalWS")


# --------------------------------------------------------------------------------------------------------------------
# the cloud deck, the street blocks, the cars, road paint (look pass 2)
# --------------------------------------------------------------------------------------------------------------------

# A low overcast on the inside of a sphere round the district (generate_city's City_CloudDeck): unlit, a slow
# drifting texture of cloud bellies projected on a plane overhead, brighter at the zenith by day (an overcast sky's
# luminance, CIE), and by night a dull orange-brown lit from below by the city, brightest at the horizon.
CLOUD_BODY = r"""
float3 d = normalize(-V);
float el = saturate(d.z);
float2 p = d.xy / max(d.z + 0.12, 0.12) * 1.6 + float2(T * 0.0035, T * 0.0018);
float c = L.FBM(p * 1.3) * 0.65 + L.FBM(p * 4.1 + 7.0) * 0.35;
float belly = smoothstep(0.35, 0.75, c);
float3 dayC = lerp(float3(0.70, 0.72, 0.75), float3(0.92, 0.93, 0.95), belly) * DayLum * lerp(0.75, 1.15, el);
float3 nightC = lerp(float3(0.060, 0.034, 0.020), float3(0.095, 0.058, 0.034), 1.0 - belly) * NightLum * lerp(1.35, 0.7, el);
return lerp(nightC, dayC, Day);
"""
CLOUD_HLSL = LIB_HLSL + CLOUD_BODY

# The street blocks that stand in for cover (the parkour test blocks): Kind 0 a concrete jersey barrier with an
# orange and white band, 1 a dark green steel dumpster with vertical ribs and rust, 2 a granite block wall.
BLOCK_BODY = r"""
float3 n = normalize(N);
float3 up = float3(0.0, 0.0, 1.0);
float2 t2 = normalize(float2(-n.y, n.x) + float2(1e-4, 0.0));
float u = dot(WP.xy, t2);
float z = WP.z;
int kind = (int)round(Kind);
float3 col;
float rough;
float3 nrm = n;
float side = 1.0 - step(0.7, abs(n.z));
if (kind == 0)
{
    col = float3(0.42, 0.41, 0.39) * lerp(0.8, 1.1, L.FBM(float2(u, z) / 25.0)) * lerp(0.6, 1.0, saturate((z - 15.0) / 60.0));
    float band = side * step(abs(z - Top + 22.0), 8.0);
    float stripe = step(0.5, frac((u + z) / 40.0));
    col = lerp(col, lerp(float3(0.60, 0.18, 0.02), float3(0.70, 0.70, 0.66), stripe), band);
    rough = lerp(0.9, 0.5, band);
}
else if (kind == 1)
{
    float rib = frac(u / 28.0);
    col = float3(0.025, 0.08, 0.045) * lerp(0.75, 1.1, L.VN(float2(u, z) / 30.0));
    float rust = smoothstep(0.62, 0.72, L.FBM(float2(u, z) / 18.0 + 3.0)) * side;
    col = lerp(col, float3(0.20, 0.08, 0.03), rust);
    col = lerp(col, float3(0.01, 0.01, 0.01), side * step(z, 12.0));
    nrm = normalize(n + side * float3(t2, 0.0) * (rib < 0.12 ? 0.8 : (rib > 0.88 ? -0.8 : 0.0)));
    rough = lerp(0.5, 0.85, rust);
}
else
{
    float2 bc = float2(u / 60.0 + 0.5 * floor(z / 30.0), z / 30.0);
    float j = max(L.Joint(bc.x, fwidth(bc.x), 0.02), L.Joint(bc.y, fwidth(bc.y), 0.04));
    col = float3(0.33, 0.32, 0.31) * lerp(0.8, 1.15, L.H21(floor(bc))) * lerp(0.9, 1.05, L.H21(floor(WP.xy * 0.7 + z)));
    col = lerp(col, float3(0.15, 0.15, 0.15), j);
    rough = 0.8;
}
Rough = rough;
NormalWS = nrm;
return col;
"""
BLOCK_HLSL = LIB_HLSL + BLOCK_BODY

# Parked cars (SM_City_ParkedCar, instanced): local position (cm, +X forward, ground at 0) splits the box car into
# tyres, bumpers, painted body with a trim line, glass round the cabin, a roof; lamps front and back.
CAR_BODY = r"""
float3 n = normalize(N);
float zl = LP.z;
float3 col = Paint;
float rough = 0.28;
float3 emis = float3(0.0, 0.0, 0.0);
float glassLo = Shape1.x;
float glassHi = Shape1.y;
float halfLen = Shape1.z;
float lampZ = Shape2.x;
float wb = Shape2.y;
float wr = Shape2.z;
float halfW = Shape3.x;
float2 wq = float2(abs(LP.x) - wb, zl - wr);
bool wheel = length(wq) < wr + 1.0 && abs(LP.y) > halfW - 26.0;
if (zl < 30.0 || wheel)
{
    bool rim = wheel && length(wq) < wr * 0.58 && abs(n.z) < 0.5;
    col = rim ? float3(0.22, 0.22, 0.23) : float3(0.02, 0.02, 0.022);
    rough = rim ? 0.35 : 0.8;
}
else if (zl < 42.0) { col = float3(0.035, 0.035, 0.038); rough = 0.6; }
else if (zl > glassLo && zl < glassHi && abs(n.z) < 0.85)
{
    // glass, with the pillars between the side windows in the body colour
    bool pillar = abs(n.y) > 0.7 && (abs(LP.x - Shape3.y) < 6.0 || (Shape3.z != 0.0 && abs(LP.x - Shape3.z) < 7.0));
    col = pillar ? Paint * 0.8 : float3(0.015, 0.017, 0.02);
    rough = pillar ? 0.3 : 0.05;
}
else if (!wheel && length(wq) < wr + 8.0 && abs(n.y) > 0.7) { col = float3(0.008, 0.008, 0.009); rough = 0.9; }
if (abs(zl - (glassLo - 28.0)) < 1.5 && abs(n.z) < 0.6 && !wheel) { col = float3(0.15, 0.15, 0.16); }
if (abs(LP.x) > halfLen && abs(zl - lampZ) < 7.0 && abs(LP.y) > halfW - 45.0)
{
    bool front = LP.x > 0.0;
    col = front ? float3(0.75, 0.75, 0.72) : float3(0.45, 0.02, 0.02);
    emis = (front ? float3(1.0, 0.95, 0.85) : float3(1.0, 0.05, 0.03)) * Glow * (1.0 - Day) * 0.3;
}
Rough = rough;
Emis = emis;
return col;
"""
CAR_HLSL = LIB_HLSL + CAR_BODY

# Road paint (crosswalk bars): white thermoplastic, worn through to the asphalt where the tyres run, grey with grime.
PAINT_BODY = r"""
float wear = smoothstep(0.55, 0.75, L.FBM(WP.xy / 35.0));
float3 col = lerp(float3(0.62, 0.62, 0.60), float3(0.05, 0.05, 0.055), wear * 0.8) * lerp(0.8, 1.0, L.VN(WP.xy / 8.0));
Rough = lerp(0.55, 0.2, wear);
return col;
"""
PAINT_HLSL = LIB_HLSL + PAINT_BODY


def _unlit(m, material):
    import unreal
    m.c.set_props(material, [("shading_model", unreal.MaterialShadingModel.MSM_UNLIT), ("two_sided", True)],
                  material.get_name())


def build_cloud_deck(m, material):
    """M_CloudDeck: CLOUD_HLSL, unlit, two-sided, DayLum / NightLum parameters."""
    import unreal
    _unlit(m, material)
    inputs = [
        ("V", m.expr(material, "MaterialExpressionCameraVectorWS", -1600, 0)),
        ("T", m.time_node(material, -1600, 100)),
        ("Day", day_node(m, material, -2300, 300)),
        ("DayLum", m.scalar_param(material, "DayLuminance", CLOUD_DAY, -1600, 400)),
        ("NightLum", m.scalar_param(material, "NightLuminance", CLOUD_NIGHT, -1600, 500)),
    ]
    node = custom(m, material, CLOUD_HLSL, inputs, [], -900, 0, "CloudDeck")
    m.connect_property(node, unreal.MaterialProperty.MP_EMISSIVE_COLOR)


def _lit_with_snow(m, material, node, x=-400):
    import unreal
    snow = m.function_call(material, m.ensure_snow_function(), x, 0)
    m.connect(node, "", snow, "BaseColor")
    m.connect(node, "Rough", snow, "Roughness")
    m.connect(m.constant(material, 1.0, x - 250, 150), "", snow, "Coverage")
    m.connect_property(snow, unreal.MaterialProperty.MP_BASE_COLOR, "BaseColor")
    m.connect_property(snow, unreal.MaterialProperty.MP_ROUGHNESS, "Roughness")
    return snow


def build_street_block(m, material):
    """M_StreetBlock: BLOCK_HLSL with Kind and Top (cm, the block's top), snow on top."""
    import unreal
    _world_space_normals(m, material)
    inputs = [
        ("WP", m.world_position(material, -1600, 0)),
        ("N", m.expr(material, "MaterialExpressionVertexNormalWS", -1600, 100)),
        ("Kind", m.scalar_param(material, "Kind", 0.0, -1600, 200)),
        ("Top", m.scalar_param(material, "Top", 90.0, -1600, 300)),
    ]
    node = custom(m, material, BLOCK_HLSL, inputs, [("Rough", 1), ("NormalWS", 3)], -900, 0, "StreetBlock")
    m.connect_property(node, unreal.MaterialProperty.MP_NORMAL, "NormalWS")
    snow = _lit_with_snow(m, material, node)
    m.connect_property(snow, unreal.MaterialProperty.MP_EMISSIVE_COLOR, "Emissive")


def build_car(m, material):
    """M_Car: CAR_HLSL, Color, snow on the roof and bonnet, lamps lit at night."""
    import unreal
    inputs = [
        ("LP", m.expr(material, "MaterialExpressionLocalPosition", -1600, 0)),
        ("N", m.expr(material, "MaterialExpressionVertexNormalWS", -1600, 100)),
        ("Paint", m.vector_param(material, "Color", (0.1, 0.1, 0.11), -1600, 200)),
        # the box car's numbers by default (SM_City_ParkedCar); _street_look.CAR_SHAPES for the others
        ("Shape1", m.vector_param(material, "Shape1", (100.0, 132.0, 222.0), -1600, 500)),
        ("Shape2", m.vector_param(material, "Shape2", (69.0, 140.0, 33.0), -1600, 600)),
        ("Shape3", m.vector_param(material, "Shape3", (90.0, 0.0, 0.0), -1600, 700)),
        ("Glow", m.constant(material, m.EMISSIVE_INTENSITY_FACTOR, -1600, 300)),
        ("Day", day_node(m, material, -2300, 400)),
    ]
    node = custom(m, material, CAR_HLSL, inputs, [("Rough", 1), ("Emis", 3)], -900, 0, "Car")
    snow = _lit_with_snow(m, material, node)
    total = m.add(material, None, None, -150, 250)
    m.connect(node, "Emis", total, "A")
    m.connect(snow, "Emissive", total, "B")
    m.connect_property(total, unreal.MaterialProperty.MP_EMISSIVE_COLOR)


def build_paint(m, material):
    """M_RoadPaint: PAINT_HLSL."""
    import unreal
    inputs = [("WP", m.world_position(material, -1600, 0))]
    node = custom(m, material, PAINT_HLSL, inputs, [("Rough", 1)], -900, 0, "RoadPaint")
    m.connect_property(node, unreal.MaterialProperty.MP_BASE_COLOR)
    m.connect_property(node, unreal.MaterialProperty.MP_ROUGHNESS, "Rough")


# --------------------------------------------------------------------------------------------------------------------
# look pass 3: manholes, steam, snow piles, street signs, bark
# --------------------------------------------------------------------------------------------------------------------

# A manhole (SM_Street_Manhole, a 75 cm disc in local cm, masked): a cast-iron cover in its frame, and round it the
# asphalt the steam under it keeps clear of snow, wet and black, with a ragged edge.
MANHOLE_BODY = r"""
float r = length(LP.xy);
float a = atan2(LP.y, LP.x);
float3 col;
float rough;
Mask = 1.0;
if (r < 33.0)
{
    float ring = frac(r / 6.5) < 0.14 ? 0.55 : 1.0;
    float2 g = frac(LP.xy / 5.0);
    float stud = (r > 6.0 && g.x < 0.32 && g.y < 0.32) ? 1.35 : 1.0;
    col = float3(0.04, 0.037, 0.034) * ring * stud * lerp(0.75, 1.25, L.VN(LP.xy / 9.0));
    col = lerp(col, float3(0.13, 0.06, 0.03), 0.4 * smoothstep(0.6, 0.8, L.FBM(LP.xy / 12.0)));
    rough = 0.3;
}
else if (r < 37.0)
{
    col = float3(0.06, 0.055, 0.05);
    rough = 0.35;
}
else
{
    col = float3(0.028, 0.028, 0.03) * lerp(0.8, 1.15, L.FBM(WP.xy / 25.0));
    rough = 0.1;
    float edge = 50.0 + 22.0 * L.FBM(float2(a * 2.5, 1.7) + LP.xy / 22.0);
    Mask = r < edge ? 1.0 : 0.0;
}
Rough = rough;
return col;
"""
MANHOLE_HLSL = LIB_HLSL + MANHOLE_BODY

# Steam from a manhole (SM_Street_Steam, an open cone 3 m tall in local cm, unlit translucent): puffs of a rising
# noise, soft at the silhouette and the base, thinning as they climb; grey-white by day, a dull warm grey at night.
STEAM_BODY = r"""
float h = saturate(LP.z / 320.0);
float2 p = float2(atan2(LP.y, LP.x) * 1.3 + LP.z / 90.0, LP.z / 45.0 - T * 0.8);
float puff = L.FBM(p) * 0.7 + L.FBM(p * 2.3 + 5.0) * 0.3;
float fres = saturate(abs(dot(normalize(N), normalize(V))));
float a = smoothstep(0.38, 0.78, puff) * fres * fres * (1.0 - h) * saturate(LP.z / 30.0);
Opacity = a * lerp(0.45, 0.6, Day);
return lerp(float3(0.07, 0.06, 0.055), float3(0.85, 0.86, 0.88), Day) * Lum;
"""
STEAM_HLSL = LIB_HLSL + STEAM_BODY

# Shovelled and plowed snow (SM_Street_SnowPiles, world cm): clean on top, grey-brown with road dirt low down and on
# the sides, lumpy in the normal.
SNOWPILE_BODY = r"""
float3 n = normalize(N);
float top = smoothstep(0.25, 0.75, n.z);
float low = saturate(1.0 - (WP.z - 4.0) / 45.0);
float dirt = saturate((0.45 + low * 0.8 + (1.0 - top) * 0.5) * lerp(0.5, 1.3, L.FBM(WP.xy / 35.0 + WP.z / 20.0)) - 0.15);
float3 snowC = float3(0.62, 0.62, 0.63) * lerp(0.85, 1.0, L.VN(WP.xy / 18.0));
float3 dirtyC = float3(0.15, 0.13, 0.105) * lerp(0.6, 1.2, L.FBM(WP.xy / 12.0 + WP.z / 9.0));
float3 col = lerp(snowC, dirtyC, dirt);
float2 q = WP.xy / 14.0 + WP.z / 30.0;
float b0 = L.FBM(q);
float bx = L.FBM(q + float2(0.1, 0.0));
float by = L.FBM(q + float2(0.0, 0.1));
NormalWS = normalize(n + float3(b0 - bx, b0 - by, 0.0) * 1.2);
Rough = lerp(0.7, 0.85, dirt);
Emis = step(0.9993, L.H21(floor(WP.xy / 2.0) + floor(WP.z / 2.0))) * top * (1.0 - dirt) * saturate(1.0 - Depth / 1200.0) * Sparkle;
return col;
"""
SNOWPILE_HLSL = LIB_HLSL + SNOWPILE_BODY

# Street signs (SM_Street_SignPlate: 2 x 46 x 60 cm, the face toward local +X; LocalPosition y across, z up): Kind 0 a
# no-parking plate (white, red border, red NO and PARKING, rows of small print), 1 a bus stop (white, blue band,
# BUS STOP), 2 a one-way arrow (black plate, white arrow, ONE WAY on it). Lettering from the sign atlas (Roboto Bold).
SIGN_TEXTS = ["NO", "PARKING", "BUS", "STOP", "ONE WAY"]


def sign_text_hlsl():
    import _sign_font as sf
    chars, starts, lengths = [], [], []
    for text in SIGN_TEXTS:
        starts.append(len(chars))
        lengths.append(len(text))
        chars.extend(sf.GLYPHS.index(ch) for ch in text)

    def arr(kind, name, values, fmt="{0}"):
        return "static const {0} {1}[{2}] = {{{3}}};".format(kind, name, len(values), ", ".join(fmt.format(v) for v in values))
    return "\n".join([
        arr("uint", "T_CHARS", chars), arr("uint", "T_START", starts), arr("uint", "T_LEN", lengths),
        arr("float", "T_ADV", sf.ADVANCE[0], "{0:.5f}"),
        "static const float T_GRID = {0:.1f};".format(sf.GRID),
        "static const float T_ORIGIN = {0:.6f};".format(sf.ORIGIN_X / float(sf.CELL)),
        "static const float T_CAP = {0:.6f};".format(sf.CAP / float(sf.CELL)),
        "static const float T_SDF = {0:.1f};".format(2.0 * sf.SPREAD),
        "static const float T_CELL = {0:.1f};".format(sf.CELL),
    ]) + "\n"


SIGN_BODY = r"""
float3 n = normalize(N);
float x = -LP.y;          // seen from the face (local +X), the plate's right is local -Y
float y = LP.z;
bool front = LP.x > 0.5;
int kind = (int)round(Kind);
float3 col = float3(0.35, 0.35, 0.36);
float rough = 0.5;
float fw = max(fwidth(x), fwidth(y));
if (front)
{
    // Text(t, centre y, cap height): the string laid out on its advances, centred on x = 0; returns ink 0..1.
    float ink0 = 0.0;
    float ink1 = 0.0;
    int t0 = kind == 0 ? 0 : (kind == 1 ? 2 : 4);
    int t1 = kind == 0 ? 1 : (kind == 1 ? 3 : -1);
    float cy0 = kind == 0 ? 12.0 : (kind == 1 ? 8.0 : 0.0);
    float cap0 = kind == 0 ? 13.0 : (kind == 1 ? 10.0 : 5.0);
    float cy1 = kind == 0 ? -4.0 : -6.0;
    float cap1 = kind == 0 ? 6.0 : 8.0;
    [unroll] for (int ln = 0; ln < 2; ln++)
    {
        int tt = ln == 0 ? t0 : t1;
        if (tt < 0) { continue; }
        float cap = ln == 0 ? cap0 : cap1;
        float cy = ln == 0 ? cy0 : cy1;
        float kc = cap / T_CAP;
        uint st = T_START[tt];
        int len = (int)T_LEN[tt];
        float wsum = 0.0;
        [loop] for (int i = 0; i < len; i++) { wsum += T_ADV[T_CHARS[st + (uint)i]]; }
        float qx = x / kc + 0.5 * wsum;
        float qy = 0.5 - (y - cy) / kc;
        float pen = 0.0;
        float sd = 0.0;
        [loop] for (int j = 0; j < len; j++)
        {
            uint g = T_CHARS[st + (uint)j];
            float2 c = float2(qx - pen + T_ORIGIN, qy);
            if (c.x >= 0.0 && c.x <= 1.0 && c.y >= 0.0 && c.y <= 1.0)
            {
                float2 gc = float2(fmod((float)g, T_GRID), floor((float)g / T_GRID));
                sd = max(sd, Atlas.SampleLevel(AtlasSampler, (gc + c) / T_GRID, 0.0).r);
            }
            pen += T_ADV[g];
        }
        float aa = 0.5 * (fw / kc * T_CELL) / T_SDF + 0.01;
        float ink = smoothstep(0.5 - aa, 0.5 + aa, sd);
        if (ln == 0) { ink0 = ink; } else { ink1 = ink; }
    }
    float border = min(22.0 - abs(x), 29.0 - abs(y));
    if (kind == 0)
    {
        col = float3(0.75, 0.74, 0.70);
        col = border < 1.6 ? float3(0.45, 0.03, 0.03) : col;
        col = lerp(col, float3(0.45, 0.03, 0.03), max(ink0, ink1));
        float rows = step(abs(x), 16.0) * step(-26.0, y) * step(y, -12.0) * step(frac(y / 2.6), 0.35) * step(0.3, L.H21(float2(floor(x / 1.3), floor(y / 2.6))));
        col = lerp(col, float3(0.05, 0.05, 0.05), rows);
    }
    else if (kind == 1)
    {
        col = y > 19.0 ? float3(0.03, 0.10, 0.40) : float3(0.75, 0.74, 0.70);
        col = border < 1.2 ? float3(0.03, 0.10, 0.40) : col;
        col = lerp(col, float3(0.03, 0.10, 0.40), max(ink0, ink1));
    }
    else
    {
        col = float3(0.02, 0.02, 0.022);
        float body = step(abs(y), 6.0) * step(-19.0, x) * step(x, 12.0);
        float head = step(12.0, x) * step(x, 21.0) * step(abs(y), (21.0 - x) * 1.2);
        float arrow = max(body, head);
        col = lerp(col, float3(0.75, 0.74, 0.70), arrow);
        col = lerp(col, float3(0.02, 0.02, 0.022), ink0 * arrow);
    }
    col *= lerp(0.85, 1.0, L.VN(float2(x, y) / 6.0));
    rough = 0.35;
}
Rough = rough;
return col;
"""
SIGN_HLSL = LIB_HLSL + sign_text_hlsl() + SIGN_BODY

# Bark on the street trees and the park's elms (SM_Street_Tree_<n>): grey-brown, fissured along the limb, darker
# and wetter low down, and snow lying along the upper side of every branch and twig.
BARK_BODY = r"""
float3 n = normalize(N);
float fis = L.VN(float2(dot(WP.xy, float2(0.7, 0.7)) / 3.0, WP.z / 40.0));
float3 col = float3(0.07, 0.062, 0.055) * lerp(0.6, 1.25, fis) * lerp(0.8, 1.1, L.VN(WP.xy / 30.0 + WP.z / 50.0));
col *= lerp(0.6, 1.0, saturate((WP.z - 20.0) / 120.0));
float snow = smoothstep(0.32, 0.62, n.z + 0.15 * (L.VN(WP.xy / 6.0 + WP.z / 6.0) - 0.5)) * step(60.0, WP.z);
col = lerp(col, float3(0.62, 0.62, 0.63) * lerp(0.85, 1.0, L.VN(WP.xy / 5.0)), snow);
Rough = lerp(0.9, 0.65, snow);
return col;
"""
BARK_HLSL = LIB_HLSL + BARK_BODY


def _blend(m, material, mode, two_sided=False, unlit=False):
    import unreal
    props = [("blend_mode", mode), ("two_sided", two_sided)]
    if unlit:
        props.append(("shading_model", unreal.MaterialShadingModel.MSM_UNLIT))
    m.c.set_props(material, props, material.get_name())


def build_manhole(m, material):
    """M_Manhole: MANHOLE_HLSL, masked."""
    import unreal
    _blend(m, material, unreal.BlendMode.BLEND_MASKED)
    inputs = [("WP", m.world_position(material, -1600, 0)),
              ("LP", m.expr(material, "MaterialExpressionLocalPosition", -1600, 100))]
    node = custom(m, material, MANHOLE_HLSL, inputs, [("Rough", 1), ("Mask", 1)], -900, 0, "Manhole")
    m.connect_property(node, unreal.MaterialProperty.MP_BASE_COLOR)
    m.connect_property(node, unreal.MaterialProperty.MP_ROUGHNESS, "Rough")
    m.connect_property(node, unreal.MaterialProperty.MP_OPACITY_MASK, "Mask")


def build_steam(m, material):
    """M_Steam: STEAM_HLSL, unlit translucent, two-sided."""
    import unreal
    _blend(m, material, unreal.BlendMode.BLEND_TRANSLUCENT, two_sided=True, unlit=True)
    inputs = [("LP", m.expr(material, "MaterialExpressionLocalPosition", -1600, 0)),
              ("N", m.expr(material, "MaterialExpressionVertexNormalWS", -1600, 100)),
              ("V", m.expr(material, "MaterialExpressionCameraVectorWS", -1600, 200)),
              ("T", m.time_node(material, -1600, 300)),
              ("Day", day_node(m, material, -2300, 400)),
              ("Lum", m.scalar_param(material, "Luminance", STEAM_LUM, -1600, 500))]
    node = custom(m, material, STEAM_HLSL, inputs, [("Opacity", 1)], -900, 0, "Steam")
    m.connect_property(node, unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    m.connect_property(node, unreal.MaterialProperty.MP_OPACITY, "Opacity")


def build_snow_pile(m, material):
    """M_SnowPile: SNOWPILE_HLSL."""
    import unreal
    _world_space_normals(m, material)
    inputs = [("WP", m.world_position(material, -1600, 0)),
              ("N", m.expr(material, "MaterialExpressionVertexNormalWS", -1600, 100)),
              ("Depth", m.expr(material, "MaterialExpressionPixelDepth", -1600, 200)),
              ("Sparkle", m.constant(material, m.SNOW_SPARKLE * m.EMISSIVE_INTENSITY_FACTOR, -1600, 300))]
    node = custom(m, material, SNOWPILE_HLSL, inputs, [("Rough", 1), ("Emis", 3), ("NormalWS", 3)], -900, 0, "SnowPile")
    m.connect_property(node, unreal.MaterialProperty.MP_BASE_COLOR)
    m.connect_property(node, unreal.MaterialProperty.MP_ROUGHNESS, "Rough")
    m.connect_property(node, unreal.MaterialProperty.MP_EMISSIVE_COLOR, "Emis")
    m.connect_property(node, unreal.MaterialProperty.MP_NORMAL, "NormalWS")


def build_street_sign(m, material):
    """M_StreetSign: SIGN_HLSL with Kind (the plate's face is its local +X side), lettering from the atlas."""
    import unreal
    inputs = [("LP", m.expr(material, "MaterialExpressionLocalPosition", -1600, 0)),
              ("N", m.expr(material, "MaterialExpressionVertexNormalWS", -1600, 100)),
              ("Kind", m.scalar_param(material, "Kind", 0.0, -1600, 300)),
              ("Atlas", sign_atlas_node(m, material, -1600, 400))]
    node = custom(m, material, SIGN_HLSL, inputs, [("Rough", 1)], -900, 0, "StreetSign")
    m.connect_property(node, unreal.MaterialProperty.MP_BASE_COLOR)
    m.connect_property(node, unreal.MaterialProperty.MP_ROUGHNESS, "Rough")


def build_bark(m, material):
    """M_TreeBark: BARK_HLSL."""
    import unreal
    inputs = [("WP", m.world_position(material, -1600, 0)),
              ("N", m.expr(material, "MaterialExpressionVertexNormalWS", -1600, 100))]
    node = custom(m, material, BARK_HLSL, inputs, [("Rough", 1)], -900, 0, "Bark")
    m.connect_property(node, unreal.MaterialProperty.MP_BASE_COLOR)
    m.connect_property(node, unreal.MaterialProperty.MP_ROUGHNESS, "Rough")


STEAM_LUM = 1.0


CLOUD_DAY = 3.0       # the deck's luminance by day and night, against the fixed exposure (EV100 1; bias 0 / +2)
CLOUD_NIGHT = 1.0

def _atlas_digest():
    try:
        return sign_atlas_stamp()[1]
    except OSError:
        return "no-atlas"


def _source_hash():
    import hashlib
    text = "\n".join((FACADE_HLSL, ROAD_HLSL, SIDEWALK_HLSL, PARK_HLSL, CLOUD_HLSL, BLOCK_HLSL, CAR_HLSL, PAINT_HLSL,
                      MANHOLE_HLSL, STEAM_HLSL, SNOWPILE_HLSL, SIGN_HLSL, BARK_HLSL,
                      repr((CLOUD_DAY, CLOUD_NIGHT, NIGHT_FILL, STEAM_LUM)), _atlas_digest()))
    return hashlib.sha1(text.encode("utf-8")).hexdigest()[:10]


SOURCE_HASH = _source_hash()
