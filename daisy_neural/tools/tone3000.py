#!/usr/bin/env python3
"""
tone3000.py — search TONE3000, list a tone's files, download chosen ones, and scan
any gear type for licences Mirth can ship.

find_cc0.py walks the A2 catalogue only, so it cannot see cab IRs (gear `cab`,
which carry no architecture). This covers the rest of the workflow that built the
personal card and found the shareable cabs, 2026-10-11:

    search     most-downloaded tones for a gear type and/or query
    models     the files inside one tone (A2 captures, or IRs with --ir)
    download   chosen files by tone:model id, saved under their own names
    licences   walk every tone of a gear type; count licences, list CC0 / CC-BY

Gear types seen: amp, amp-cab, pedal, cab (IRs), outboard. A tone's gear is set per
pack, not per file — "amp-cab" packs can hold DI files, so read the file names.

Same key as find_cc0.py (its docstring says where it lives). Downloads use each
model's `model_url` with the key; the whole-tone zip endpoint is partner-only.
Use a Python that can verify HTTPS: /usr/bin/python3 or Homebrew's, not the
PlatformIO one.

    python3 tools/tone3000.py search --gears pedal --a2
    python3 tools/tone3000.py search --gears cab --query "4x12"
    python3 tools/tone3000.py models 36827             # an A2 tone's captures
    python3 tools/tone3000.py models 75087 --ir        # a cab tone's IRs
    python3 tools/tone3000.py download --out captures/personal/nam 36827:381611
    python3 tools/tone3000.py licences --gears cab
"""

import argparse
import collections
import json
import pathlib
import re
import sys
import time
import urllib.error
import urllib.parse
import urllib.request

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from find_cc0 import MIN_INTERVAL, SHIPPABLE, secret  # noqa: E402

BASE = "https://www.tone3000.com/api/v1"


def get(key, path, raw=False, **params):
    params = {k: v for k, v in params.items() if v is not None}
    url = path if path.startswith("http") else f"{BASE}/{path}?{urllib.parse.urlencode(params)}"
    req = urllib.request.Request(url, headers={"Authorization": f"Bearer {key}"})
    try:
        with urllib.request.urlopen(req, timeout=60) as r:
            return r.read() if raw else json.load(r)
    except urllib.error.HTTPError as e:
        raise SystemExit(f"HTTP {e.code} from {path}: {e.read()[:200].decode('utf-8', 'replace')}")


def list_models(key, tone_id, ir=False):
    # Without architecture=2 the API returns A1 and custom models and leaves A2 out,
    # so A2 has to be asked for; IRs have no architecture at all.
    return get(key, "models", tone_id=tone_id, page_size=300,
               architecture=None if ir else "2")["data"]


def cmd_search(key, a):
    r = get(key, "tones/search", gears=a.gears, query=a.query, architecture="2" if a.a2 else None,
            sort="downloads-all-time", page=1, page_size=25)
    print(f"{a.gears or 'any gear'}{' A2' if a.a2 else ''}{' ' + a.query if a.query else ''}: "
          f"{r.get('total')} tones")
    for t in r.get("data", [])[: a.top]:
        print(f"  {(t.get('license') or '?'):6} {str(t.get('gear')):8} {t.get('downloads_count') or 0:>7} dl "
              f"{str(t.get('models_count')):>4} files  {t['id']:>6}  {t.get('title')}")


def cmd_models(key, a):
    for m in list_models(key, a.tone, a.ir):
        print(f"  {a.tone}:{m['id']}  {m['name']}")


def cmd_download(key, a):
    out = pathlib.Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    for i, spec in enumerate(a.ids):
        if i:
            time.sleep(MIN_INTERVAL)
        tone, model = map(int, spec.split(":"))
        found = [m for ir in (False, True) for m in list_models(key, tone, ir) if m["id"] == model]
        if not found:
            print(f"  {spec}: no such model in that tone")
            continue
        body = get(key, found[0]["model_url"], raw=True)
        ext = ".wav" if body[:4] == b"RIFF" else ".nam" if body[:1] == b"{" else ".bin"
        dst = out / (re.sub(r'[\\/:*?"<>|]+', "_", found[0]["name"]).strip() + ext)
        dst.write_bytes(body)
        print(f"  {spec}  {len(body):>8,} B  {dst.name}")


def cmd_licences(key, a):
    counts, keep, page = collections.Counter(), [], 1
    while True:
        r = get(key, "tones/search", gears=a.gears, sort="oldest", page=page, page_size=25)
        for t in r.get("data", []):
            lic = (t.get("license") or "?").lower()
            counts[lic] += 1
            if lic in SHIPPABLE:
                keep.append(t)
        if not r.get("data") or page * 25 >= (r.get("total") or 0):
            break
        page += 1
        time.sleep(MIN_INTERVAL)
    print(f"{a.gears}: {sum(counts.values())} tones — {dict(counts.most_common())}")
    for t in sorted(keep, key=lambda t: -(t.get("downloads_count") or 0)):
        print(f"  {t['license']:6} {t.get('downloads_count') or 0:>6} dl {str(t.get('models_count')):>4} files  "
              f"{t['id']:>6}  {t.get('title')}\n{'':30}{t.get('url')}")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    s = sub.add_parser("search")
    s.add_argument("--gears")
    s.add_argument("--query")
    s.add_argument("--a2", action="store_true", help="A2 tones only")
    s.add_argument("--top", type=int, default=15)
    m = sub.add_parser("models")
    m.add_argument("tone", type=int)
    m.add_argument("--ir", action="store_true", help="an IR (cab) tone")
    d = sub.add_parser("download")
    d.add_argument("ids", nargs="+", help="tone:model pairs, as `models` prints them")
    d.add_argument("--out", required=True)
    lc = sub.add_parser("licences")
    lc.add_argument("--gears", required=True)
    a = ap.parse_args()
    {"search": cmd_search, "models": cmd_models, "download": cmd_download,
     "licences": cmd_licences}[a.cmd](secret(), a)


if __name__ == "__main__":
    main()
