"""Plot mid price and bid-ask spread from the replay tool's CSV output.

Usage:
    replay <message.csv> <orderbook.csv> --csv data/spread.csv
    python scripts/plot_spread.py data/spread.csv [out.png] [--title AMZN]

The CSV has one row per second: time_s, bid, ask, mid, spread, with prices in
LOBSTER units (dollars * 10000).
"""

import argparse

import matplotlib

matplotlib.use("Agg")  # render to file, no window needed
import matplotlib.pyplot as plt
import pandas as pd

TICKS_PER_DOLLAR = 10_000


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("csv")
    ap.add_argument("out", nargs="?", default=None)
    ap.add_argument("--title", default="")
    args = ap.parse_args()

    df = pd.read_csv(args.csv)
    t = pd.to_datetime(df["time_s"], unit="s")  # seconds after midnight -> clock time
    mid = df["mid"] / TICKS_PER_DOLLAR
    spread_cents = df["spread"] / TICKS_PER_DOLLAR * 100

    fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(11, 6), sharex=True,
                                   gridspec_kw={"height_ratios": [2, 1]})
    ax1.plot(t, mid, linewidth=0.8, color="#1f5fa8")
    ax1.set_ylabel("Mid price ($)")
    ax1.set_title(f"{args.title} mid price and bid-ask spread (1 s samples)".strip())
    ax1.grid(alpha=0.3)

    ax2.plot(t, spread_cents, linewidth=0.5, color="#c0504d")
    ax2.axhline(spread_cents.median(), color="black", linewidth=0.8, linestyle="--",
                label=f"median {spread_cents.median():.1f}c")
    ax2.set_ylabel("Spread (cents)")
    ax2.set_xlabel("Time (ET)")
    ax2.legend(loc="upper right")
    ax2.grid(alpha=0.3)
    ax2.xaxis.set_major_formatter(matplotlib.dates.DateFormatter("%H:%M"))

    fig.tight_layout()
    out = args.out or args.csv.rsplit(".", 1)[0] + ".png"
    fig.savefig(out, dpi=120)
    print(f"wrote {out}")


if __name__ == "__main__":
    main()
