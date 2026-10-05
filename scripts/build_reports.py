#!/usr/bin/env python3
import csv
import html
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TEMPLATE_DIR = ROOT / "docs" / "report"
DOCS = ["explicacion_pcam", "evidencia"]
VERSION_LABELS = {
    "sequential": "A secuencial",
    "parallel": "B directa",
    "optimized": "C pares",
    "atomic": "C atomic",
}
REQUIRED = [
    (1, "sequential", "-"),
    (2, "static", "default"),
    (4, "static", "default"),
    (8, "static", "default"),
    (8, "static", "8"),
    (8, "static", "64"),
    (8, "dynamic", "8"),
    (8, "dynamic", "64"),
    (8, "guided", "default"),
]
SCHEDULES = [(s, c) for s in ("static", "dynamic", "guided") for c in ("default", "8", "64")]


def load_rows():
    with open(ROOT / "results.csv", newline="") as f:
        return list(csv.DictReader(f))


def find(rows, version, threads, schedule, chunk):
    for row in rows:
        if (row["version"], row["threads"], row["schedule"], row["chunk"]) == (
            version, str(threads), schedule, chunk,
        ):
            return row
    return None


def decimal(value, digits):
    return f"{float(value):.{digits}f}".replace(".", ",")


def fmt_time(row):
    return decimal(row["avg_time"], 3) if row else "—"


def fmt_ratio(row):
    return f"{decimal(row['speedup'], 2)} / {decimal(row['efficiency'], 2)}" if row else "—"


def table(headers, body, classes=""):
    head = "".join(f"<th>{html.escape(h)}</th>" for h in headers)
    rows = "".join("<tr>" + "".join(f"<td>{c}</td>" for c in r) + "</tr>" for r in body)
    return f'<table class="{classes}"><thead><tr>{head}</tr></thead><tbody>{rows}</tbody></table>'


def required_table(rows):
    body = []
    for threads, schedule, chunk in REQUIRED:
        if schedule == "sequential":
            seq = find(rows, "sequential", 1, "sequential", "-")
            body.append([threads, schedule, chunk, fmt_time(seq), "—", fmt_ratio(seq), "—"])
            continue
        b = find(rows, "parallel", threads, schedule, chunk)
        c = find(rows, "optimized", threads, schedule, chunk)
        body.append([threads, schedule, chunk, fmt_time(b), fmt_time(c), fmt_ratio(b), fmt_ratio(c)])
    headers = ["Threads", "Schedule", "Chunk", "T̄ B (s)", "T̄ C (s)", "S / E  B", "S / E  C"]
    return table(headers, body, "num")


def schedule_table(rows):
    body = []
    for schedule, chunk in SCHEDULES:
        b = find(rows, "parallel", 8, schedule, chunk)
        c = find(rows, "optimized", 8, schedule, chunk)
        body.append([schedule, chunk, fmt_time(b), fmt_ratio(b), fmt_time(c), fmt_ratio(c)])
    headers = ["Schedule", "Chunk", "T̄ B (s)", "S / E  B", "T̄ C (s)", "S / E  C"]
    return table(headers, body, "num")


def scaling_table(rows):
    threads = sorted({int(r["threads"]) for r in rows if r["version"] == "parallel"})
    body = []
    for t in threads:
        b = find(rows, "parallel", t, "static", "default")
        c = find(rows, "optimized", t, "static", "default")
        body.append([t, fmt_time(b), fmt_ratio(b), fmt_time(c), fmt_ratio(c)])
    headers = ["Threads", "T̄ B (s)", "S / E  B", "T̄ C (s)", "S / E  C"]
    return table(headers, body, "num")


def full_table(rows):
    body = []
    for r in rows:
        body.append([
            VERSION_LABELS.get(r["version"], r["version"]), r["threads"], r["schedule"], r["chunk"],
            decimal(r["run1"], 3), decimal(r["run2"], 3), decimal(r["run3"], 3), decimal(r["avg_time"], 3),
            decimal(r["speedup"], 2), decimal(r["efficiency"], 2), r["checksum"],
        ])
    headers = ["Versión", "T", "Schedule", "Chunk", "Run 1", "Run 2", "Run 3", "T̄ (s)", "S", "E",
               "Checksum"]
    return table(headers, body, "num compact")


def text_block(path):
    p = ROOT / path
    return f"<pre>{html.escape(p.read_text().rstrip())}</pre>" if p.exists() else "<pre>—</pre>"


def placeholders(rows):
    return {
        "root": ROOT.as_uri(),
        "table_required": required_table(rows),
        "table_schedules": schedule_table(rows),
        "table_scaling": scaling_table(rows),
        "table_full": full_table(rows),
        "environment": text_block("results/environment.txt"),
        "correctness": text_block("evidence/correctness.txt"),
    }


def find_chromium():
    for name in ("chromium", "chromium-browser", "google-chrome-stable", "google-chrome"):
        path = shutil.which(name)
        if path:
            return path
    mac = "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"
    return mac if Path(mac).exists() else None


def main():
    chromium = find_chromium()
    if not chromium:
        print("No se encontró Chromium/Chrome para imprimir a PDF", file=sys.stderr)
        return 1
    values = placeholders(load_rows())
    with tempfile.TemporaryDirectory() as tmp:
        for name in DOCS:
            source = (TEMPLATE_DIR / f"{name}.html").read_text()
            for key, value in values.items():
                source = source.replace("{{" + key + "}}", value)
            rendered = Path(tmp) / f"{name}.html"
            rendered.write_text(source)
            target = ROOT / "docs" / f"{name}.pdf"
            subprocess.run(
                [chromium, "--headless", "--disable-gpu", "--no-pdf-header-footer",
                 f"--print-to-pdf={target}", rendered.as_uri()],
                check=True, capture_output=True,
            )
            print(target.relative_to(ROOT))
    return 0


if __name__ == "__main__":
    sys.exit(main())
