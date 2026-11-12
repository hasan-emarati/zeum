/* organ.c — پارسر شناسنامه اندام (Organ Spec v1) */
#include "organ.h"

#include <stdio.h>
#include <string.h>

typedef enum {
	SEC_NONE = 0,
	SEC_ORGAN,
	SEC_HW,
	SEC_REFLEX,
	SEC_VOCAB,
} section_t;

static const char *skip_ws(const char *p, const char *e)
{
	while (p < e && (*p == ' ' || *p == '\t')) {
		p++;
	}
	return p;
}

/* بعد از مقدار فقط فاصله و توضیح (#) مجاز است */
static bool tail_ok(const char *p, const char *e)
{
	p = skip_ws(p, e);
	return p == e || *p == '#';
}

static bool parse_int(const char **pp, const char *e, int32_t *out)
{
	const char *p = *pp;
	bool neg = false;
	int64_t v = 0;
	int digits = 0;

	if (p < e && (*p == '-' || *p == '+')) {
		neg = (*p == '-');
		p++;
	}
	while (p < e && *p >= '0' && *p <= '9') {
		v = v * 10 + (*p - '0');
		if (v > 2147483648LL) {
			return false;
		}
		p++;
		digits++;
	}
	if (digits == 0) {
		return false;
	}
	if (neg) {
		v = -v;
	}
	if (v > INT32_MAX || v < INT32_MIN) {
		return false;
	}
	*out = (int32_t)v;
	*pp = p;
	return true;
}

/* رشته «...» روی همان خط. اگر truncate نباشد و طول بیشتر از dst_size-1 شود خطا. */
static organ_err_t parse_string(const char **pp, const char *e, char *dst,
				size_t dst_size, bool truncate)
{
	const char *p = *pp;
	const char *start;
	size_t len;

	if (p >= e || *p != '"') {
		return ORGAN_ERR_SYNTAX;
	}
	start = ++p;
	while (p < e && *p != '"') {
		p++;
	}
	if (p >= e) {
		return ORGAN_ERR_SYNTAX; /* بسته نشده */
	}
	len = (size_t)(p - start);
	if (len >= dst_size) {
		if (!truncate) {
			return ORGAN_ERR_TOO_LONG;
		}
		len = dst_size - 1;
		/* برش وسط یک نویسه UTF-8 نباشد */
		while (len > 0 && ((unsigned char)start[len] & 0xC0) == 0x80) {
			len--;
		}
	}
	memcpy(dst, start, len);
	dst[len] = '\0';
	*pp = p + 1;
	return ORGAN_OK;
}

static organ_err_t parse_bool(const char **pp, const char *e, bool *out)
{
	const char *p = *pp;
	size_t n = (size_t)(e - p);

	if (n >= 4 && strncmp(p, "true", 4) == 0) {
		*out = true;
		*pp = p + 4;
		return ORGAN_OK;
	}
	if (n >= 5 && strncmp(p, "false", 5) == 0) {
		*out = false;
		*pp = p + 5;
		return ORGAN_OK;
	}
	return ORGAN_ERR_SYNTAX;
}

static organ_err_t parse_range(const char **pp, const char *e, int32_t *lo, int32_t *hi)
{
	const char *p = *pp;
	int32_t v[2];

	if (p >= e || *p != '[') {
		return ORGAN_ERR_SYNTAX;
	}
	p = skip_ws(p + 1, e);
	if (!parse_int(&p, e, &v[0])) {
		return ORGAN_ERR_SYNTAX;
	}
	p = skip_ws(p, e);
	if (p >= e || *p != ',') {
		return ORGAN_ERR_SYNTAX;
	}
	p = skip_ws(p + 1, e);
	if (!parse_int(&p, e, &v[1])) {
		return ORGAN_ERR_SYNTAX;
	}
	p = skip_ws(p, e);
	if (p >= e || *p != ']') {
		return ORGAN_ERR_SYNTAX;
	}
	*lo = v[0];
	*hi = v[1];
	*pp = p + 1;
	return ORGAN_OK;
}

static organ_err_t parse_words(const char **pp, const char *e, organ_t *o)
{
	const char *p = *pp;

	if (p >= e || *p != '[') {
		return ORGAN_ERR_SYNTAX;
	}
	p = skip_ws(p + 1, e);
	o->word_count = 0;
	if (p < e && *p == ']') {
		*pp = p + 1;
		return ORGAN_OK;
	}
	for (;;) {
		organ_err_t err;

		if (o->word_count >= ORGAN_WORDS_MAX) {
			return ORGAN_ERR_TOO_MANY;
		}
		p = skip_ws(p, e);
		err = parse_string(&p, e, o->words[o->word_count], ORGAN_STR, false);
		if (err != ORGAN_OK) {
			return err;
		}
		o->word_count++;
		p = skip_ws(p, e);
		if (p < e && *p == ',') {
			p++;
			continue;
		}
		if (p < e && *p == ']') {
			*pp = p + 1;
			return ORGAN_OK;
		}
		return ORGAN_ERR_SYNTAX;
	}
}

static bool key_is(const char *k, size_t klen, const char *name)
{
	return strlen(name) == klen && memcmp(k, name, klen) == 0;
}

static organ_err_t set_int(const char **pp, const char *e, int32_t *dst)
{
	if (!parse_int(pp, e, dst)) {
		return ORGAN_ERR_SYNTAX;
	}
	return ORGAN_OK;
}

static bool id_valid(const char *id)
{
	size_t n = strlen(id);

	if (n == 0 || n > ORGAN_ID_MAX) {
		return false;
	}
	for (size_t i = 0; i < n; i++) {
		char c = id[i];

		if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) {
			return false;
		}
	}
	return true;
}

/* یک جفت key=value را در بخش جاری اعمال می‌کند */
static organ_err_t apply_key(section_t sec, const char *k, size_t klen,
			     const char **pp, const char *e, organ_t *o,
			     bool *seen_kind, bool *seen_range, bool *seen_id,
			     bool *seen_words)
{
	organ_err_t err;
	int32_t tmp;

	switch (sec) {
	case SEC_ORGAN:
		if (key_is(k, klen, "id")) {
			*seen_id = true;
			return parse_string(pp, e, o->id, ORGAN_STR, false);
		}
		if (key_is(k, klen, "kind")) {
			char buf[ORGAN_STR];

			err = parse_string(pp, e, buf, sizeof(buf), false);
			if (err != ORGAN_OK) {
				return err;
			}
			if (strcmp(buf, "sensor") == 0) {
				o->kind = ORGAN_SENSOR;
			} else if (strcmp(buf, "actuator") == 0) {
				o->kind = ORGAN_ACTUATOR;
			} else if (strcmp(buf, "internal") == 0) {
				o->kind = ORGAN_INTERNAL;
			} else {
				return ORGAN_ERR_VALUE;
			}
			*seen_kind = true;
			return ORGAN_OK;
		}
		if (key_is(k, klen, "unit")) {
			return parse_string(pp, e, o->unit, ORGAN_STR, false);
		}
		if (key_is(k, klen, "range")) {
			*seen_range = true;
			return parse_range(pp, e, &o->range_min, &o->range_max);
		}
		if (key_is(k, klen, "rate_hz")) {
			err = set_int(pp, e, &tmp);
			if (err == ORGAN_OK) {
				if (tmp < 0) {
					return ORGAN_ERR_VALUE;
				}
				o->rate_hz = tmp;
			}
			return err;
		}
		if (key_is(k, klen, "channels")) {
			err = set_int(pp, e, &tmp);
			if (err == ORGAN_OK) {
				if (tmp < 1) {
					return ORGAN_ERR_VALUE;
				}
				o->channels = tmp;
			}
			return err;
		}
		if (key_is(k, klen, "version")) {
			err = set_int(pp, e, &tmp);
			if (err == ORGAN_OK) {
				if (tmp < 1) {
					return ORGAN_ERR_VALUE;
				}
				o->version = tmp;
			}
			return err;
		}
		return ORGAN_ERR_UNKNOWN_KEY;

	case SEC_HW:
		/* اطلاعات ردیابی؛ کلید ناشناخته نادیده گرفته می‌شود */
		if (key_is(k, klen, "part")) {
			return parse_string(pp, e, o->hw_part, ORGAN_HW_STR, true);
		}
		if (key_is(k, klen, "bus")) {
			return parse_string(pp, e, o->hw_bus, ORGAN_STR, true);
		}
		if (key_is(k, klen, "note")) {
			return parse_string(pp, e, o->hw_note, ORGAN_HW_STR, true);
		}
		{
			char skip[ORGAN_HW_STR];

			if (**pp == '"') {
				return parse_string(pp, e, skip, sizeof(skip), true);
			}
			/* مقدار غیررشته‌ای: تا انتهای توکن رد می‌شود */
			while (*pp < e && **pp != ' ' && **pp != '\t' && **pp != '#') {
				(*pp)++;
			}
			return ORGAN_OK;
		}

	case SEC_REFLEX:
		if (key_is(k, klen, "none")) {
			return parse_bool(pp, e, &o->reflex_none);
		}
		if (key_is(k, klen, "max_rate_hz")) {
			err = set_int(pp, e, &tmp);
			if (err == ORGAN_OK) {
				if (tmp < 0) {
					return ORGAN_ERR_VALUE;
				}
				o->max_rate_hz = tmp;
			}
			return err;
		}
		if (key_is(k, klen, "max_duty")) {
			err = set_int(pp, e, &tmp);
			if (err == ORGAN_OK) {
				if (tmp < 0) {
					return ORGAN_ERR_VALUE;
				}
				o->max_duty = tmp;
			}
			return err;
		}
		if (key_is(k, klen, "min_off_ms")) {
			err = set_int(pp, e, &tmp);
			if (err == ORGAN_OK) {
				if (tmp < 0) {
					return ORGAN_ERR_VALUE;
				}
				o->min_off_ms = tmp;
			}
			return err;
		}
		return ORGAN_ERR_UNKNOWN_KEY;

	case SEC_VOCAB:
		if (key_is(k, klen, "words")) {
			*seen_words = true;
			return parse_words(pp, e, o);
		}
		return ORGAN_ERR_UNKNOWN_KEY;

	case SEC_NONE:
	default:
		return ORGAN_ERR_SYNTAX; /* کلید قبل از هر بخش */
	}
}

organ_err_t organ_parse(const char *text, size_t len, organ_t *out, int *err_line)
{
	const char *p = text;
	const char *end = text + len;
	section_t sec = SEC_NONE;
	int line_no = 0;
	bool seen_id = false, seen_kind = false, seen_range = false;
	bool seen_words = false, seen_reflex = false, seen_vocab = false;
	bool seen_organ = false;
	organ_err_t err = ORGAN_OK;

	if (err_line) {
		*err_line = 0;
	}
	memset(out, 0, sizeof(*out));
	out->channels = 1;
	out->version = 1;
	out->max_rate_hz = ORGAN_UNSET;
	out->max_duty = ORGAN_UNSET;
	out->min_off_ms = ORGAN_UNSET;

	while (p < end) {
		const char *line = p;
		const char *line_end = p;
		const char *q;

		while (line_end < end && *line_end != '\n') {
			line_end++;
		}
		p = (line_end < end) ? line_end + 1 : line_end;
		if (line_end > line && line_end[-1] == '\r') {
			line_end--;
		}
		line_no++;

		q = skip_ws(line, line_end);
		if (q == line_end || *q == '#') {
			continue;
		}

		if (*q == '[') {
			const char *name = q + 1;
			const char *close = name;

			while (close < line_end && *close != ']') {
				close++;
			}
			if (close >= line_end || !tail_ok(close + 1, line_end)) {
				err = ORGAN_ERR_SYNTAX;
				goto fail;
			}
			if (strncmp(name, "organ", (size_t)(close - name)) == 0 &&
			    close - name == 5) {
				sec = SEC_ORGAN;
				seen_organ = true;
			} else if (close - name == 8 && strncmp(name, "hardware", 8) == 0) {
				sec = SEC_HW;
			} else if (close - name == 6 && strncmp(name, "reflex", 6) == 0) {
				sec = SEC_REFLEX;
				seen_reflex = true;
			} else if (close - name == 10 && strncmp(name, "vocabulary", 10) == 0) {
				sec = SEC_VOCAB;
				seen_vocab = true;
			} else {
				err = ORGAN_ERR_UNKNOWN_SECTION;
				goto fail;
			}
			continue;
		}

		/* key = value */
		{
			const char *k = q;
			const char *kend = q;
			const char *v;

			while (kend < line_end && *kend != '=' && *kend != ' ' && *kend != '\t') {
				kend++;
			}
			v = skip_ws(kend, line_end);
			if (kend == k || v >= line_end || *v != '=') {
				err = ORGAN_ERR_SYNTAX;
				goto fail;
			}
			v = skip_ws(v + 1, line_end);
			if (v >= line_end) {
				err = ORGAN_ERR_SYNTAX;
				goto fail;
			}
			err = apply_key(sec, k, (size_t)(kend - k), &v, line_end, out,
					&seen_kind, &seen_range, &seen_id, &seen_words);
			if (err != ORGAN_OK) {
				goto fail;
			}
			if (!tail_ok(v, line_end)) {
				err = ORGAN_ERR_SYNTAX;
				goto fail;
			}
		}
	}

	/* اعتبارسنجی کلی (خط ۰) */
	if (!seen_organ || !seen_id || !seen_kind || !seen_range ||
	    !seen_reflex || !seen_vocab || !seen_words) {
		err = ORGAN_ERR_MISSING;
		goto fail_global;
	}
	if (!id_valid(out->id)) {
		err = ORGAN_ERR_VALUE;
		goto fail_global;
	}
	if (out->range_min > out->range_max) {
		err = ORGAN_ERR_RANGE;
		goto fail_global;
	}
	if (out->reflex_none &&
	    (out->max_rate_hz != ORGAN_UNSET || out->max_duty != ORGAN_UNSET ||
	     out->min_off_ms != ORGAN_UNSET)) {
		err = ORGAN_ERR_REFLEX;
		goto fail_global;
	}
	if (out->kind == ORGAN_ACTUATOR) {
		if (out->reflex_none ||
		    (out->max_rate_hz == ORGAN_UNSET && out->max_duty == ORGAN_UNSET &&
		     out->min_off_ms == ORGAN_UNSET)) {
			err = ORGAN_ERR_REFLEX;
			goto fail_global;
		}
	}
	return ORGAN_OK;

fail:
	if (err_line) {
		*err_line = line_no;
	}
	return err;
fail_global:
	if (err_line) {
		*err_line = 0;
	}
	return err;
}

const char *organ_err_str(organ_err_t err)
{
	switch (err) {
	case ORGAN_OK:                 return "ok";
	case ORGAN_ERR_SYNTAX:         return "syntax error";
	case ORGAN_ERR_UNKNOWN_SECTION: return "unknown section";
	case ORGAN_ERR_UNKNOWN_KEY:    return "unknown key";
	case ORGAN_ERR_MISSING:        return "missing required key or section";
	case ORGAN_ERR_VALUE:          return "invalid value";
	case ORGAN_ERR_RANGE:          return "range min > max";
	case ORGAN_ERR_REFLEX:         return "inconsistent [reflex]";
	case ORGAN_ERR_TOO_LONG:       return "string too long";
	case ORGAN_ERR_TOO_MANY:       return "too many items";
	case ORGAN_ERR_DUPLICATE:      return "duplicate organ id";
	default:                       return "unknown error";
	}
}

const char *organ_kind_str(organ_kind_t kind)
{
	switch (kind) {
	case ORGAN_SENSOR:   return "sensor";
	case ORGAN_ACTUATOR: return "actuator";
	case ORGAN_INTERNAL: return "internal";
	default:             return "?";
	}
}

void organ_registry_init(organ_registry_t *reg)
{
	memset(reg, 0, sizeof(*reg));
}

organ_err_t organ_registry_add(organ_registry_t *reg, const organ_t *organ)
{
	if (organ_registry_find(reg, organ->id) != NULL) {
		return ORGAN_ERR_DUPLICATE;
	}
	if (reg->count >= ORGAN_MAX) {
		return ORGAN_ERR_TOO_MANY;
	}
	reg->organs[reg->count++] = *organ;
	return ORGAN_OK;
}

const organ_t *organ_registry_find(const organ_registry_t *reg, const char *id)
{
	for (int i = 0; i < reg->count; i++) {
		if (strcmp(reg->organs[i].id, id) == 0) {
			return &reg->organs[i];
		}
	}
	return NULL;
}

int organ_format_line(const organ_t *o, char *buf, size_t buf_size)
{
	return snprintf(buf, buf_size, "%-8s %-8s %-10s range=[%ld,%ld] rate=%ldHz ch=%ld words=%u%s",
			o->id, organ_kind_str(o->kind), o->unit,
			(long)o->range_min, (long)o->range_max, (long)o->rate_hz,
			(long)o->channels, (unsigned)o->word_count,
			o->reflex_none ? "" : " reflex");
}
