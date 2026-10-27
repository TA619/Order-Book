"""Plot the per-event latency distribution from the latency tool's CSV.

Usage:
    latency <message.csv> <orderbook.csv> --csv data/latency.csv
    python scripts/plot_latency.py data/latency.csv [out.png] [--title MSFT]

The CSV has one row per event: type (LOBSTER event type 1-7), ns.
"""

import argparse

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
import pandas as pd

NAMES = {1: "submit", 3: "delete", 4: "execute"}


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("csv")
    ap.add_argument("out", nargs="?", default=None)
    ap.add_argument("--title", default="")
    args = ap.parse_args()

    df = pd.read_csv(args.csv)
    bins = np.logspace(1, 5, 49)  # 10 ns .. 100 us, 12 bins per decade (samples step ~10 ns)

    fig, ax = plt.subplots(figsize=(10, 4.8))
    for t, name in NAMES.items():
        ns = df.loc[df["type"] == t, "ns"].clip(lower=10)
        ax.hist(ns, bins=bins, histtype="step", linewidth=1.3,
                label=f"{name} ({len(ns):,})")

    all_ns = df["ns"]
    for q, style in ((0.50, "-"), (0.99, "--"), (0.999, ":")):
        v = all_ns.quantile(q)
        ax.axvline(v, color="black", linewidth=0.9, linestyle=style)
        ax.text(v * 1.05, ax.get_ylim()[1] * 0.92, f"p{q * 100:g} = {v:,.0f} ns",
                fontsize=9, rotation=90, va="top")

    ax.set_xscale("log")
    ax.set_xlabel("latency per event (ns, log scale)")
    ax.set_ylabel("events")
    ax.set_title(f"{args.title} per-event latency, full-day replay".strip())
    ax.legend(loc="upper right")
    ax.grid(alpha=0.3, which="both")
    fig.tight_layout()

    out = args.out or args.csv.rsplit(".", 1)[0] + ".png"
    fig.savefig(out, dpi=120)
    print(f"wrote {out}")


if __name__ == "__main__":
    main()
