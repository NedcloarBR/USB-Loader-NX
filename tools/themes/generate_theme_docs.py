#!/usr/bin/env python3
"""The Manager's theme format from one place: manager/themes/theme-spec.json.

    generate_theme_docs.py           writes manager/themes/theme.schema.json and the tables of manager/themes/README.md
    generate_theme_docs.py --check   fails when the files are out of date, or when the Manager's code reads a key,
                                     color or component the spec does not have (and says which)

The spec lists every theme.json key (type, range, default), the colors, and the components of each screen; the PC
Manager's theme editor reads the same file. The README keeps its prose: only what sits between
<!-- spec:NAME --> and <!-- /spec:NAME --> is generated.
"""
import argparse
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
THEMES = ROOT / "manager" / "themes"
SPEC = THEMES / "theme-spec.json"
SCHEMA = THEMES / "theme.schema.json"
README = THEMES / "README.md"
SOURCES = ROOT / "manager" / "source" / "ui"

NUMBER = r"^-?[0-9]+(\.[0-9]+)?$"
COLOR = r"^#([0-9A-Fa-f]{6}|[0-9A-Fa-f]{8})$"


def code(text) -> str:
    return f"`{text}`"


def format_number(value) -> str:
    return str(int(value)) if float(value).is_integer() else str(value)


def values_text(row) -> str:
    """What a key accepts, as the README says it."""
    kind = row["type"]
    if kind == "number":
        text = f"{code(format_number(row['min']))} to {code(format_number(row['max']))}"
        if "default" in row:
            text += f" (default {code(format_number(row['default']))})"
        if row.get("also"):
            text += ", or " + ", ".join(code(v) for v in row["also"])
        return text
    if kind in ("enum", "list"):
        text = ", ".join(code(v) for v in row["values"])
        if row.get("default") and kind == "enum":
            text += f" (default {code(row['default'])})"
        return text
    if kind == "file":
        return "A file of the theme: " + ", ".join(f.upper() for f in row["formats"])
    if kind == "color":
        return "`#RRGGBB` or `#RRGGBBAA`"
    return "Text"


def keys_cell(keys) -> str:
    """`Images.Panel`, `.Card`…: the shared prefix written once."""
    if len(keys) == 1:
        return code(keys[0])
    prefix = keys[0].rsplit(".", 1)[0] + "."
    if all(k.startswith(prefix) and "." not in k[len(prefix):] for k in keys):
        return ", ".join([code(keys[0])] + [code("." + k[len(prefix):]) for k in keys[1:]])
    return ", ".join(code(k) for k in keys)


def key_table(rows) -> str:
    lines = ["| Key | Values | What it does |", "|---|---|---|"]
    for row in rows:
        lines.append(f"| {keys_cell(row['keys'])} | {values_text(row)} | {row['description']} |")
    return "\n".join(lines)


def generated_tables(spec) -> dict:
    colors = ["| Color | Default | Used for |", "|---|---|---|"]
    for row in spec["colors"]:
        colors.append(f"| {', '.join(code(n) for n in row['names'])} | {', '.join(code(d) for d in row['defaults'])} | {row['description']} |")

    properties = ["| Property | Values | What it does |", "|---|---|---|"]
    for row in spec["componentProperties"]:
        properties.append(f"| {code(row['keys'][0])} | {values_text(row)} | {row['description']} |")

    screens = []
    for screen, groups in spec["screens"].items():
        screens.append(f"**{code('Layout.' + screen)}**\n")
        screens.append("| Component | What it is |\n|---|---|")
        for group in groups:
            screens.append(f"| {', '.join(code(i) for i in group['ids'])} | {group['description']} |")
        screens.append("")

    types = ["| `Type` | What it shows |", "|---|---|"]
    for row in spec["componentTypes"]:
        types.append(f"| {code(row['type'])} | {row['description']} |")
    types.append("")
    types.append("Fields of `text`: " + ", ".join(code("{{" + f + "}}") for f in spec["textFields"]) + ".")

    return {
        "keys": key_table(spec["keys"]),
        "layout": key_table(spec["layoutKeys"]),
        "properties": "\n".join(properties),
        "components": "\n".join(screens).rstrip(),
        "types": "\n".join(types),
        "colors": "\n".join(colors),
    }


def value_schema(row) -> dict:
    kind = row["type"]
    schema = {"type": "string", "description": row["description"]}
    if kind == "number":
        schema["pattern"] = NUMBER
        if row.get("also"):
            schema = {"description": row["description"], "anyOf": [{"type": "string", "pattern": NUMBER}, {"enum": row["also"]}]}
        schema["minimum"] = row["min"]
        schema["maximum"] = row["max"]
    elif kind == "enum":
        schema["enum"] = row["values"]
    elif kind == "list":
        schema["pattern"] = r"^\s*(" + "|".join(row["values"]) + r")(\s*,\s*(" + "|".join(row["values"]) + r"))*\s*$"
    elif kind == "color":
        schema["pattern"] = COLOR
    elif kind == "file":
        schema["pattern"] = r"(?i)\.(" + "|".join(row["formats"]) + r")$"
    if "default" in row:
        schema["default"] = format_number(row["default"]) if kind == "number" else row["default"]
    return schema


def put(tree: dict, dotted: str, schema: dict):
    """Places a value schema at its dotted path, as nested objects (theme.json is nested)."""
    parts = dotted.split(".")
    node = tree
    for part in parts[:-1]:
        node = node.setdefault("properties", {}).setdefault(part, {"type": "object"})
    node.setdefault("properties", {})[parts[-1]] = schema


def build_schema(spec) -> dict:
    root = {
        "$schema": "https://json-schema.org/draft/2020-12/schema",
        "title": "USB Loader NX Manager theme",
        "description": "theme.json of a Manager theme; generated from theme-spec.json. Every value is a string.",
        "type": "object",
    }
    for row in spec["keys"] + spec["layoutKeys"]:
        for key in row["keys"]:
            put(root, key, value_schema(row))
    for row in spec["colors"]:
        for name, default in zip(row["names"], row["defaults"]):
            put(root, "Colors." + name, {"type": "string", "pattern": COLOR, "default": default, "description": row["description"]})

    component = {"type": "object", "additionalProperties": False, "properties": {}}
    for row in spec["componentProperties"]:
        component["properties"][row["keys"][0]] = value_schema(row)
    for screen, groups in spec["screens"].items():
        ids = [i for group in groups for i in group["ids"]]
        put(root, "Layout." + screen, {
            "type": "object",
            "description": f"Components of the {screen} screen: " + ", ".join(ids) + ", or any other name with a Type.",
            "additionalProperties": {"$ref": "#/$defs/component"},
        })
    root["$defs"] = {"component": component}
    return root


def readme_with(text: str, tables: dict) -> str:
    for name, table in tables.items():
        pattern = re.compile(rf"(<!-- spec:{name} -->\n).*?(\n<!-- /spec:{name} -->)", re.S)
        if not pattern.search(text):
            raise SystemExit(f"README.md has no <!-- spec:{name} --> block")
        text = pattern.sub(lambda m: m.group(1) + table + m.group(2), text)
    return text


def check_code(spec) -> list:
    """Keys, colors and components the Manager's code uses but the spec lacks."""
    problems = []
    theme_cpp = (SOURCES / "theme.cpp").read_text(encoding="utf-8")
    known = {k for row in spec["keys"] + spec["layoutKeys"] for k in row["keys"]}
    sections = "Theme|Format|Images|Fonts|Sounds|Animations|Layout|Carousel|Grid|Views"
    for key in sorted(set(re.findall(rf'"((?:{sections})\.[A-Za-z.]+)"', theme_cpp))):
        if key not in known and not key.endswith("."):
            problems.append(f"theme.cpp reads {key}, not in the spec")
    for array, prefix, suffix in (("SkinNames", "Images.", ""), ("SkinNames", "Images.", "Border"), ("IconNames", "Images.Icon.", "")):
        match = re.search(rf"{array}\[\] = \{{([^}}]*)\}}", theme_cpp)
        for name in re.findall(r'"([A-Za-z]+)"', match.group(1) if match else ""):
            if prefix + name + suffix not in known:
                problems.append(f"theme.cpp reads {prefix}{name}{suffix}, not in the spec")
    colors = {n for row in spec["colors"] for n in row["names"]}
    for name in re.findall(r'\{ "([A-Za-z]+)",\s*&color::', theme_cpp):
        if name not in colors:
            problems.append(f"theme.cpp has the color {name}, not in the spec")
    screens = {screen: {i for g in groups for i in g["ids"]} for screen, groups in spec["screens"].items()}
    for source in sorted(SOURCES.glob("*.cpp")):
        for screen, component in re.findall(r'Place\("(\w+)", "([\w.]+)"', source.read_text(encoding="utf-8")):
            if component not in screens.get(screen, set()):
                problems.append(f"{source.name} places {screen}.{component}, not in the spec")
    return problems


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()

    spec = json.loads(SPEC.read_text(encoding="utf-8"))
    schema = json.dumps(build_schema(spec), indent="\t", ensure_ascii=False) + "\n"
    readme = readme_with(README.read_text(encoding="utf-8"), generated_tables(spec))

    if args.check:
        problems = check_code(spec)
        if not SCHEMA.exists() or SCHEMA.read_text(encoding="utf-8") != schema:
            problems.append("theme.schema.json is out of date: run tools/themes/generate_theme_docs.py")
        if README.read_text(encoding="utf-8") != readme:
            problems.append("README.md's tables are out of date: run tools/themes/generate_theme_docs.py")
        for problem in problems:
            print(problem, file=sys.stderr)
        sys.exit(1 if problems else 0)

    SCHEMA.write_text(schema, encoding="utf-8")
    README.write_text(readme, encoding="utf-8")
    print(f"wrote {SCHEMA.relative_to(ROOT)} and the tables of {README.relative_to(ROOT)}", file=sys.stderr)


if __name__ == "__main__":
    main()
