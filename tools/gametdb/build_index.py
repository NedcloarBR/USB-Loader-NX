#!/usr/bin/env python3
"""Builds the Manager's GameTDB index (manager/romfs/gametdb/index.bin) from GameTDB's switchtdb.xml.

The Manager matches a game to GameTDB by its name (GameTDB has no title ids), so the index keeps, for every GameTDB
entry, its id, its regions and its titles in every language, normalized the way gametdb.cpp normalizes NACP names.

    build_index.py [--xml switchtdb.xml | --download] [--out index.bin] [--match "Game name"]

File: "UTDB", u32 format version (2), u32 size of the text, then the text compressed with zlib. The text has one line
per entry, its fields separated by tabs: id, regions (comma separated), developer, publisher, year, genres (comma
separated, GameTDB's English names), players, modes ("t" TV, "h" handheld, "m" tabletop), age ratings ("ESRB=M,PEGI=18")
and then the normalized titles.
"""
import argparse
import io
import os
import struct
import sys
import urllib.request
import xml.etree.ElementTree as ET
import zipfile
import zlib

FORMAT_VERSION = 2
DOWNLOAD_URL = "https://www.gametdb.com/switchtdb.zip"

# Latin letters with marks, folded to ASCII; the same table is in manager/source/gametdb.cpp.
FOLD = {}
for plain, marked in {
    "a": "àáâãäåāăą", "c": "çćĉċč", "d": "ďđ", "e": "èéêëēĕėęě", "g": "ĝğġģ", "h": "ĥħ", "i": "ìíîïĩīĭįı",
    "j": "ĵ", "k": "ķ", "l": "ĺļľŀł", "n": "ñńņňŉ", "o": "òóôõöøōŏő", "r": "ŕŗř", "s": "śŝşšß", "t": "ţťŧ",
    "u": "ùúûüũūŭůűų", "w": "ŵ", "y": "ýÿŷ", "z": "źżž",
}.items():
    for ch in marked:
        FOLD[ch] = plain
        FOLD[ch.upper()] = plain


# Punctuation and symbols outside ASCII, read as spaces; the same ranges are in manager/source/gametdb.cpp.
SPACE_RANGES = ((0x00A0, 0x00BF), (0x00D7, 0x00D7), (0x00F7, 0x00F7), (0x2000, 0x206F), (0x2100, 0x214F),
                (0x2190, 0x2BFF), (0x3000, 0x303F), (0x30FB, 0x30FB), (0xFF01, 0xFF0F), (0xFF1A, 0xFF20),
                (0xFF3B, 0xFF40), (0xFF5B, 0xFF65))


def normalize(name: str) -> str:
    """Lowercase ASCII letters and digits, marks folded, punctuation a single space; other scripts kept."""
    out = []
    for ch in name:
        ch = FOLD.get(ch, ch)
        code = ord(ch)
        if code < 0x80:
            ch = ch.lower()
            out.append(ch if ch.isalnum() else " ")
        elif any(low <= code <= high for low, high in SPACE_RANGES):
            out.append(" ")
        else:
            out.append(ch)
    return " ".join("".join(out).split())


def read_xml(args) -> bytes:
    if args.download:
        request = urllib.request.Request(DOWNLOAD_URL, headers={"User-Agent": "USBLoaderNX-index-builder"})
        with urllib.request.urlopen(request, timeout=120) as response:
            archive = zipfile.ZipFile(io.BytesIO(response.read()))
        return archive.read(next(n for n in archive.namelist() if n.endswith(".xml")))
    with open(args.xml, "rb") as file:
        return file.read()


def clean(text) -> str:
    return " ".join((text or "").replace("\t", " ").split())


def entries(xml: bytes):
    for _, game in ET.iterparse(io.BytesIO(xml)):
        if game.tag != "game":
            continue
        game_id = clean(game.findtext("id"))
        titles = []
        for locale in game.findall("locale"):
            title = normalize(locale.findtext("title") or "")
            if title and title not in titles:
                titles.append(title)
        date = game.find("date")
        players = game.find("input")
        mode = game.find("mode")
        ratings = []
        for system in ("ESRB", "PEGI", "CERO"):
            rating = game.find(f"rating_{system}")
            if rating is not None and rating.get("value"):
                ratings.append(f"{system}={clean(rating.get('value'))}")
        fields = [
            game_id,
            clean(game.findtext("region")),
            clean(game.findtext("developer")),
            clean(game.findtext("publisher")),
            clean(date.get("year")) if date is not None else "",
            ",".join(g.strip() for g in clean(game.findtext("genre")).split(",") if g.strip()),
            clean(players.get("players")) if players is not None else "",
            "".join(flag for key, flag in (("tv", "t"), ("handheld", "h"), ("tabletop", "m")) if mode is not None and mode.get(key) == "true"),
            ",".join(ratings),
        ]
        game.clear()
        if game_id and titles:
            yield fields, titles


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--xml")
    source.add_argument("--download", action="store_true")
    parser.add_argument("--out", default=os.path.join(os.path.dirname(__file__), "..", "..", "manager", "romfs", "gametdb", "index.bin"))
    parser.add_argument("--match", help="print the entries a name matches instead of writing the index")
    args = parser.parse_args()

    rows = list(entries(read_xml(args)))
    if args.match:
        wanted = normalize(args.match)
        print(f"normalized: {wanted!r}")
        for fields, titles in rows:
            if wanted in titles:
                print(fields, titles[0])
        return

    text = "".join("\t".join(fields + titles) + "\n" for fields, titles in rows).encode("utf-8")
    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    with open(args.out, "wb") as file:
        file.write(b"UTDB" + struct.pack("<II", FORMAT_VERSION, len(text)) + zlib.compress(text, 9))
    print(f"{len(rows)} entries, {len(text)} bytes of text, {os.path.getsize(args.out)} bytes written to {args.out}", file=sys.stderr)


if __name__ == "__main__":
    main()
