"""Summarise the MA3_SNDWATCH guest-state log (see MA3_SNDWATCH in src/ma3.c).

The watch samples the firmware sound driver's state bytes at 0x00407F44 once per
instruction and logs every transition:

    <tick> -------- SND mask=<hh> req=<hh> pulse=<hh> run=<hh> irq=<hh> en=<hh> eos=<hh>[ ctr=<hhhh>]

    req   +0x21  stop request            pulse +0x22  stop-pulse pending
    run   +0x23  running                 irq   +0x25  saved IRQ status
    en    +0x26  handler enable mask     eos   +0x28  0 normal, 1 EOS seen, 2 stop pending
    mask         last guest value written to MA-3 0x162 (the interrupt mask)
    ctr          MaSound_ReceiveMessage(0,0,0x80) block/loop counter (sentinel 0xFFFF)

The suspect silence chain is

    ctr dry -> eos=1 -> eos=2 -> next FIFO IRQ -> mask=9f (MaDevDrv_StopSequencer)

so this script prints the transition timeline and, for each mask=9f write, the
most recent eos=1/eos=2 transition that preceded it. A frontend run attached with
--ma5-hw is required: the offline replay has no CPU and maps no RAM, so the watch
stays silent there.

Usage: python tools\\snd_watch_report.py [build/ma3-sndwatch.log] [ticks_per_sec]
"""
import re
import sys

SND = re.compile(
    r"^(\d+) -+ SND mask=(\w\w) req=(\w\w) pulse=(\w\w) run=(\w\w) "
    r"irq=(\w\w) en=(\w\w) eos=(\w\w)(?: ctr=(\w\w\w\w))?\s*$")


def parse(path):
    events = []
    for line in open(path, "r", encoding="utf-8", errors="replace"):
        match = SND.match(line)
        if not match:
            continue
        events.append({
            "tick": int(match.group(1)),
            "mask": int(match.group(2), 16),
            "req": int(match.group(3), 16),
            "pulse": int(match.group(4), 16),
            "run": int(match.group(5), 16),
            "irq": int(match.group(6), 16),
            "en": int(match.group(7), 16),
            "eos": int(match.group(8), 16),
            "ctr": int(match.group(9), 16) if match.group(9) else None,
        })
    return events


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else "build/ma3-sndwatch.log"
    hz = int(sys.argv[2]) if len(sys.argv) > 2 else 1000000000
    try:
        events = parse(path)
    except OSError as error:
        print("cannot read %s: %s" % (path, error))
        return 1
    print("%s: %d transitions (ticks_hz=%d)" % (path, len(events), hz))
    if not events:
        print("no transitions: the frontend never reached the sound driver, or the")
        print("offline replay (no CPU, no RAM) was used instead of the frontend.")
        return 0
    print("\n   time(s)      tick mask req pulse run irq  en eos  ctr  notes")
    last_mask = last_eos = None
    for event in events:
        notes = []
        if last_eos is not None and event["eos"] != last_eos:
            notes.append("eos %d->%d" % (last_eos, event["eos"]))
        if last_mask is not None and event["mask"] != last_mask:
            if event["mask"] == 0x9F:
                notes.append("mask %02x->9f (MaDevDrv_StopSequencer)" % last_mask)
            elif event["mask"] == 0xFF:
                notes.append("mask %02x->ff (MaDevDrv_StartSequencer)" % last_mask)
            else:
                notes.append("mask %02x->%02x" % (last_mask, event["mask"]))
        last_mask, last_eos = event["mask"], event["eos"]
        print("  %8.3f %9d %4s %3d %5d %3d %3d %3d %3d %5s  %s" % (
            event["tick"] / hz, event["tick"], "%02x" % event["mask"],
            event["req"], event["pulse"], event["run"], event["irq"], event["en"],
            event["eos"], "%04x" % event["ctr"] if event["ctr"] is not None else "-",
            ", ".join(notes)))
    print("\nverdict (silence chain: eos=1 -> eos=2 -> mask=9f):")
    pending = []
    stops = 0
    for event in events:
        if event["eos"] in (1, 2):
            pending.append((event["eos"], event["tick"]))
        if event["eos"] == 0 and event["mask"] == 0xFF:
            pending = []
        if event["mask"] == 0x9F:
            stops += 1
            ones = [e for e in pending if e[0] == 1]
            twos = [e for e in pending if e[0] == 2]
            if ones and twos:
                print("  tick %d: mask=9f preceded by eos=1@%d and eos=2@%d -> chain holds"
                      % (event["tick"], ones[-1][1], twos[-1][1]))
            else:
                print("  tick %d: mask=9f with no preceding eos=1/2 -> chain broken"
                      % event["tick"])
            pending = []
    print("  stop events: %d; start events: %d" % (
        stops, sum(1 for e in events if e["mask"] == 0xFF)))


if __name__ == "__main__":
    sys.exit(main())
