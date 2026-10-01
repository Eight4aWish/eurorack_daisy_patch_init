#!/usr/bin/env python3
"""
find_cc0.py — every A2 tone on TONE3000 whose licence lets Mirth ship it.

TONE3000's search has no licence filter, but each tone it returns carries its
`license`. So this walks the whole A2 catalogue, oldest first (new tones only
append, so the walk stays stable while it runs), and keeps the ones that are
redistributable: CC0 and CC-BY. It also counts every licence, so the result says
how much of the library is T3K rather than leaving it a guess.

Needs a TONE3000 secret key (Settings → keys, `t3k_cs_…`). It is a server
credential, so it lives outside every repo:

    mkdir -p ~/.config/tone3000 && chmod 700 ~/.config/tone3000
    pbpaste > ~/.config/tone3000/secret_key && chmod 600 ~/.config/tone3000/secret_key

or in $T3K_SECRET. It is sent only as the Authorization header and never printed.

    python3 tools/find_cc0.py --pages 1          # one page: check the key works
    python3 tools/find_cc0.py                    # the lot, at ~90 requests a minute
    python3 tools/find_cc0.py --start-page 400   # resume after an interruption
    python3 tools/find_cc0.py --query "Vox AC30" --query "Fender Twin"
                                                 # top A2 tones per amp, any licence

Writes cc0_tones.csv (tones you can ship) and licence_counts.txt to --out,
default amp_compare/ (gitignored).
"""

import argparse
import collections
import csv
import json
import os
import pathlib
import sys
import time
import urllib.error
import urllib.parse
import urllib.request

API = "https://www.tone3000.com/api/v1/tones/search"
KEY_FILE = pathlib.Path.home() / ".config" / "tone3000" / "secret_key"
SHIPPABLE = {"cc0", "cco", "cc-by"}  # the API docs spell CC0 as "cco"
PAGE_SIZE = 25
MIN_INTERVAL = 60 / 90  # stay under the documented 100 requests a minute


def secret():
    key = os.environ.get("T3K_SECRET") or (KEY_FILE.read_text().strip() if KEY_FILE.exists() else "")
    if not key:
        raise SystemExit(f"no key: put it in {KEY_FILE} (see the docstring) or $T3K_SECRET")
    # Checked before it goes anywhere near a request: a malformed header makes
    # http.client raise with the header's value in the message, which would print
    # the key. Say what is wrong without ever echoing it.
    if not key.startswith("t3k_cs_") or any(c.isspace() for c in key):
        hint = "it is the publishable key — use the secret one, t3k_cs_…" if key.startswith("t3k_") \
            else "it does not start t3k_cs_, so the file holds something other than the secret key"
        raise SystemExit(f"{KEY_FILE}: {len(key)} characters, but {hint}")
    return key


def fetch(key, page, query=None, gears=None):
    params = {"architecture": "2", "page": page, "page_size": PAGE_SIZE,
              "sort": "downloads-all-time" if query else "oldest"}
    if query:
        params["query"] = query
    if gears:
        params["gears"] = gears
    q = urllib.parse.urlencode(params)
    req = urllib.request.Request(f"{API}?{q}", headers={"Authorization": f"Bearer {key}"})
    for attempt in range(5):
        try:
            with urllib.request.urlopen(req, timeout=30) as r:
                return json.load(r)
        except urllib.error.HTTPError as e:
            if e.code == 429 or e.code >= 500:
                time.sleep(15 * (attempt + 1))
                continue
            if e.code in (401, 403):
                raise SystemExit(f"HTTP {e.code}: the key was refused") from None
            raise
        except urllib.error.URLError:
            time.sleep(15 * (attempt + 1))
    raise SystemExit(f"page {page}: gave up after 5 attempts")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--pages", type=int, help="stop after this many pages")
    ap.add_argument("--start-page", type=int, default=1)
    ap.add_argument("--out", default=str(pathlib.Path(__file__).resolve().parent.parent / "amp_compare"))
    ap.add_argument("--query", action="append",
                    help="search mode: the most-downloaded A2 tones for this, with licence (repeatable)")
    ap.add_argument("--gears", help="with --query: e.g. amp-cab, amp, pedal (underscore-separated)")
    ap.add_argument("--top", type=int, default=5, help="with --query: results per search")
    args = ap.parse_args()

    key = secret()
    if args.query:
        for i, query in enumerate(args.query):
            if i:
                time.sleep(MIN_INTERVAL)
            r = fetch(key, 1, query, args.gears)
            print(f"\n{query}  ({r.get('total')} A2 tones)")
            for t in r.get("data", [])[: args.top]:
                print(f"  {(t.get('license') or '?'):6} {str(t.get('gear')):8} {t.get('downloads_count') or 0:>7} dl  "
                      f"{t.get('title')}\n         {t.get('url')}")
        return
    out = pathlib.Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    csv_path = out / "cc0_tones.csv"
    new_file = args.start_page == 1 or not csv_path.exists()
    f = open(csv_path, "w" if new_file else "a", newline="")
    w = csv.writer(f)
    if new_file:
        w.writerow(["license", "gear", "title", "a2_models", "downloads", "url"])

    counts = collections.Counter()
    page, last = args.start_page, 0.0
    total_pages = None
    while True:
        time.sleep(max(0.0, MIN_INTERVAL - (time.time() - last)))
        last = time.time()
        r = fetch(key, page)
        if total_pages is None:
            total_pages = r.get("total_pages")
            print(f"{r.get('total')} A2 tones in {total_pages} pages", flush=True)
        for t in r.get("data", []):
            lic = (t.get("license") or "none").lower()
            counts[lic] += 1
            if lic in SHIPPABLE:
                w.writerow([lic, t.get("gear"), t.get("title"), t.get("a2_models_count"),
                            t.get("downloads_count"), t.get("url")])
        f.flush()
        if page % 20 == 0:
            print(f"  page {page}/{total_pages}  {dict(counts)}", flush=True)
        done = page - args.start_page + 1
        if not r.get("data") or (total_pages and page >= total_pages) or (args.pages and done >= args.pages):
            break
        page += 1
    f.close()

    lines = [f"{lic:12} {n}" for lic, n in counts.most_common()]
    (out / "licence_counts.txt").write_text(
        f"pages {args.start_page}-{page} of {total_pages}\n" + "\n".join(lines) + "\n")
    print("\n".join(lines))
    print(f"\nshippable tones in {csv_path}")


if __name__ == "__main__":
    main()
