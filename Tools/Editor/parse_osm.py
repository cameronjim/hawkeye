"""Turn a raw Overpass download into the records generate_city.py builds from.

Plain Python, no ``import unreal``: Tools/fetch-osm.ps1 runs it with the engine's bundled
interpreter right after the download, and it can be re-run by hand at any time:

    python Tools/Editor/parse_osm.py Tools/Data/osm/east_village.json

Writes next to the input:

    <name>.buildings.json   one record per building: outer ring (+ holes) in lat/lon, height in
                            metres and where the height came from, and the tags worth keeping
    <name>.streets.json     named roads clipped to the district, parks, and POIs for later

The district is not the download bbox. The download is a lat/lon box, but Manhattan's grid
runs 29 degrees off true north, so a lat/lon box cuts every block on the diagonal. Instead
the district is the rectangle between four named street centre lines (DISTRICTS below); a
building belongs to it when its footprint centroid is inside, so every block is whole.
"""

import io
import json
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import _geo as geo  # noqa: E402

# The four streets whose centre lines bound each district, and how far past them the ground
# and roads extend (so the boundary streets get both sidewalks).
DISTRICTS = {
    "east_village": {
        "title": "East Village, Tompkins Square Park",
        "west": "1st Avenue",
        "east": "Avenue C",
        "south": "East 6th Street",
        "north": "East 11th Street",
        "margin_m": 22.0,
    },
}

KEEP_BUILDING_TAGS = ("building", "height", "building:levels", "name", "addr:housenumber",
                      "addr:street", "roof:shape", "min_height")

ROAD_TYPES = ("primary", "secondary", "tertiary", "residential", "unclassified",
              "living_street", "primary_link", "secondary_link", "tertiary_link")
AVENUE_TYPES = ("primary", "secondary", "tertiary")
STREET_WIDTH_M = 12.0
AVENUE_WIDTH_M = 25.0


def load(path):
    with io.open(path, "r", encoding="utf-8") as f:
        return json.load(f)


def write(path, data):
    with io.open(path, "w", encoding="utf-8", newline="\n") as f:
        json.dump(data, f, indent=1, sort_keys=False, ensure_ascii=False)
        f.write("\n")


def geom_latlon(geometry):
    return [(p["lat"], p["lon"]) for p in geometry if p is not None]


def stitch_rings(ways):
    """Join way geometries (lists of (lat, lon)) end to end into closed rings."""
    pending = [list(w) for w in ways if len(w) >= 2]
    rings = []
    while pending:
        ring = pending.pop(0)
        grew = True
        while ring[0] != ring[-1] and grew:
            grew = False
            for i, w in enumerate(pending):
                if w[0] == ring[-1]:
                    ring.extend(w[1:])
                elif w[-1] == ring[-1]:
                    ring.extend(list(reversed(w))[1:])
                elif w[-1] == ring[0]:
                    ring = w[:-1] + ring
                elif w[0] == ring[0]:
                    ring = list(reversed(w))[:-1] + ring
                else:
                    continue
                pending.pop(i)
                grew = True
                break
        if ring[0] == ring[-1] and len(ring) >= 4:
            rings.append(ring[:-1])
    return rings


def street_points(elements, name, lat0, lon0):
    pts = []
    for e in elements:
        t = e.get("tags", {})
        if e["type"] == "way" and t.get("name") == name and t.get("highway") in ROAD_TYPES:
            pts.extend(geo.project(la, lo, lat0, lon0) for la, lo in geom_latlon(e["geometry"]))
    return pts


def district_rect(elements, cfg, lat0, lon0):
    """Corners (local metres about lat0/lon0) of the rectangle between the four streets."""
    lines = {}
    for side in ("west", "east", "south", "north"):
        pts = street_points(elements, cfg[side], lat0, lon0)
        if len(pts) < 2:
            raise SystemExit("district edge street '{0}' not found in the download".format(cfg[side]))
        lines[side] = geo.fit_line(pts)
    corners = {}
    for ns in ("south", "north"):
        for ew in ("west", "east"):
            p = geo.intersect_lines(lines[ns][0], lines[ns][1], lines[ew][0], lines[ew][1])
            corners[ns + "_" + ew] = p
    return corners


def building_records(elements):
    """(id, osm_type, osm_id, outer ring, holes, tags) for every building way and relation."""
    out = []
    for e in elements:
        t = e.get("tags", {})
        if "building" not in t:
            continue
        if e["type"] == "way":
            ring = geom_latlon(e.get("geometry", []))
            if len(ring) >= 4 and ring[0] == ring[-1]:
                out.append(("W{0}".format(e["id"]), "way", e["id"], [ring[:-1]], [], t))
        elif e["type"] == "relation":
            outers = [geom_latlon(m.get("geometry", [])) for m in e.get("members", [])
                      if m.get("type") == "way" and m.get("role") == "outer"]
            inners = [geom_latlon(m.get("geometry", [])) for m in e.get("members", [])
                      if m.get("type") == "way" and m.get("role") == "inner"]
            outer_rings = stitch_rings(outers)
            if outer_rings:
                out.append(("R{0}".format(e["id"]), "relation", e["id"], outer_rings,
                            stitch_rings(inners), t))
    return out


def park_rings(elements):
    """(id, name, outer ring) for every outer ring of every leisure=park way and relation.

    A relation with several outer rings gets ``R<id>_1``, ``R<id>_2``... in stitch order, so
    every record id is unique; a single ring keeps the plain ``W<id>`` / ``R<id>``.
    """
    out = []
    for e in elements:
        t = e.get("tags", {})
        if t.get("leisure") != "park":
            continue
        if e["type"] == "way":
            rings = [geom_latlon(e["geometry"])[:-1]]
            prefix = "W"
        else:
            rings = stitch_rings([geom_latlon(m.get("geometry", [])) for m in e.get("members", [])
                                  if m.get("role") == "outer"])
            prefix = "R"
        base = "{0}{1}".format(prefix, e["id"])
        for idx, ring in enumerate(rings, 1):
            out.append((base if len(rings) == 1 else "{0}_{1}".format(base, idx), t.get("name"), ring))
    return out


def sort_parks(parks):
    """Deterministic park order: by id, then by outer ring. ``district.parks[0]`` is 'the park'."""
    return sorted(parks, key=lambda p: (p["id"], p["outer"]))


def road_width(tags):
    w = geo.parse_length_m(tags.get("width"))
    if w is not None and 3.0 <= w <= 60.0:
        return w, "width"
    if tags.get("highway") in AVENUE_TYPES or "Avenue" in (tags.get("name") or ""):
        return AVENUE_WIDTH_M, "avenue default"
    return STREET_WIDTH_M, "street default"


def main(raw_path):
    base = raw_path[:-len(".json")] if raw_path.endswith(".json") else raw_path
    name = os.path.basename(base)
    cfg = DISTRICTS.get(name)
    if cfg is None:
        raise SystemExit("no DISTRICTS entry for '{0}'".format(name))

    data = load(raw_path)
    elements = data["elements"]
    meta_path = base + ".meta.json"
    fetch_meta = load(meta_path) if os.path.exists(meta_path) else {}

    # Rough origin first (the download bbox centre) to find the streets, then the real one:
    # the centre of the district rectangle.
    bb = fetch_meta.get("bbox")
    if bb:
        lat_r, lon_r = (bb["south"] + bb["north"]) / 2.0, (bb["west"] + bb["east"]) / 2.0
    else:
        lats = [p["lat"] for e in elements for p in e.get("geometry", []) or [] if p]
        lons = [p["lon"] for e in elements for p in e.get("geometry", []) or [] if p]
        lat_r, lon_r = sum(lats) / len(lats), sum(lons) / len(lons)
    rough = district_rect(elements, cfg, lat_r, lon_r)
    cx = sum(p[0] for p in rough.values()) / 4.0
    cy = sum(p[1] for p in rough.values()) / 4.0
    lat0, lon0 = geo.unproject(cx, cy, lat_r, lon_r)
    corners = district_rect(elements, cfg, lat0, lon0)

    # Frame: u runs along the streets (west -> east), v along the avenues (south -> north).
    sw, se, nw = corners["south_west"], corners["south_east"], corners["north_west"]
    frame = geo.Frame((0.0, 0.0), (se[0] - sw[0] + (corners["north_east"][0] - nw[0]),
                                    se[1] - sw[1] + (corners["north_east"][1] - nw[1])))
    local = {k: frame.to_local(p) for k, p in corners.items()}
    box = (min(q[0] for q in local.values()), min(q[1] for q in local.values()),
           max(q[0] for q in local.values()), max(q[1] for q in local.values()))
    margin = cfg["margin_m"]
    outer_box = (box[0] - margin, box[1] - margin, box[2] + margin, box[3] + margin)

    def box_corners_latlon(b):
        pts = [frame.to_world((b[0], b[1])), frame.to_world((b[2], b[1])),
               frame.to_world((b[2], b[3])), frame.to_world((b[0], b[3]))]
        return [list(geo.unproject(p[0], p[1], lat0, lon0)) for p in pts]

    # --- buildings --------------------------------------------------------------------
    buildings = []
    for rid, osm_type, osm_id, outers, holes, tags in building_records(elements):
        for idx, ring in enumerate(outers):
            pts = [geo.project(la, lo, lat0, lon0) for la, lo in ring]
            if len(geo.clean_ring(pts)) < 3:
                continue
            c = frame.to_local(geo.centroid(pts))
            if not (box[0] <= c[0] <= box[2] and box[1] <= c[1] <= box[3]):
                continue
            height, source = geo.building_height_m(tags)
            record_id = rid if len(outers) == 1 else "{0}_{1}".format(rid, idx)
            buildings.append({
                "id": record_id,
                "osm_type": osm_type,
                "osm_id": osm_id,
                "height_m": round(height, 3),
                "height_source": source,
                "area_m2": round(abs(geo.signed_area(pts)), 1),
                "outer": [[round(la, 7), round(lo, 7)] for la, lo in ring],
                "holes": [[[round(la, 7), round(lo, 7)] for la, lo in h] for h in holes] if idx == 0 else [],
                "tags": {k: tags[k] for k in KEEP_BUILDING_TAGS if k in tags},
            })
    buildings.sort(key=lambda b: b["id"])

    # --- roads ------------------------------------------------------------------------
    roads = []
    for e in elements:
        t = e.get("tags", {})
        if e["type"] != "way" or t.get("highway") not in ROAD_TYPES or not t.get("name"):
            continue
        pts = [frame.to_local(geo.project(la, lo, lat0, lon0)) for la, lo in geom_latlon(e["geometry"])]
        pieces = geo.clip_polyline_to_box(pts, outer_box)
        pieces = [p for p in pieces if geo.polyline_length(p) > 1.0]
        if not pieces:
            continue
        width, width_source = road_width(t)
        roads.append({
            "id": "W{0}".format(e["id"]),
            "name": t.get("name"),
            "highway": t.get("highway"),
            "lanes": t.get("lanes"),
            "oneway": t.get("oneway"),
            "width_m": width,
            "width_source": width_source,
            "pieces": [[list(geo.unproject(*frame.to_world(q), lat0=lat0, lon0=lon0)) for q in piece]
                       for piece in pieces],
        })
    roads.sort(key=lambda r: r["id"])

    # --- parks and POIs -------------------------------------------------------------------
    parks = []
    for park_id, park_name, ring in park_rings(elements):
        pts = [geo.project(la, lo, lat0, lon0) for la, lo in ring]
        if len(pts) < 3:
            continue
        c = frame.to_local(geo.centroid(pts))
        if box[0] <= c[0] <= box[2] and box[1] <= c[1] <= box[3]:
            parks.append({"id": park_id, "name": park_name,
                          "outer": [[round(la, 7), round(lo, 7)] for la, lo in ring]})
    parks = sort_parks(parks)

    pois = []
    for e in elements:
        t = e.get("tags", {})
        if e["type"] == "node" and ("amenity" in t or "shop" in t):
            c = frame.to_local(geo.project(e["lat"], e["lon"], lat0, lon0))
            if box[0] <= c[0] <= box[2] and box[1] <= c[1] <= box[3]:
                pois.append({"id": "N{0}".format(e["id"]), "lat": e["lat"], "lon": e["lon"],
                             "amenity": t.get("amenity"), "shop": t.get("shop"),
                             "name": t.get("name")})

    size_u = box[2] - box[0]
    size_v = box[3] - box[1]
    meta = {
        "district": name,
        "title": cfg["title"],
        "source": os.path.basename(raw_path),
        "fetched_utc": fetch_meta.get("fetched_utc"),
        "download_bbox": fetch_meta.get("bbox"),
        "licence": "ODbL 1.0, (c) OpenStreetMap contributors",
        "origin": {"lat": round(lat0, 8), "lon": round(lon0, 8)},
        "bounds_streets": {k: cfg[k] for k in ("west", "east", "south", "north")},
        "district_corners_latlon": box_corners_latlon(box),
        "ground_corners_latlon": box_corners_latlon(outer_box),
        "grid_bearing_deg": round(math.degrees(math.atan2(frame.u[0], frame.u[1])), 2),
        "size_m": [round(size_u, 1), round(size_v, 1)],
        "rules": {
            "height": "height tag, else building:levels * 3.2 + 1.5, else 15 m (5 m for "
                      "garages/sheds)",
            "road_width": "width tag, else 25 m for avenues and 12 m for streets",
        },
    }
    write(base + ".buildings.json", {"_meta": meta, "buildings": buildings})
    write(base + ".streets.json", {"_meta": meta, "roads": roads, "parks": parks, "pois": pois})

    # --- stats --------------------------------------------------------------------------
    heights = [b["height_m"] for b in buildings]
    by_source = {}
    for b in buildings:
        by_source[b["height_source"]] = by_source.get(b["height_source"], 0) + 1
    in_range = sum(1 for h in heights if 15.0 <= h <= 25.0)
    print("district   {0} ({1})".format(name, cfg["title"]))
    print("bounds     {west} / {east} / {south} / {north}".format(**cfg))
    print("origin     {0:.7f}, {1:.7f}".format(lat0, lon0))
    print("size       {0:.0f} m along the streets x {1:.0f} m along the avenues "
          "(ground incl. margin {2:.0f} x {3:.0f} m)".format(
              size_u, size_v, size_u + 2 * margin, size_v + 2 * margin))
    print("buildings  {0}  (explicit height {1}, from levels {2}, default {3}, small {4})".format(
        len(buildings), by_source.get("height", 0), by_source.get("levels", 0),
        by_source.get("default", 0), by_source.get("small", 0)))
    if heights:
        print("height m   min {0:.1f}  median {1:.1f}  max {2:.1f}  ({3} of {4} = {5:.0f}% in 15-25 m)".format(
            min(heights), geo.median(heights), max(heights), in_range, len(heights),
            100.0 * in_range / len(heights)))
    print("roads      {0} ways ({1})".format(len(roads), ", ".join(sorted({r["name"] for r in roads}))))
    print("parks      {0} ({1})".format(len(parks), ", ".join(p["name"] or p["id"] for p in parks)))
    print("pois       {0}".format(len(pois)))
    return 0


if __name__ == "__main__":
    if len(sys.argv) < 2:
        raise SystemExit("usage: parse_osm.py <raw overpass json>")
    sys.exit(main(sys.argv[1]))
