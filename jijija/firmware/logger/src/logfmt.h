/* logfmt.h — فرمت لاگ داده خام (بازسازی‌شده از logfmt.c و gen_testlog.c؛ نسخه قبلی این هدر
 * به‌اشتباه بازنویسی و از دست رفت). مقادیر زیر که در کد قبلی دیده نمی‌شدند حدسی‌اند و علامت دارند.
 * مشخصات: docs/data-protocol.md */
#ifndef LOGFMT_H
#define LOGFMT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LOGFMT_VERSION       1   /* حدسی */
#define LOGFMT_HEADER_SIZE   32
#define LOGFMT_REC_FIXED     14  /* seq(4) + t_us(8) + organ(1) + flags/n(1) */
#define LOGFMT_MAX_VALUES    8   /* حدسی؛ touch به ۶ نیاز دارد، n چهار بیتی است (حداکثر ۱۵) */
#define LOGFMT_REC_MAX       (LOGFMT_REC_FIXED + 2 * LOGFMT_MAX_VALUES + 2)

/* شناسه اندام در رکورد. ۰، ۱، ۲ و ۱۵ از gen_testlog.c/test_log_parser.py قطعی‌اند؛ بقیه حدسی/جدیدند. */
#define LOGFMT_ORGAN_LIGHT       0
#define LOGFMT_ORGAN_THERMO      1
#define LOGFMT_ORGAN_TOUCH       2
#define LOGFMT_ORGAN_HUNGER      3   /* حدسی */
#define LOGFMT_ORGAN_LED         4   /* حدسی */
#define LOGFMT_ORGAN_BUZZER      5   /* حدسی */
#define LOGFMT_ORGAN_HEARING     6   /* حدسی/جدید: ۸ باند انرژی (ترکیب گوش‌ها) */
#define LOGFMT_ORGAN_EARS        7   /* جدید: سطح هر ۳ گوش */
/* فقط لاگر (سنسورهای مرجع و خطا؛ جزو اندام‌های موجود نیستند): */
#define LOGFMT_ORGAN_HEAR_L      20  /* جدید: ۸ باند انرژی گوش چپ (KY-037) */
#define LOGFMT_ORGAN_HEAR_C      21  /* گوش وسط */
#define LOGFMT_ORGAN_HEAR_R      22  /* گوش راست */
#define LOGFMT_ORGAN_MIC_REF     23  /* ۸ باند میکروفون مرجع (INMP441)، فقط اگر برد I2S‌دار باشد */
#define LOGFMT_ORGAN_LIGHT_REF   8   /* BH1750 raw */
#define LOGFMT_ORGAN_THERMO_REF  9   /* DS18B20 raw، واحد 1/16 درجه */
#define LOGFMT_ORGAN_MCU_TEMP    10  /* centi-degC */
#define LOGFMT_ORGAN_ERROR       14  /* v0=کد، v1=آرگومان */
#define LOGFMT_ORGAN_MARKER      15  /* v0=رویداد، v1=آرگومان (قطعی از تست) */

#define LOGFMT_FLAG_READ_ERROR   0x1 /* حدسی؛ بیت پایین flags */

struct logfmt_header {
	uint32_t device_id;
	char session[4];
	uint32_t start_unix;
	uint32_t boot_count;
};

struct logfmt_record {
	uint32_t seq;
	uint64_t t_us;
	uint8_t organ;
	uint8_t flags;   /* ۴ بیت */
	uint8_t n;       /* تعداد مقادیر، ۰..LOGFMT_MAX_VALUES */
	int16_t v[LOGFMT_MAX_VALUES];
};

uint16_t logfmt_crc16(const uint8_t *data, size_t len);  /* CRC-16/CCITT-FALSE */
uint32_t logfmt_crc32(const uint8_t *data, size_t len);  /* CRC-32 استاندارد */
void logfmt_header_encode(const struct logfmt_header *h, uint8_t *buf);  /* LOGFMT_HEADER_SIZE بایت */
/* طول رکورد را برمی‌گرداند، یا ۰ در خطا */
size_t logfmt_record_encode(const struct logfmt_record *r, uint8_t *buf, size_t buf_size);

#ifdef __cplusplus
}
#endif

#endif /* LOGFMT_H */
