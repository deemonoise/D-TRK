#!/usr/bin/env python3
"""Compile normalized Genre Lab facts to flash-only C++ tables (no runtime parser)."""
import argparse
import gzip
import json
from pathlib import Path
if __package__:
    from . import dice_timbres
else:
    import dice_timbres

ROOT = Path(__file__).resolve().parents[1]


def row(values):
    return "{" + ",".join(json.dumps(v) if isinstance(v, str) else str(v) for v in values) + "}"


def table(name, rows):
    return name + "[] = {\n" + "".join("  " + r + ",\n" for r in rows) + "};\n"


def compile_events(data):
    events, indices, sequences = [], [], []
    event_ids = {}
    for seq in data["sequences"]:
        sequences.append(row([len(indices), len(seq)]))
        for event in seq:
            token = tuple(event)
            if token not in event_ids:
                event_ids[token] = len(events)
                events.append(row(event))
            indices.append(str(event_ids[token]))
    assert len(event_ids) <= 65536
    return (table("static const DiceEvent kEvents", events) +
            table("static const uint16_t kEventIds", [",".join(indices[i:i+32]) for i in range(0,len(indices),32)]) +
            table("static const Sequence kSequences", sequences))


def timbre_row(r):
    return (row([r[k] for k in ("name","type","machine")])[:-1] + "," + row(r["osc"]) + "," + str(r["semi"]) +
            "," + row(r["params"]) + "," + row(r["env"]) + "," +
            ",".join(str(r[k]) for k in ("sub","noise","glide","drive","level","filter","cutoff","resonance","fenv","fdec","chord")) +
            "," + row(r["lfo"]) + "}")


def compile_timbres(data):
    rows, refs, identities = [], [], {}
    for voice in data["voices"]:
        recipe = dice_timbres.build(voice)
        token = json.dumps(recipe)
        if token not in identities:
            identities[token] = len(rows)
            rows.append(timbre_row(recipe))
        refs.append(identities[token])
    assert len(rows) <= 65536
    return table("static const DiceTimbre kTimbres", rows), refs


def compile_voices(data, timbres):
    voices = []
    for index, v in enumerate(data["voices"]):
        values = [v["name"], timbres[index]] + [v[k] for k in ("seq", "cat", "pitch", "decay", "cut", "res", "vol", "delay", "reverb", "chance", "length", "speed")]
        voices.append(row(values)[:-1] + "," + row(v["lfo"]) + "}")
    return table("static const DiceVoice kVoices", voices)


# Stable guide group IDs. Keep the rhythmic and instrumental vocabulary together.
FEELS = {
    "Pocket": "house minh edmA garage uk",
    "Broken": "idmA ambB breaks dnb dnbA dnbB dub grime idm jun brB",
    "Air": "amb ambX indA exA exB exC ambA",
    "Keys": "chA chill chB chX",
    "Echo": "dubX hiA worldA world worldB",
    "Motor": "acid hitech hardB hardX hard ind psyA hiB psyB psyC shr hardC techX techno2 techno",
    "Anthem": "edmB edm euB fbC trance2 trC trX",
}
GROUP_FEEL = {group: feel for feel, groups in FEELS.items() for group in groups.split()}

# The hardware menu is shorter than the source catalogue. Keep the legacy
# coordinates in the generated projection so saved selections still resolve.
MENU_FAMILIES = (
    ("Acid", ("Acid", "Acid House", "Acid Breaks", "Acid Techno")),
    ("Techno", ("Warehouse Techno", "Hypnotic Techno", "Detroit Techno", "Dub Techno", "Hardgroove", "Minimal Techno")),
    ("Drum & Bass", ("Rolling DnB", "Liquid DnB", "Neurofunk", "Jungle", "Halftime DnB", "Jump Up")),
    ("Trance", ("Classic Trance", "Progressive Trance", "Uplifting", "Goa", "Dark Psy", "Full-On")),
    ("Hardcore & Gabber", ("Gabber", "Industrial Hardcore", "Happy Hardcore", "Breakbeat Hardcore")),
    ("IDM", ("IDM", "Braindance", "Clicks & Cuts", "Polyrhythmic IDM")),
    ("Ambient", ("Space Ambient", "Dark Ambient", "Generative Ambient", "Berlin School")),
    ("Industrial", ("Electro-Industrial", "Industrial Techno", "Darkwave")),
    ("UK Garage", ("UK Garage", "2-Step Garage", "Speed Garage", "Future Garage", "UK Bassline")),
    ("Rave", ("Old Skool Rave", "Bleep & Bass", "Rave Revival", "Rave Techno")),
)


def compile_menu(addresses):
    choices, families = [], []
    seen = set()
    for name, styles in MENU_FAMILIES:
        families.append(row([name, len(choices), len(styles)]))
        for style in styles:
            if style in seen or style not in addresses:
                raise ValueError(f"Invalid Dice menu style: {style}")
            choices.append(row(addresses[style]))
            seen.add(style)
    return (table("static const DiceChoice kMenuChoices", choices) +
            table("static const Group kMenuFamilies", families))


def production_row(line):
    name, brief, basis, profile, hits, shade = (cell.strip() for cell in line.strip("|").split("|"))
    if not all((name, brief, basis, profile.isidentifier())):
        raise ValueError(f"Incomplete Dice direction: {name}")
    hits, shade = int(hits), int(shade)
    if hits not in range(1, 9) or shade not in range(-2, 3):
        raise ValueError(f"Dice direction out of range: {name}")
    return name, f"{{DiceVoicing::{profile},{hits},{shade}}}"


def production_directions(path):
    directions = {}
    for line in path.read_text().splitlines():
        if not line.startswith("| ") or line.startswith("| Стиль |"):
            continue
        name, production = production_row(line)
        if name in directions:
            raise ValueError(f"Duplicate Dice direction: {name}")
        directions[name] = production
    return directions


def compile_styles(data):
    directions = production_directions(ROOT / "data/dice-styles.md")
    if directions.keys() != {s["name"] for s in data["styles"]}:
        raise ValueError("Dice style directions do not match the catalogue")
    styles, variants, groups, addresses = [], [], [], {}
    for genre, group in enumerate(data["groups"]):
        members = [s for s in data["styles"] if s["group"] == group["id"]]
        groups.append(row([group["name"], len(styles), len(members)]))
        for index, style in enumerate(members):
            addresses[style["name"]] = (genre, index)
            recipe = row([style["name"], len(variants), style["length"], style["resolution"]])
            styles.append(recipe[:-1] + ",DiceFeel::" + GROUP_FEEL[group["id"]] + "," + directions[style["name"]] + "}")
            for v in style["variants"]:
                variants.append(row([v[k] for k in ("name", "bpm", "swing", "root", "scale")])[:-1] + "," + row(v["voices"]) + "}")
    assert len(styles) == 317 and len(variants) == 5072 and len(groups) == 54
    return (table("static const DiceVariant kVariants", variants) +
            table("static const DiceRecipe kRecipes", styles) +
            table("static const Group kGroups", groups) + compile_menu(addresses))


def compile_catalog(data):
    timbres, refs = compile_timbres(data)
    return ("// Generated by scripts/generate_dice_catalog.py; see data/dice-guide.md.\n" +
            timbres + compile_events(data) + compile_voices(data,refs) + compile_styles(data))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    data = json.loads(gzip.decompress((ROOT / "data/dice_guide.json.gz").read_bytes()))
    generated = compile_catalog(data)
    target = ROOT / "lib/core/src/dice_catalog.inc"
    if args.check:
        if target.read_text() != generated:
            raise SystemExit("DICE catalogue is stale: run scripts/generate_dice_catalog.py")
    else:
        target.write_text(generated)


if __name__ == "__main__":
    main()
