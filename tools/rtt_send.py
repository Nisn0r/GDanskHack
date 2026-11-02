#!/usr/bin/env python3
"""Send one line to the firmware CLI through RTT down-buffer 0 (non-halting).

    tools/rtt_send.py <rtt_control_block_addr> "energy-scan 0x07FFF800 4"

Pair with tools/rtt_poll.py to read the answer.
"""
import sys
import time

sys.path.insert(0, __import__("os").path.dirname(__file__))
from rtt_poll import jlink, words  # noqa: E402


def read_words(addr, count, tries=20):
    """The probe may be busy with rtt_poll.py: retry until the read parses."""
    for _ in range(tries):
        vals = words(jlink([f"mem32 {addr:#x} {count}"]), addr, count)
        if len(vals) == count:
            return vals
        time.sleep(0.3)
    sys.exit("J-Link busy or target not responding")


def main():
    cb = int(sys.argv[1], 16)
    data = (sys.argv[2] + "\r\n").encode()
    max_up, _ = read_words(cb + 16, 2)
    desc = cb + 24 + 24 * max_up          # aDown[0]: sName pBuffer Size WrOff RdOff Flags
    _, buf, size, wr, rd, _ = read_words(desc, 6)
    free = (rd - wr - 1) % size
    if len(data) > free:
        sys.exit(f"down buffer full ({free} bytes free)")
    cmds = []
    for b in data:
        cmds.append(f"w1 {buf + wr:#x} {b:#x}")
        wr = (wr + 1) % size
    cmds.append(f"w4 {desc + 12:#x} {wr:#x}")
    cmds.append(f"mem32 {desc + 12:#x} 1")
    for _ in range(20):
        if words(jlink(cmds), desc + 12, 1) == [wr]:
            return
        time.sleep(0.3)
    sys.exit("write not confirmed (J-Link busy?)")


if __name__ == "__main__":
    main()
