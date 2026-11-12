/* organ.h — پارسر شناسنامه اندام (Organ Spec v1, docs/organ-spec.md)
 *
 * هسته قابل‌حمل: بدون malloc، بدون وابستگی به Zephyr یا libc فایل.
 * روی PC (تست gcc) و روی MCU یک کد است.
 */
#ifndef ORGAN_H
#define ORGAN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ORGAN_MAX        8   /* حداکثر اندام همزمان */
#define ORGAN_WORDS_MAX  8   /* حداکثر کلمه برای هر اندام */
#define ORGAN_ID_MAX     15  /* حداکثر طول id */
#define ORGAN_STR        32  /* بافر رشته‌های کوتاه (۳۱ بایت + پایان) */
#define ORGAN_HW_STR     64  /* بافر رشته‌های بخش hardware (برش‌خورده) */
#define ORGAN_UNSET      (-1)

typedef enum {
	ORGAN_SENSOR = 0,
	ORGAN_ACTUATOR,
	ORGAN_INTERNAL,
} organ_kind_t;

typedef enum {
	ORGAN_OK = 0,
	ORGAN_ERR_SYNTAX,          /* ساختار خط نامعتبر */
	ORGAN_ERR_UNKNOWN_SECTION, /* بخش ناشناخته */
	ORGAN_ERR_UNKNOWN_KEY,     /* کلید ناشناخته (organ/reflex/vocabulary) */
	ORGAN_ERR_MISSING,         /* کلید یا بخش الزامی نیست */
	ORGAN_ERR_VALUE,           /* مقدار نامعتبر (id، kind، منفی، ...) */
	ORGAN_ERR_RANGE,           /* range[0] > range[1] */
	ORGAN_ERR_REFLEX,          /* تناقض در [reflex] */
	ORGAN_ERR_TOO_LONG,        /* رشته بلندتر از حد */
	ORGAN_ERR_TOO_MANY,        /* تعداد کلمه یا اندام بیش از حد */
	ORGAN_ERR_DUPLICATE,       /* id تکراری در رجیستری */
} organ_err_t;

typedef struct {
	char id[ORGAN_STR];
	organ_kind_t kind;
	char unit[ORGAN_STR];
	int32_t range_min;
	int32_t range_max;
	int32_t rate_hz;
	int32_t channels;
	int32_t version;

	char hw_part[ORGAN_HW_STR];
	char hw_bus[ORGAN_STR];
	char hw_note[ORGAN_HW_STR];

	bool reflex_none;
	int32_t max_rate_hz; /* ORGAN_UNSET اگر تعریف نشده */
	int32_t max_duty;
	int32_t min_off_ms;

	uint8_t word_count;
	char words[ORGAN_WORDS_MAX][ORGAN_STR];
} organ_t;

typedef struct {
	organ_t organs[ORGAN_MAX];
	int count;
} organ_registry_t;

/* متن TOML را (با طول مشخص، بدون نیاز به NUL) پارس و اعتبارسنجی می‌کند.
 * در خطا، *err_line شماره خط (از ۱) یا ۰ برای خطای اعتبارسنجی کلی است.
 * err_line می‌تواند NULL باشد. */
organ_err_t organ_parse(const char *text, size_t len, organ_t *out, int *err_line);

const char *organ_err_str(organ_err_t err);
const char *organ_kind_str(organ_kind_t kind);

void organ_registry_init(organ_registry_t *reg);
organ_err_t organ_registry_add(organ_registry_t *reg, const organ_t *organ);
const organ_t *organ_registry_find(const organ_registry_t *reg, const char *id);

/* یک خط خلاصه برای دستور Shell «organ list». تعداد بایت نوشته‌شده را برمی‌گرداند. */
int organ_format_line(const organ_t *organ, char *buf, size_t buf_size);

#endif /* ORGAN_H */
