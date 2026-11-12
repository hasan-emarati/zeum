#!/usr/bin/env python3
"""embed_organs.py — شناسنامه‌های اندام (*.toml) را به یک فایل C تبدیل می‌کند.

برای native_sim که فایل‌سیستم ندارد: شناسنامه‌ها داخل فریمور جا می‌گیرند.
روی سخت‌افزار واقعی همین‌ها از SD (/CREATURE/organs) خوانده می‌شوند.

استفاده: embed_organs.py <پوشه toml> <خروجی .c>
"""
import sys
from pathlib import Path


def main() -> int:
    if len(sys.argv) != 3:
        print(__doc__, file=sys.stderr)
        return 2
    src, out = Path(sys.argv[1]), Path(sys.argv[2])
    files = sorted(src.glob("*.toml"))
    if not files:
        print(f"no .toml files in {src}", file=sys.stderr)
        return 1

    lines = ["/* تولیدشده توسط tools/embed_organs.py — ویرایش نکن */",
             "#include <stddef.h>", ""]
    for i, f in enumerate(files):
        data = f.read_bytes()
        body = ", ".join(f"0x{b:02x}" for b in data)
        lines.append(f"static const unsigned char organ_blob_{i}[] = {{{body}}};")
    lines.append("")
    lines.append("const unsigned char *const organ_embedded_data[] = {")
    lines += [f"\torgan_blob_{i}," for i in range(len(files))]
    lines.append("};")
    lines.append("const size_t organ_embedded_len[] = {")
    lines += [f"\tsizeof(organ_blob_{i})," for i in range(len(files))]
    lines.append("};")
    lines.append(f"const int organ_embedded_count = {len(files)};")
    out.write_text("\n".join(lines) + "\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
