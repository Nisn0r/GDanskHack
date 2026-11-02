#!/usr/bin/env python3
"""Non-halting RTT reader (up-buffer 0) built on JLinkExe.

JLinkRTTLogger doesn't reliably find the control block on this setup, so this
polls the SEGGER RTT ring buffer through plain memory accesses (the core keeps
running), appends new text to a log file and advances RdOff so the firmware's
non-blocking buffer never fills up.

    tools/rtt_poll.py <rtt_control_block_addr> <logfile> [period_s]
"""
import os
import re
import subprocess
import sys
import tempfile
import time

DEVICE = "EFR32MG21A020F768IM32"
JLINK = ["JLinkExe", "-device", DEVICE, "-if", "SWD", "-speed", "4000",
         "-autoconnect", "1", "-NoGui", "1"]


def jlink(commands):
    with tempfile.NamedTemporaryFile("w", suffix=".jlink", delete=False) as f:
        f.write("connect\n" + "\n".join(commands) + "\nexit\n")
        path = f.name
    try:
        return subprocess.run(JLINK + ["-CommandFile", path], capture_output=True,
                              text=True, timeout=30).stdout
    finally:
        os.unlink(path)


def words(out, addr, count):
    vals = []
    for line in out.splitlines():
        m = re.match(r"^([0-9A-F]{8}) = ((?:[0-9A-F]{8} ?)+)", line)
        if m and addr <= int(m.group(1), 16) < addr + 4 * count:
            vals += [int(v, 16) for v in m.group(2).split()]
    return vals[:count]


def main():
    cb = int(sys.argv[1], 16)
    log = sys.argv[2]
    period = float(sys.argv[3]) if len(sys.argv) > 3 else 1.0
    desc = cb + 24                      # aUp[0]: sName pBuffer Size WrOff RdOff Flags
    dump = tempfile.mktemp(suffix=".bin")
    while True:
        try:
            out = jlink([f"mem32 {cb:#x} 1", f"mem32 {desc:#x} 6"])
            magic = words(out, cb, 1)
            d = words(out, desc, 6)
            if magic != [0x47474553] or len(d) != 6:   # "SEGG"
                time.sleep(period)
                continue
            _, buf, size, wr, rd, _ = d
            if wr != rd and 0 < size <= 0x10000 and wr < size and rd < size:
                out = jlink([f"savebin {dump} {buf:#x} {size:#x}",
                             f"w4 {desc + 16:#x} {wr:#x}"])
                with open(dump, "rb") as f:
                    data = f.read()
                chunk = data[rd:wr] if wr > rd else data[rd:] + data[:wr]
                with open(log, "ab") as f:
                    f.write(chunk)
                sys.stdout.write(chunk.decode("utf-8", "replace"))
                sys.stdout.flush()
        except subprocess.TimeoutExpired:
            pass
        time.sleep(period)


if __name__ == "__main__":
    main()
