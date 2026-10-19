#!/usr/bin/env python3
"""
gru_status.py — a web page showing how train_gru.py runs are going, served from the
training machine so any computer on the network can watch:

    .venv-nam/bin/python tools/gru_status.py               # http://<machine>.local:8124
    .venv-nam/bin/python tools/gru_status.py --runs 'lp_*'

Every request reads each run's log.txt (as train_lp_playable.sh writes them), its
config.json, TensorBoard log and, once finished, result.json; the page reloads itself
every minute. One row per run: its epoch, the best validation ESR and when, how close
early stopping is, and the test scores once it has finished. Then a chart of validation
ESR by epoch, and the validation ESR per Res mark at each run's best epoch.
"""

import argparse
import fnmatch
import html
import json
import math
import pathlib
import re
import subprocess
import time
import traceback
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

HERE = pathlib.Path(__file__).resolve().parent
RUNS = HERE.parent / "captures" / "wasp" / "gru_runs"
EPOCH = re.compile(r"^epoch\s+(\d+)\s+train (?:ESR|MSE) ([\d.e+-]+)\s+valid ESR ([\d.]+)\s+lr (\S+)\s+"
                   r"(?:grad (\S+) \(max (\S+), (\d+)% clipped\)\s+)?([\d.]+) min")
COLOURS = {"cutoff + Res, GRU-16": "var(--c1)", "cutoff + Res, GRU-24": "var(--c2)",
           "cutoff + Res, WaveNet FiLM": "var(--c3)",
           "cutoff, GRU-16": "var(--c3)", "cutoff, GRU-24": "var(--c4)"}
DASHES = {1: "", 2: "6 4", 3: "2 3"}

try:
    from tensorboard.backend.event_processing.event_accumulator import EventAccumulator
except ImportError:  # the per-Res table needs TensorBoard; the rest doesn't
    EventAccumulator = None


# ------------------------------------------------------------------------- runs

def design(cfg):
    controls = {"audio,cv,res": "cutoff + Res", "audio,cv": "cutoff", "audio": "fixed"}.get(cfg["inputs"], cfg["inputs"])
    if cfg.get("arch") == "wavenet":
        return f"{controls}, WaveNet FiLM"
    net = f"{cfg['cell'].upper()}-{cfg['hidden']}" + (f" x{cfg['layers']}" if cfg.get("layers", 1) > 1 else "")
    return f"{controls}, {net}"


def per_res(run, step):
    """Validation ESR per Res mark at one epoch, from the run's TensorBoard log."""
    if EventAccumulator is None or step is None:
        return {}
    acc = EventAccumulator(str(run), size_guidance={"scalars": 0})
    acc.Reload()
    out = {}
    for tag in acc.Tags().get("scalars", []):
        if tag.startswith("valid_res/"):
            at = [e.value for e in acc.Scalars(tag) if e.step == step]
            if at:
                out[tag.split("/", 1)[1]] = at[-1]
    return out


def read_run(run, processes):
    log = (run / "log.txt").read_text(errors="replace")
    cfg = json.loads((run / "config.json").read_text()) if (run / "config.json").exists() else {}
    # (epoch, train loss, valid ESR, lr, minutes, gradient median, gradient max, % clipped)
    epochs = [(int(m[1]), float(m[2]), float(m[3]), float(m[4]), float(m[8]),
               *((float(m[5]), float(m[6]), int(m[7])) if m[5] else (None, None, None)))
              for m in map(EPOCH.match, log.splitlines()) if m]
    result = json.loads((run / "result.json").read_text()) if (run / "result.json").exists() else None
    best = min(epochs, key=lambda e: e[2]) if epochs else None
    alive = any(f"--name {run.name} " in p or p.endswith(f"--name {run.name}") for p in processes)
    if result:
        state = "stopped early" if "no improvement" in log else "finished"
    elif alive:
        state = "training"
    else:
        state = "not running" + (": crashed" if "Traceback" in log else "")
    recent = epochs[-11:]
    pace = (recent[-1][4] - recent[0][4]) / (len(recent) - 1) if len(recent) > 1 else None
    return {
        "name": run.name, "cfg": cfg, "design": design(cfg) if cfg else "?", "seed": cfg.get("seed"),
        "epochs": epochs, "best": best, "result": result, "state": state, "pace": pace,
        "patience": cfg.get("patience", 30), "max_epochs": cfg.get("epochs", 300),
        "res": per_res(run, best[0] if best else None),
        "tail": log.strip().splitlines()[-3:] if "Traceback" in log else [],
    }


def runs(folder, pattern):
    processes = subprocess.run(["ps", "-Ao", "command"], capture_output=True, text=True).stdout.splitlines()
    pats = pattern.split(",")
    found = [d for d in sorted(pathlib.Path(folder).iterdir()) if (d / "log.txt").exists()
             and any(fnmatch.fnmatch(d.name, p) for p in pats)]
    order = list(COLOURS)  # the playable network first, then by size and seed
    return sorted((read_run(d, processes) for d in found),
                  key=lambda r: (order.index(r["design"]) if r["design"] in order else len(order),
                                 r["design"], r["seed"] or 0))


# ------------------------------------------------------------------------- page

def fmt(v, digits=4):
    return "–" if v is None else f"{v:.{digits}f}"


def chart(rs):
    pts = [(e[0], e[2]) for r in rs for e in r["epochs"]]
    if not pts:
        return "<p class=muted>No epochs logged yet.</p>"
    w, h, left, right, top, bottom = 760, 320, 52, 12, 20, 34
    xmax = max(50, max(p[0] for p in pts) + 5)
    lo = math.floor(math.log10(max(min(p[1] for p in pts), 1e-4)))
    peak = max(p[1] for p in pts)  # the axis ends at the next 1, 2 or 5 above the highest point
    hi = min(math.log10(m * 10 ** d) for d in range(lo, 4) for m in (1, 2, 5) if m * 10 ** d >= peak)
    hi = max(hi, lo + 1)
    X = lambda e: left + (w - left - right) * e / xmax  # noqa: E731
    Y = lambda v: top + (h - top - bottom) * (hi - math.log10(max(v, 10 ** lo))) / (hi - lo)  # noqa: E731
    g = []
    for d in range(lo, math.ceil(hi) + 1):
        for m in (1, 2, 5):
            v = m * 10 ** d
            if 10 ** lo <= v <= 10 ** hi * 1.0001:
                y = Y(v)
                g.append(f'<line x1="{left}" x2="{w - right}" y1="{y:.1f}" y2="{y:.1f}" class="{"grid" if m == 1 else "grid minor"}"/>')
                g.append(f'<text x="{left - 6}" y="{y + 4:.1f}" class="axis" text-anchor="end">{v:g}</text>')
    step = 50 if xmax > 150 else 25 if xmax > 60 else 10
    for e in range(0, xmax + 1, step):
        g.append(f'<line x1="{X(e):.1f}" x2="{X(e):.1f}" y1="{top}" y2="{h - bottom}" class="grid minor"/>')
        g.append(f'<text x="{X(e):.1f}" y="{h - bottom + 16}" class="axis" text-anchor="middle">{e}</text>')
    g.append(f'<text x="{(left + w - right) / 2}" y="{h - 2}" class="axis" text-anchor="middle">epoch</text>')
    for r in rs:
        if not r["epochs"]:
            continue
        line = " ".join(f"{X(e[0]):.1f},{Y(e[2]):.1f}" for e in r["epochs"])
        colour = COLOURS.get(r["design"], "var(--fg)")
        dash = DASHES.get(r["seed"], "")
        g.append(f'<polyline points="{line}" fill="none" stroke="{colour}" stroke-width="1.6"'
                 + (f' stroke-dasharray="{dash}"' if dash else "") + f'><title>{html.escape(r["name"])}</title></polyline>')
        b = r["best"]
        g.append(f'<circle cx="{X(b[0]):.1f}" cy="{Y(b[2]):.1f}" r="3.2" fill="{colour}"/>')
    legend = "".join(
        f'<span><svg width="30" height="10"><line x1="0" x2="30" y1="5" y2="5" stroke="{c}" stroke-width="2.4"/></svg>{html.escape(k)}</span>'
        for k, c in COLOURS.items() if any(r["design"] == k for r in rs))
    seeds = "".join(
        f'<span><svg width="30" height="10"><line x1="0" x2="30" y1="5" y2="5" stroke="var(--muted)" stroke-width="2"'
        + (f' stroke-dasharray="{d}"' if d else "") + f'/></svg>seed {s}</span>' for s, d in DASHES.items())
    return (f'<div class=chart><svg viewBox="0 0 {w} {h}" role="img" aria-label="validation ESR by epoch">'
            + "".join(g) + f'</svg></div><div class=legend>{legend}{seeds}<span>● best so far</span></div>')


def table(rs):
    rows = []
    for r in rs:
        b, last, res = r["best"], (r["epochs"][-1] if r["epochs"] else None), r["result"]
        since = (last[0] - b[0]) if b else None
        if r["state"] == "training" and b and r["pace"]:
            if r["patience"]:
                stop_by = b[0] + r["patience"]
                eta = f"epoch {stop_by} at the earliest, ~{(stop_by - last[0]) * r['pace']:.0f} min"
            else:  # a fixed budget: every epoch runs
                left = (r["max_epochs"] - 1 - last[0]) * r["pace"]
                eta = f"ends at epoch {r['max_epochs']}, ~{left / 60:.1f} h"
        else:
            eta = ""
        # Since the best epoch, out of the patience; with a fixed budget, the epochs done
        # out of the budget instead.
        span = r["patience"] or r["max_epochs"]
        done = since if r["patience"] else (last[0] + 1 if last else 0)
        if res:
            held = res.get("held_out")
            tests = list(res.get("test_take_by_res", {}).values())
            test_txt = (f"test takes {sum(tests) / len(tests):.4f}" if tests else "") + (f" · Res 5 {held:.4f}" if held is not None else "")
        else:
            test_txt = ""
        colour = COLOURS.get(r["design"], "var(--fg)")
        rows.append(
            f'<tr><td><span class=dot style="background:{colour}"></span>{html.escape(r["design"])}</td>'
            f'<td class=num>{r["seed"]}</td>'
            f'<td class=num>{last[0] if last else "–"}</td>'
            f'<td class=num><b>{fmt(b[2] if b else None)}</b></td>'
            f'<td class=num>{b[0] if b else "–"}</td>'
            f'<td class=num>{fmt(last[2] if last else None)}</td>'
            f'<td><span class=bar><span style="width:{min(100, 100 * (done or 0) / span):.0f}%"></span></span>'
            f'<span class=muted> {done if done is not None else "–"}/{span}</span></td>'
            f'<td class=num>{f"{last[3]:.1e}" if last else "–"}</td>'
            + (f'<td class=num>{last[5]:.3g}<div class=small>max {last[6]:.3g} · {last[7]}% clipped</div></td>'
               if last and last[5] is not None else '<td class=num>–</td>')
            + f'<td><span class="state {r["state"].split(":")[0].replace(" ", "-")}">{r["state"]}</span>'
            + (f'<div class=small>{html.escape(eta)}</div>' if eta else "")
            + (f'<div class=small>{html.escape(test_txt)}</div>' if test_txt else "")
            + "".join(f'<div class="small err">{html.escape(t)}</div>' for t in r["tail"]) + "</td></tr>")
    return ('<div class=scroll><table><thead><tr><th>Network</th><th>Seed</th><th>Epoch</th>'
            '<th>Best valid ESR</th><th>at epoch</th><th>Latest</th>'
            + ('<th>Since best</th>' if any(r["patience"] for r in rs) else '<th>Progress</th>') + '<th>lr</th>'
            '<th>Gradient</th><th>State</th>'
            '</tr></thead><tbody>' + "".join(rows) + "</tbody></table></div>")


def res_table(rs):
    marks = sorted({k for r in rs for k in r["res"]}, key=float)
    if not marks:
        return ""
    head = "".join(f"<th>{m}</th>" for m in marks)
    body = ""
    for r in rs:
        if not r["res"]:
            continue
        cells = "".join(f'<td class=num>{fmt(r["res"].get(m), 3) if m in r["res"] else ""}</td>' for m in marks)
        body += f'<tr><td>{html.escape(r["design"])} · seed {r["seed"]}</td>{cells}</tr>'
    return ('<h2>Validation ESR by Res mark, at each run\'s best epoch</h2>'
            '<p class=muted>Res 5 is held out, so it never appears here; it is scored once, at the end.</p>'
            f'<div class=scroll><table><thead><tr><th>Run</th>{head}</tr></thead><tbody>{body}</tbody></table></div>')


CSS = """
:root { --bg:#f6f5f3; --panel:#ffffff; --fg:#1c1b1a; --muted:#77736e; --line:#e2dfdb; --accent:#c8102e;
  --c1:#c8102e; --c2:#e07b00; --c3:#2459c4; --c4:#13917f; --ok:#1d7a3e; --warn:#b3261e; color-scheme:light }
@media (prefers-color-scheme: dark) { :root:not([data-theme="light"]) { --bg:#121212; --panel:#1b1b1b; --fg:#ecebe9;
  --muted:#9b9893; --line:#2c2b2a; --accent:#ff4d5e; --c1:#ff4d5e; --c2:#ffa23a; --c3:#6f9bff; --c4:#3cc7b1;
  --ok:#5cc97f; --warn:#ff7a70; color-scheme:dark } }
:root[data-theme="dark"] { --bg:#121212; --panel:#1b1b1b; --fg:#ecebe9; --muted:#9b9893; --line:#2c2b2a;
  --accent:#ff4d5e; --c1:#ff4d5e; --c2:#ffa23a; --c3:#6f9bff; --c4:#3cc7b1; --ok:#5cc97f; --warn:#ff7a70; color-scheme:dark }
* { box-sizing:border-box }
body { margin:0; background:var(--bg); color:var(--fg); font:14px/1.45 "IBM Plex Sans", system-ui, sans-serif }
main { max-width:1240px; margin:0 auto; padding-block:24px 48px;
  padding-left:max(16px, env(safe-area-inset-left, 0px)); padding-right:max(16px, env(safe-area-inset-right, 0px));
  display:grid; grid-template-columns:minmax(0, 1fr); gap:18px }
h1 { font-size:22px; margin:0; text-wrap:balance } h1 b { color:var(--accent) }
h2 { font-size:15px; margin:8px 0 0; text-wrap:balance }
.muted { color:var(--muted) } .small { font-size:12px; color:var(--muted) } .err { color:var(--warn) }
.summary { display:flex; flex-wrap:wrap; gap:8px 20px; color:var(--muted) } .summary b { color:var(--fg) }
.scroll { overflow-x:auto; background:var(--panel); border:1px solid var(--line); border-radius:8px }
table { border-collapse:collapse; width:100%; min-width:640px }
th, td { padding:7px 10px; border-bottom:1px solid var(--line); text-align:left; vertical-align:top; white-space:nowrap }
th { font-size:11px; letter-spacing:.06em; text-transform:uppercase; color:var(--muted); font-weight:600 }
tr:last-child td { border-bottom:0 }
td:last-child { white-space:normal; min-width:170px }
.num { font-variant-numeric:tabular-nums; font-family:"IBM Plex Mono", ui-monospace, monospace; font-size:13px }
.dot { display:inline-block; width:9px; height:9px; border-radius:50%; margin-right:7px }
.bar { display:inline-block; width:60px; height:6px; background:var(--line); border-radius:3px; vertical-align:middle; overflow:hidden }
.bar span { display:block; height:100%; background:var(--muted) }
.state { font-size:12px; font-weight:600 } .state.training { color:var(--accent) }
.state.finished, .state.stopped-early { color:var(--ok) } .state.not-running { color:var(--warn) }
.chart { background:var(--panel); border:1px solid var(--line); border-radius:8px; padding:8px }
.chart svg { width:100%; height:auto; display:block }
.grid { stroke:var(--line); stroke-width:1 } .grid.minor { stroke-dasharray:2 4 }
.axis { fill:var(--muted); font-size:11px; font-family:"IBM Plex Mono", ui-monospace, monospace }
.legend { display:flex; flex-wrap:wrap; gap:6px 16px; font-size:12px; color:var(--muted) }
.legend span { display:inline-flex; align-items:center; gap:6px }
"""


def page(args):
    rs = runs(args.dir, args.runs)
    training = sum(r["state"] == "training" for r in rs)
    done = sum(r["result"] is not None for r in rs)
    best = min((r["best"][2] for r in rs if r["best"]), default=None)
    series = sorted({r["cfg"].get("series") or "" for r in rs} - {""})
    heading = " and ".join(s.upper() for s in series) + " series" if series else args.runs
    return f"""<!doctype html><html lang=en><head><meta charset=utf-8>
<meta name=viewport content="width=device-width, initial-scale=1, viewport-fit=cover">
<meta http-equiv=refresh content=60><title>Wasp GRU runs</title>
<link rel=stylesheet href="https://fonts.googleapis.com/css2?family=IBM+Plex+Mono&family=IBM+Plex+Sans:wght@400;600&display=swap">
<style>{CSS}</style></head><body><main>
<h1>Wasp GRU runs <b>·</b> {html.escape(heading)}</h1>
<div class=summary><span><b>{training}</b> training</span><span><b>{done}</b> of {len(rs)} finished</span>
<span>best validation ESR so far <b>{fmt(best)}</b></span><span>updated {time.strftime("%H:%M:%S")}, reloads every minute</span></div>
{table(rs)}
<h2>Validation ESR by epoch</h2>
<p class=muted>Pooled over the validation blocks (beginning, middle and end of every sweep take). Log scale; lower is better.
{(f'A run stops after {rs[0]["patience"]} epochs without a new best, or at {rs[0]["max_epochs"]}.') if rs and rs[0]["patience"] else (f'Every run goes the full {rs[0]["max_epochs"]} epochs; the best by validation ESR is kept.' if rs else '')}</p>
{chart(rs)}
{res_table(rs)}
</main></body></html>"""


class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path not in ("/", "/index.html"):
            self.send_error(404)
            return
        try:
            body, status = page(self.server.args), 200
        except Exception:  # show the problem on the page rather than a dead connection
            body, status = f"<pre>{html.escape(traceback.format_exc())}</pre>", 500
        data = body.encode()
        self.send_response(status)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Cache-Control", "no-store")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def log_message(self, *_):
        pass


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--dir", default=str(RUNS), help="where the runs are")
    ap.add_argument("--runs", default="lp_cr_*,lp6_c_*", help="run folders to show, comma-separated globs")
    ap.add_argument("--port", type=int, default=8124)
    ap.add_argument("--once", action="store_true", help="print the page and exit")
    args = ap.parse_args()
    if args.once:
        print(page(args))
        return
    server = ThreadingHTTPServer(("0.0.0.0", args.port), Handler)
    server.args = args
    print(f"serving on port {args.port}", flush=True)
    server.serve_forever()


if __name__ == "__main__":
    main()
