#!/usr/bin/env python3
"""Parse Creature OS binary logs (see docs/data-protocol.md; C encoder: firmware/logger/src/logfmt.c).

This file was REWRITTEN to satisfy tools/test_log_parser.py after an earlier version was
overwritten by mistake; the API (read_log, export_csv, summary, crc16_ccitt_false, LogFormatError)
is reconstructed from that test file.

CLI:  python3 tools/log_parser.py LOGFILE [--csv-dir DIR]
"""
import argparse
import csv
import struct
import sys
import zlib
from dataclasses import dataclass, field
from pathlib import Path

MAGIC = b"CRLG"
HEADER_SIZE = 32
REC_FIXED = 14
MAX_VALUES = 15          # n is a 4-bit field; the C side currently limits it to LOGFMT_MAX_VALUES
FLAG_READ_ERROR = 0x1

ORGAN_NAMES = {
    0: "light", 1: "thermo", 2: "touch", 3: "hunger", 4: "led", 5: "buzzer",
    6: "hearing", 7: "ears", 8: "light_ref", 9: "thermo_ref", 10: "mcu_temp", 14: "error", 15: "marker",
    20: "hear_l", 21: "hear_c", 22: "hear_r", 23: "mic_ref",
}


class LogFormatError(Exception):
    pass


def crc16_ccitt_false(data: bytes) -> int:
    crc = 0xFFFF
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


@dataclass
class Header:
    version: int
    device_id: int
    session: str
    start_unix: int
    boot_count: int


@dataclass
class Record:
    seq: int
    t_us: int
    organ: int
    flags: int
    values: tuple

    @property
    def read_error(self) -> bool:
        return bool(self.flags & FLAG_READ_ERROR)


@dataclass
class Stats:
    records: int = 0
    corrupt_bytes: int = 0     # bytes skipped while resynchronising
    resyncs: int = 0           # number of separate corrupt spans
    seq_gaps: int = 0          # number of gap events in the global seq
    missing_records: int = 0   # records missing according to those gaps
    truncated_tail: int = 0    # bytes of an incomplete record at EOF
    per_organ: dict = field(default_factory=dict)


def _parse_header(data: bytes) -> Header:
    if len(data) < HEADER_SIZE:
        raise LogFormatError("file shorter than header")
    if data[:4] != MAGIC:
        raise LogFormatError("bad magic")
    version, hsize = struct.unpack_from("<HH", data, 4)
    if hsize != HEADER_SIZE:
        raise LogFormatError(f"unexpected header size {hsize}")
    (crc,) = struct.unpack_from("<I", data, 28)
    if zlib.crc32(data[:28]) & 0xFFFFFFFF != crc:
        raise LogFormatError("header CRC mismatch")
    device_id, = struct.unpack_from("<I", data, 8)
    session = data[12:16].split(b"\0", 1)[0].decode("ascii", "replace")
    start_unix, boot_count = struct.unpack_from("<II", data, 16)
    return Header(version, device_id, session, start_unix, boot_count)


def _try_record(data: bytes, pos: int):
    """Return (Record, length) if a CRC-valid record starts at pos, 'short' if it would run past
    EOF, or None if invalid."""
    if len(data) - pos < REC_FIXED + 2:
        return "short"
    seq, t_us, organ, fn = struct.unpack_from("<IQBB", data, pos)
    n = fn & 0xF
    flags = fn >> 4
    length = REC_FIXED + 2 * n
    if pos + length + 2 > len(data):
        return "short"
    (crc,) = struct.unpack_from("<H", data, pos + length)
    if crc16_ccitt_false(data[pos:pos + length]) != crc:
        return None
    values = struct.unpack_from(f"<{n}h", data, pos + REC_FIXED) if n else ()
    return Record(seq, t_us, organ, flags, values), length + 2


def read_log(path):
    data = Path(path).read_bytes()
    header = _parse_header(data)
    st = Stats()
    recs = []
    pos = HEADER_SIZE
    in_resync = False
    last_seq = None
    while pos < len(data):
        res = _try_record(data, pos)
        if isinstance(res, tuple):
            rec, ln = res
            recs.append(rec)
            st.records += 1
            st.per_organ[rec.organ] = st.per_organ.get(rec.organ, 0) + 1
            if last_seq is not None:
                d = (rec.seq - last_seq) & 0xFFFFFFFF
                if d != 1:
                    st.seq_gaps += 1
                    st.missing_records += (d - 1) & 0xFFFFFFFF
            last_seq = rec.seq
            pos += ln
            in_resync = False
        elif res == "short" and not in_resync:
            st.truncated_tail = len(data) - pos   # power cut during the last write
            break
        else:
            if not in_resync:
                st.resyncs += 1
                in_resync = True
            st.corrupt_bytes += 1
            pos += 1
    return header, recs, st


def export_csv(path, outdir):
    _, recs, _ = read_log(path)
    outdir = Path(outdir)
    outdir.mkdir(parents=True, exist_ok=True)
    by_organ = {}
    for r in recs:
        by_organ.setdefault(r.organ, []).append(r)
    files = []
    for organ, rows in sorted(by_organ.items()):
        name = ORGAN_NAMES.get(organ, f"organ{organ}")
        width = max(len(r.values) for r in rows)
        f = outdir / f"{name}.csv"
        with open(f, "w", newline="") as fh:
            w = csv.writer(fh, lineterminator="\n")
            w.writerow(["seq", "t_us", "read_error"] + [f"v{i}" for i in range(width)])
            for r in rows:
                vals = list(r.values) + [""] * (width - len(r.values))
                w.writerow([r.seq, r.t_us, int(r.read_error)] + vals)
        files.append(f)
    return files


def summary(path) -> str:
    h, recs, st = read_log(path)
    lines = [
        f"device_id=0x{h.device_id:08X} session={h.session} start_unix={h.start_unix} boot_count={h.boot_count}",
        f"records={st.records} corrupt_bytes={st.corrupt_bytes} resyncs={st.resyncs} "
        f"seq_gaps={st.seq_gaps} missing_records={st.missing_records} truncated_tail={st.truncated_tail}",
    ]
    for organ, n in sorted(st.per_organ.items()):
        errs = sum(1 for r in recs if r.organ == organ and r.read_error)
        lines.append(f"  organ {organ:2d} ({ORGAN_NAMES.get(organ, '?'):10s}) records={n} read_errors={errs}")
    return "\n".join(lines)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("logfile")
    ap.add_argument("--csv-dir")
    a = ap.parse_args()
    try:
        print(summary(a.logfile))
        if a.csv_dir:
            files = export_csv(a.logfile, a.csv_dir)
            print(f"CSV: {', '.join(f.name for f in files)} -> {a.csv_dir}")
    except LogFormatError as e:
        sys.exit(f"log format error: {e}")


if __name__ == "__main__":
    main()
