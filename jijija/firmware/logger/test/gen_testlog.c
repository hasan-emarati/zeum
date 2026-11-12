/* gen_testlog.c — با کدگذار C یک لاگ آزمایشی می‌نویسد تا پارسر پایتون آن را بخواند.
 * استفاده: gen_testlog <خروجی.bin> [corrupt]
 *   corrupt: وسط فایل چند بایت خراب و یک رکورد حذف می‌کند (تست resync و نمونه‌های گم‌شده) */
#include "logfmt.h"

#include <stdio.h>
#include <string.h>

static uint32_t g_seq;

static void emit(FILE *f, uint64_t t_us, uint8_t organ, uint8_t flags, uint8_t n,
		 const int16_t *v, int skip_write)
{
	struct logfmt_record r;
	uint8_t buf[LOGFMT_REC_MAX];
	size_t len;

	memset(&r, 0, sizeof(r));
	r.seq = g_seq++;
	r.t_us = t_us;
	r.organ = organ;
	r.flags = flags;
	r.n = n;
	for (int i = 0; i < n; i++) {
		r.v[i] = v[i];
	}
	len = logfmt_record_encode(&r, buf, sizeof(buf));
	if (len == 0) {
		fprintf(stderr, "encode failed\n");
		return;
	}
	if (!skip_write) {
		fwrite(buf, 1, len, f);
	}
}

int main(int argc, char **argv)
{
	struct logfmt_header h;
	uint8_t hb[LOGFMT_HEADER_SIZE];
	int corrupt = (argc >= 3 && strcmp(argv[2], "corrupt") == 0);
	FILE *f;

	if (argc < 2) {
		return 2;
	}
	f = fopen(argv[1], "wb");
	if (!f) {
		return 2;
	}
	memset(&h, 0, sizeof(h));
	h.device_id = 0xC0FFEE01u;
	memcpy(h.session, "S1\0", 4);
	h.start_unix = 1789000000u;
	h.boot_count = 7;
	logfmt_header_encode(&h, hb);
	fwrite(hb, 1, sizeof(hb), f);

	/* ۱۰۰ چرخه: touch هر چرخه (۶ کانال)، light هر چرخه، thermo هر ۱۰ چرخه (یکی با خطا) */
	for (int i = 0; i < 100; i++) {
		uint64_t t = 1000000ull * 5000 + (uint64_t)i * 5000 + 5000000000ull; /* بیش از ۳۲ بیت */
		int16_t tv[6] = {(int16_t)(100 + i), (int16_t)(-200 - i), 16384, (int16_t)(-32768 + i), 32767, 0};
		int16_t lv[1] = {(int16_t)(2000 + (i % 7))};

		/* رکورد حذف‌شده در حالت corrupt: touch در i==50 (seq مصرف می‌شود ولی نوشته نمی‌شود) */
		emit(f, t, LOGFMT_ORGAN_TOUCH, 0, 6, tv, corrupt && i == 50);
		emit(f, t + 100, LOGFMT_ORGAN_LIGHT, 0, 1, lv, 0);
		if (i % 10 == 0) {
			int16_t th[1] = {(int16_t)(250 + i / 10)};

			if (i == 30) {
				emit(f, t + 200, LOGFMT_ORGAN_THERMO, LOGFMT_FLAG_READ_ERROR, 1, th, 0);
			} else {
				emit(f, t + 200, LOGFMT_ORGAN_THERMO, 0, 1, th, 0);
			}
		}
		if (corrupt && i == 70) {
			/* ۵ بایت زباله وسط جریان */
			static const uint8_t junk[5] = {0xDE, 0xAD, 0xBE, 0xEF, 0x55};

			fwrite(junk, 1, sizeof(junk), f);
		}
	}
	{
		int16_t m[2] = {1, 4}; /* marker: event_start، نوع ۴ */

		emit(f, 9000000000ull, LOGFMT_ORGAN_MARKER, 0, 2, m, 0);
	}
	fclose(f);
	return 0;
}
