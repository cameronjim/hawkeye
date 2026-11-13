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
# the facade
# --------------------------------------------------------------------------------------------------------------------

# Inputs: WP (world position, cm), N (vertex normal), V (camera vector), OP / OB (object bounds centre and size),
# Wall, Trim, LitCol (float3), LitFrac, Glow, Masonry (0 brick, 1 brownstone, 2 concrete), Painted (0..1), Day (0 at
# night, 1 by day), Card (1 on a dressing window card: always a lit window). Outputs: the base colour, Rough, Emis,
# NormalWS (world space; the material has tangent-space normals off).
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

// The building's own colour inside its style's range.
float3 wall = Wall * lerp(0.78, 1.2, bs);
wall *= lerp(float3(0.93, 0.96, 1.06), float3(1.08, 0.97, 0.88), bs2);

int mode = (int)round(Masonry);
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
    // board marks and tie holes on the concrete
    float board = frac(z / 15.0);
    col *= lerp(1.0, (board < 0.06 ? 0.9 : 1.0) * lerp(0.94, 1.04, L.H21(float2(floor(z / 15.0), floor(u / 160.0)))), detail);
}

// Paint over brick, peeling here and there.
float peel = smoothstep(0.66, 0.72, L.FBM(float2(u, z) / 45.0 + bs * 10.0));
float3 paintCol = bs < 0.3 ? float3(0.62, 0.58, 0.50) : (bs < 0.55 ? Wall : (bs < 0.8 ? float3(0.12, 0.20, 0.15) : float3(0.55, 0.52, 0.47)));
paintCol *= (1.0 - 0.18 * mortar);
float3 under = lerp(float3(0.30, 0.11, 0.075) * var, float3(0.36, 0.345, 0.31), mortar);
col = lerp(col, lerp(paintCol, under, peel), Painted);

// Weather: blotches, a dirty base with a salt line, soot under the top.
col *= lerp(0.76, 1.06, L.FBM(float2(u, z) / 240.0 + bs * 3.0));
col *= lerp(0.55, 1.0, sqrt(saturate((z - 15.0) / 280.0)));
float salt = saturate(1.0 - abs(z - 48.0) / 26.0) * smoothstep(0.35, 0.6, L.VN(float2(u / 30.0, 1.7)));
col = lerp(col, float3(0.50, 0.49, 0.46), 0.4 * salt);
col *= lerp(0.72, 1.0, saturate(rel / 320.0));
float rough = lerp(0.88, 0.94, mortar);
rough = lerp(rough, 0.62, Painted * (1.0 - peel));

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
    float k = (mode == 2 ? 0.3 : 0.8) * saturate(1.5 - fwc / 0.8) * (1.0 - 0.6 * Painted);
    float2 sp = float2(u, z) / (mode == 0 ? 2.5 : 6.0);
    float s0 = L.VN(sp);
    float su = L.VN(sp + float2(0.35, 0.0));
    float sv = L.VN(sp + float2(0.0, 0.35));
    float ks = 1.2 * saturate(1.5 - fwc / 1.5);
    float GU = gu * k + (su - s0) * ks;
    float GV = gv * k + (sv - s0) * ks;
    nrm = normalize(n - (GU * T + GV * up) * vert);
}

float3 emis = float3(0.0, 0.0, 0.0);
float snow = 0.0;
float3 trim = Trim * lerp(0.9, 1.1, bs2);
float3 rv = -normalize(V);
float3 r = float3(dot(rv, T), rv.z, max(dot(rv, -outN), 0.03));

// Upper floors: the window grid.
float upper = step(420.0, z) * step(165.0, rel) * step(0.5, vert);
if (Card > 0.5)
{
    upper = 1.0;
}
float bayU = u / 160.0;
float bay = floor(bayU);
float lu = (frac(bayU) - 0.5) * 160.0;
float storey = floor(z / 330.0);
float lz = z - storey * 330.0 - 95.0;
float wseed = L.H21(float2(bay, storey) + floor(dpl / 100.0) * 1.37);
float3 frameCol = bs2 < 0.45 ? float3(0.62, 0.60, 0.55) : (bs2 < 0.8 ? float3(0.025, 0.025, 0.028) : float3(0.05, 0.12, 0.08));
if (upper > 0.5)
{
    if (abs(lu) < 66.0 && lz > 150.0 && lz < 172.0)
    {
        col = trim * lerp(0.85, 1.05, L.VN(float2(u, z) / 8.0)) * lerp(0.7, 1.0, saturate((lz - 150.0) / 6.0));
        rough = 0.8;
        nrm = n;
        if (lz > 168.5)
        {
            snow = 1.0;
            nrm = normalize(n + up * 2.0);
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
        // the reveal: masonry sides and head, the stone sill's inner face
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

// The storefronts under the sign band, on the taller buildings.
float ground = tall * step(0.5, vert) * step(z, 395.0);
if (ground > 0.5)
{
    float sU = u / 320.0;
    float shop = floor(sU);
    float su = (frac(sU) - 0.5) * 320.0;
    float sseed = L.H21(float2(shop, floor(dpl / 100.0)) + 0.37);
    if (z > 300.0)
    {
        float3 signCol = sseed < 0.25 ? float3(0.40, 0.04, 0.03) : (sseed < 0.5 ? float3(0.03, 0.12, 0.06) : (sseed < 0.75 ? float3(0.03, 0.04, 0.12) : float3(0.42, 0.40, 0.36)));
        float panel = step(abs(su), 150.0) * step(310.0, z) * step(z, 385.0);
        col = lerp(float3(0.03, 0.03, 0.035), signCol, panel);
        float lw = lerp(70.0, 125.0, L.H21(float2(shop, 1.9)));
        float lc = floor(su / 11.0);
        float lh = lerp(9.0, 15.0, L.H21(float2(lc, shop + 0.5)));
        float lf = frac(su / 11.0);
        float stroke = step(lf, 0.18) + step(0.82, lf) + step(abs(z - 347.0), 2.0) * step(0.4, L.H21(float2(lc, shop + 2.5))) + step(abs(z - 347.0 - lh + 2.0), 2.0) * step(0.3, L.H21(float2(lc, shop + 3.5)));
        float letters = panel * step(abs(su), lw) * step(abs(z - 347.0), lh) * saturate(stroke) * step(0.12, L.H21(float2(lc, shop)));
        float3 ink = sseed < 0.75 ? float3(0.80, 0.78, 0.70) : float3(0.05, 0.05, 0.05);
        col = lerp(col, ink, letters);
        rough = 0.45;
        nrm = n;
        emis += col * Glow * 0.5 * panel * (1.0 - Day) * step(0.4, sseed);
        if (z > 391.0)
        {
            snow = 1.0;
            nrm = normalize(n + up * 2.0);
        }
    }
    else if (z > 40.0 && abs(su) < 128.0)
    {
        float open = max(step(L.H21(float2(sseed, 2.1)), LitFrac + 0.2), step(0.5, Day));
        if (open < 0.5)
        {
            // the roll-down gate, ribbed, tagged
            float rib = frac((z - 40.0) / 7.0);
            col = float3(0.30, 0.30, 0.31) * lerp(0.7, 1.05, L.VN(float2(su / 40.0, z / 60.0) + sseed * 9.0)) * (rib < 0.15 ? 0.6 : 1.0);
            nrm = normalize(n + up * (rib < 0.15 ? -0.6 : (rib > 0.85 ? 0.6 : 0.0)));
            rough = 0.45;
            float tag = smoothstep(0.6, 0.66, L.FBM(float2(su, z) / 35.0 + sseed * 20.0));
            col = lerp(col, lerp(float3(0.45, 0.08, 0.35), float3(0.08, 0.25, 0.55), L.H21(float2(sseed, 3.0))), tag * 0.8);
        }
        else
        {
            float tg = 8.0 / r.z;
            float2 g = float2(su + r.x * tg, (z - 40.0) + r.y * tg);
            float pane = step(abs(g.x), 120.0) * step(5.0, g.y) * step(g.y, 255.0) * (1.0 - step(abs(g.x - 20.0), 3.0)) * (1.0 - step(abs(g.y - 210.0), 3.0));
            nrm = n;
            if (pane < 0.5)
            {
                col = float3(0.03, 0.03, 0.03);
                rough = 0.4;
            }
            else
            {
                col = float3(0.012, 0.013, 0.015);
                rough = 0.05;
                float light = 1.0;
                float3 inside = L.Room(g, r, 300.0, 0.0, -40.0, 260.0, 650.0, sseed, 1.0, light);
                emis = inside * light * Glow * 1.3 * lerp(float3(0.9, 0.95, 1.0), LitCol, 0.3);
            }
        }
    }
}

// Cornice with dentils under the parapet, its shadow under it.
if (tall > 0.5 && vert > 0.5 && rel > 90.0 && rel < 145.0)
{
    float k = (rel - 90.0) / 55.0;
    float dent = step(0.75, k) * step(0.45, frac(u / 16.0));
    col = trim * lerp(1.0, 0.7, k) * (1.0 - 0.35 * dent) * lerp(0.85, 1.05, L.VN(float2(u / 30.0, rel / 10.0)));
    rough = 0.7;
    nrm = normalize(n + up * (k < 0.15 ? 1.5 : (k > 0.6 ? -0.8 : 0.0)));
}
if (tall > 0.5 && vert > 0.5 && rel >= 145.0 && rel < 158.0)
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
Rough = rough;
Emis = emis;
NormalWS = nrm;
return col;
"""

FACADE_HLSL = LIB_HLSL + FACADE_BODY

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
float crack = (1.0 - smoothstep(0.0, 0.015 + fw * 0.002, cr)) * saturate(1.5 - fw / 2.0);
asph *= 1.0 - 0.6 * crack;

// Snow: wheel tracks worn to wet asphalt, compacted snow and slush between them, a plowed ridge at the kerb.
float park = hasFrame * step(halfW - 250.0, a);
float drive = max(halfW - 250.0, 165.0);
float laneW = 330.0;
float lanes = max(1.0, floor(2.0 * drive / laneW));
float x = across + 0.5 * lanes * laneW;
float lx = frac(x / laneW) * laneW - 0.5 * laneW;
float wob = (L.VN(float2(along / 700.0, floor(x / laneW) * 3.0)) - 0.5) * 60.0;
float trackD = min(abs(lx - wob - 80.0), abs(lx - wob + 80.0));
float track = 1.0 - smoothstep(16.0, 40.0, trackD + (L.VN(float2(along / 60.0, lx / 20.0)) - 0.5) * 20.0);
float sn = L.FBM(float2(along, across) / 120.0);
float cover = saturate(0.5 + 0.9 * (sn - 0.5));
cover = lerp(cover, 0.03, track * (1.0 - park));
float ridge = hasFrame * smoothstep(halfW - 110.0, halfW - 15.0, a);
cover = max(cover, ridge);
cover = lerp(cover, saturate(cover + 0.35), park);
if (hasFrame < 0.5)
{
    cover = saturate(0.35 + 0.8 * (sn - 0.5));
}
float slushy = saturate(1.0 - abs(cover - 0.45) * 3.0);
float3 snowC = float3(0.60, 0.60, 0.61) * lerp(0.78, 1.0, L.VN(WP.xy / 40.0));
float3 slushC = float3(0.20, 0.19, 0.17);
float3 col = lerp(asph, slushC, saturate(cover * 2.0));
col = lerp(col, snowC, saturate(cover * 2.0 - 1.0));
col = lerp(col, float3(0.32, 0.30, 0.27), ridge * 0.6 * L.VN(WP.xy / 60.0));
float wet = 1.0 - saturate(cover * 2.0);
float rough = lerp(0.7, 0.1, wet * lerp(0.55, 1.0, L.VN(WP.xy / 300.0)));
rough = lerp(rough, 0.3, slushy);
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

// Snow: a trodden path down the middle, banks at the building line and the kerb, salt and wet where it is clear.
float path = hasFrame * (1.0 - plaza) * smoothstep(40.0, 110.0, ka) * (1.0 - smoothstep(280.0, 360.0, ka));
float sn = L.FBM(float2(along, ka) / 90.0);
float bank = hasFrame * (1.0 - plaza) * (smoothstep(330.0, 390.0, ka) + (1.0 - smoothstep(20.0, 45.0, ka)) * (1.0 - kerbTop));
float cover = saturate(0.5 + (sn - 0.5) * 1.6 - path * 0.75 + bank * 0.6);
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
float3 nrm = normalize(up + float3(b0 - bx, b0 - by, 0.0) * 1.6 * trod);
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
    ]
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


def _source_hash():
    import hashlib
    text = "\n".join((FACADE_HLSL, ROAD_HLSL, SIDEWALK_HLSL, PARK_HLSL))
    return hashlib.sha1(text.encode("utf-8")).hexdigest()[:10]


SOURCE_HASH = _source_hash()
