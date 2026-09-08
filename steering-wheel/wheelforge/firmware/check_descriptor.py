"""Validate the HID report descriptor.

A malformed descriptor does not fail loudly. Windows either rejects the device
with a code 43 and no explanation, or accepts it and quietly ignores the parts
it could not parse -- which for a force feedback wheel means it enumerates fine
and never receives a single force. Nothing on the PC side will tell you why.

So this walks the descriptor the way a host parser does and checks:

  * every item is well formed and no bytes are left over
  * collections balance
  * the joystick input report matches the C struct actually being sent
  * every report is a whole number of bytes
  * the PID reports a game needs are all present

    python check_descriptor.py
"""

import re
import sys
from pathlib import Path

HERE = Path(__file__).parent
SKETCH = HERE / "wheelforge" / "wheelforge.ino"
DESCRIPTOR = HERE / "wheelforge" / "hid_descriptor.h"

C_TYPE_BITS = {
    "uint8_t": 8, "int8_t": 8,
    "uint16_t": 16, "int16_t": 16,
    "uint32_t": 32, "int32_t": 32,
}

# Main item tags.
INPUT, OUTPUT, FEATURE = 0x80, 0x90, 0xB0
COLLECTION, END_COLLECTION = 0xA0, 0xC0

# Global item tags.
USAGE_PAGE, LOGICAL_MIN, LOGICAL_MAX = 0x04, 0x14, 0x24
PHYSICAL_MIN, PHYSICAL_MAX = 0x34, 0x44
UNIT_EXP, UNIT = 0x54, 0x64
REPORT_SIZE, REPORT_ID, REPORT_COUNT = 0x74, 0x84, 0x94
PUSH, POP = 0xA4, 0xB4

# Local item tags.
USAGE, USAGE_MIN, USAGE_MAX = 0x08, 0x18, 0x28

KNOWN_TAGS = {
    INPUT, OUTPUT, FEATURE, COLLECTION, END_COLLECTION,
    USAGE_PAGE, LOGICAL_MIN, LOGICAL_MAX, PHYSICAL_MIN, PHYSICAL_MAX,
    UNIT_EXP, UNIT, REPORT_SIZE, REPORT_ID, REPORT_COUNT, PUSH, POP,
    USAGE, USAGE_MIN, USAGE_MAX,
    0x38,  # designator index
    0x78,  # string index
    0x88,  # string minimum
    0x98,  # string maximum
    0xA8,  # delimiter
}

# The PID reports DirectInput will not work without.
# name, and the payload size in bytes that ffb.h is written to parse. The
# parser indexes these reports by hand, so a descriptor change that moves a
# field has to fail here rather than quietly reading the wrong byte.
REQUIRED_OUTPUT_IDS = {
    1:  ("set effect", 13),
    2:  ("set envelope", 7),
    3:  ("set condition", 8),
    4:  ("set periodic", 6),
    5:  ("set constant force", 3),
    6:  ("set ramp force", 3),
    10: ("effect operation", 3),
    11: ("block free", 1),
    12: ("device control", 1),
    13: ("device gain", 1),
}
REQUIRED_FEATURE_IDS = {
    5: ("create new effect", 3),
    6: ("block load", 4),
    7: ("pool report", 4),
}


def defines(text):
    out = {}
    for name, value in re.findall(r"#define\s+(\w+)\s+(0x[0-9a-fA-F]+|\d+)\s*$",
                                  text, re.M):
        out[name] = int(value, 0)
    return out


def descriptor_bytes(text, macros):
    """Every comma separated entry must resolve to a number.

    Skipping anything unrecognised would shift each following byte along by one
    and parse a completely different descriptor -- which is exactly the sort of
    quietly wrong answer this script exists to prevent.
    """
    m = re.search(r"kReportDescriptor\[\]\s*\w*\s*=\s*\{(.*?)\n\};", text, re.S)
    if not m:
        raise SystemExit("could not find kReportDescriptor")

    body = re.sub(r"//[^\n]*", "", m.group(1))

    values = []
    for raw in body.split(","):
        tok = raw.strip()
        if not tok:
            continue
        if re.fullmatch(r"0x[0-9a-fA-F]+|\d+", tok):
            values.append(int(tok, 0))
        elif tok in macros:
            values.append(macros[tok])
        else:
            raise SystemExit("descriptor entry %r is neither a number nor a known "
                             "#define -- refusing to guess" % tok)

    for i, v in enumerate(values):
        if not 0 <= v <= 255:
            raise SystemExit("descriptor byte %d is %d, which is not a byte" % (i, v))

    return values


def walk(items):
    """Parse like a host would. Returns (reports, problems, depth_trace)."""
    reports = {"input": {}, "output": {}, "feature": {}}
    problems = []

    i = 0
    size = count = 0
    report_id = 0
    depth = 0
    max_depth = 0

    while i < len(items):
        prefix = items[i]

        if prefix == 0xFE:
            problems.append("long items are not supported by this checker")
            break

        length = prefix & 0x03
        if length == 3:
            length = 4
        tag = prefix & 0xFC

        if i + length >= len(items) + 0 and i + 1 + length > len(items):
            problems.append("item at byte %d claims %d data bytes but the "
                            "descriptor ends" % (i, length))
            break

        if tag not in KNOWN_TAGS:
            problems.append("unknown item tag 0x%02X at byte %d" % (tag, i))

        data = 0
        for b in range(length):
            data |= items[i + 1 + b] << (8 * b)

        if tag == REPORT_SIZE:
            size = data
        elif tag == REPORT_COUNT:
            count = data
        elif tag == REPORT_ID:
            report_id = data
            if data == 0:
                problems.append("report id 0 is reserved")
        elif tag == COLLECTION:
            depth += 1
            max_depth = max(max_depth, depth)
        elif tag == END_COLLECTION:
            depth -= 1
            if depth < 0:
                problems.append("End Collection at byte %d with no open collection" % i)
        elif tag in (INPUT, OUTPUT, FEATURE):
            kind = {INPUT: "input", OUTPUT: "output", FEATURE: "feature"}[tag]
            reports[kind][report_id] = reports[kind].get(report_id, 0) + size * count

        i += 1 + length

    if i != len(items):
        problems.append("descriptor does not end on an item boundary "
                        "(stopped at %d of %d)" % (i, len(items)))
    if depth != 0:
        problems.append("%d collection(s) left open" % depth)

    return reports, problems


def struct_bits(text):
    m = re.search(r"struct\s+__attribute__\(\(packed\)\)\s+WheelReport\s*\{(.*?)\};",
                  text, re.S)
    if not m:
        raise SystemExit("could not find struct WheelReport")

    bits = 0
    for line in m.group(1).splitlines():
        line = re.sub(r"//.*", "", line).strip().rstrip(";")
        if not line:
            continue
        parts = line.split()
        ctype = parts[0]
        if ctype not in C_TYPE_BITS:
            raise SystemExit("unhandled type in WheelReport: " + ctype)
        arr = re.search(r"\[(\d+)\]", " ".join(parts[1:]))
        bits += C_TYPE_BITS[ctype] * (int(arr.group(1)) if arr else 1)
    return bits


def main():
    sketch = SKETCH.read_text(encoding="utf-8")
    header = DESCRIPTOR.read_text(encoding="utf-8")

    macros = defines(header)
    macros.update(defines(sketch))

    items = descriptor_bytes(header, macros)
    reports, problems = walk(items)
    actual = struct_bits(sketch)

    print("descriptor: %d bytes" % len(items))
    print()
    for kind in ("input", "output", "feature"):
        ids = reports[kind]
        if not ids:
            continue
        print("%-8s %s" % (kind, ", ".join(
            "id %d = %d bytes" % (rid, bits // 8) for rid, bits in sorted(ids.items()))))
    print()

    joystick = reports["input"].get(1, 0)
    print("joystick input report : %d bits (%d bytes)" % (joystick, joystick // 8))
    print("WheelReport struct    : %d bits (%d bytes)" % (actual, actual // 8))
    print()

    if joystick != actual:
        problems.append("joystick descriptor and struct disagree by %d bits"
                        % abs(joystick - actual))

    for kind in ("input", "output", "feature"):
        for rid, bits in sorted(reports[kind].items()):
            if bits % 8:
                problems.append("%s report %d is %d bits, not a whole number of bytes"
                                % (kind, rid, bits))

    for kind, required in (("output", REQUIRED_OUTPUT_IDS),
                           ("feature", REQUIRED_FEATURE_IDS)):
        for rid, (name, expected) in sorted(required.items()):
            if rid not in reports[kind]:
                problems.append("missing %s report %d (%s) -- games need it"
                                % (kind, rid, name))
                continue
            got = reports[kind][rid] // 8
            if got != expected:
                problems.append("%s report %d (%s) is %d bytes, but ffb.h parses "
                                "it as %d" % (kind, rid, name, got, expected))

    if problems:
        for p in problems:
            print("FAIL: " + p)
        return 1

    print("OK: descriptor parses, collections balance, reports are byte aligned,")
    print("    the joystick report matches the struct, and every PID report a")
    print("    game needs is present.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
