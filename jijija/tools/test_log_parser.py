"""تست رفت‌وبرگشت: لاگ با کدگذار C (firmware/logger/src/logfmt.c) ساخته می‌شود و پارسر پایتون می‌خواند."""
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(Path(__file__).resolve().parent))
import log_parser as lp  # noqa: E402


class RoundTrip(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        d = Path(cls.tmp.name)
        cls.gen = d / "gen_testlog"
        logger = ROOT / "firmware" / "logger"
        subprocess.run(
            ["gcc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-I", str(logger / "src"),
             str(logger / "test" / "gen_testlog.c"), str(logger / "src" / "logfmt.c"),
             "-o", str(cls.gen)], check=True)
        cls.clean = d / "clean.bin"
        cls.dirty = d / "dirty.bin"
        subprocess.run([str(cls.gen), str(cls.clean)], check=True)
        subprocess.run([str(cls.gen), str(cls.dirty), "corrupt"], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def test_crc_vectors(self):
        # بردارهای استاندارد: CRC-16/CCITT-FALSE("123456789") = 0x29B1
        self.assertEqual(lp.crc16_ccitt_false(b"123456789"), 0x29B1)

    def test_header(self):
        h, _, _ = lp.read_log(self.clean)
        self.assertEqual(h.device_id, 0xC0FFEE01)
        self.assertEqual(h.session, "S1")
        self.assertEqual(h.start_unix, 1789000000)
        self.assertEqual(h.boot_count, 7)

    def test_clean_counts_and_values(self):
        _, recs, st = lp.read_log(self.clean)
        # 100 touch + 100 light + 10 thermo + 1 marker
        self.assertEqual(st.records, 211)
        self.assertEqual(st.corrupt_bytes, 0)
        self.assertEqual(st.seq_gaps, 0)
        self.assertEqual(st.per_organ, {2: 100, 0: 100, 1: 10, 15: 1})
        touch = [r for r in recs if r.organ == 2]
        self.assertEqual(touch[0].values, (100, -200, 16384, -32768, 32767, 0))
        self.assertEqual(touch[3].values[3], -32768 + 3)
        # زمان بالای ۳۲ بیت حفظ می‌شود
        self.assertGreater(touch[0].t_us, 2**32)
        err = [r for r in recs if r.organ == 1 and r.read_error]
        self.assertEqual(len(err), 1)
        marker = [r for r in recs if r.organ == 15][0]
        self.assertEqual(marker.values, (1, 4))
        # seq یکنواخت
        self.assertEqual([r.seq for r in recs], list(range(211)))

    def test_dirty_resync_and_gaps(self):
        _, recs, st = lp.read_log(self.dirty)
        self.assertEqual(st.corrupt_bytes, 5)          # فقط ۵ بایت زباله
        self.assertEqual(st.resyncs, 1)
        self.assertEqual(st.seq_gaps, 1)               # یک شکاف
        self.assertEqual(st.missing_records, 1)        # یک رکورد گم‌شده
        self.assertEqual(st.records, 210)
        # بعد از خرابی رکوردها سالم می‌مانند
        self.assertEqual(recs[-1].organ, 15)

    def test_bad_header(self):
        p = Path(self.tmp.name) / "bad.bin"
        data = bytearray(self.clean.read_bytes())
        data[9] ^= 0xFF  # device_id خراب → CRC هدر
        p.write_bytes(bytes(data))
        with self.assertRaises(lp.LogFormatError):
            lp.read_log(p)
        p.write_bytes(b"XXXX" + bytes(data[4:]))
        with self.assertRaises(lp.LogFormatError):
            lp.read_log(p)
        p.write_bytes(b"short")
        with self.assertRaises(lp.LogFormatError):
            lp.read_log(p)

    def test_truncated_tail(self):
        p = Path(self.tmp.name) / "trunc.bin"
        data = self.clean.read_bytes()
        p.write_bytes(data[:-5])  # رکورد آخر نیمه‌نوشته (مثل قطع برق)
        _, _, st = lp.read_log(p)
        self.assertEqual(st.records, 210)
        self.assertGreater(st.truncated_tail, 0)

    def test_csv_and_summary(self):
        out = Path(self.tmp.name) / "csv"
        files = lp.export_csv(self.clean, out)
        names = sorted(f.name for f in files)
        self.assertEqual(names, ["light.csv", "marker.csv", "thermo.csv", "touch.csv"])
        lines = (out / "touch.csv").read_text().splitlines()
        self.assertEqual(lines[0], "seq,t_us,read_error,v0,v1,v2,v3,v4,v5")
        self.assertEqual(len(lines), 101)
        text = lp.summary(self.clean)
        self.assertIn("records=211", text)


if __name__ == "__main__":
    unittest.main()
