/* logfmt.c — کدگذاری لاگ داده خام */
#include "logfmt.h"

#include <string.h>

static void put_u16(uint8_t *p, uint16_t v)
{
	p[0] = (uint8_t)v;
	p[1] = (uint8_t)(v >> 8);
}

static void put_u32(uint8_t *p, uint32_t v)
{
	for (int i = 0; i < 4; i++) {
		p[i] = (uint8_t)(v >> (8 * i));
	}
}

static void put_u64(uint8_t *p, uint64_t v)
{
	for (int i = 0; i < 8; i++) {
		p[i] = (uint8_t)(v >> (8 * i));
	}
}

uint16_t logfmt_crc16(const uint8_t *data, size_t len)
{
	uint16_t crc = 0xFFFF;

	for (size_t i = 0; i < len; i++) {
		crc ^= (uint16_t)data[i] << 8;
		for (int b = 0; b < 8; b++) {
			crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
		}
	}
	return crc;
}

uint32_t logfmt_crc32(const uint8_t *data, size_t len)
{
	uint32_t crc = 0xFFFFFFFFu;

	for (size_t i = 0; i < len; i++) {
		crc ^= data[i];
		for (int b = 0; b < 8; b++) {
			crc = (crc & 1) ? (crc >> 1) ^ 0xEDB88320u : (crc >> 1);
		}
	}
	return ~crc;
}

void logfmt_header_encode(const struct logfmt_header *h, uint8_t *buf)
{
	memset(buf, 0, LOGFMT_HEADER_SIZE);
	memcpy(buf, "CRLG", 4);
	put_u16(buf + 4, LOGFMT_VERSION);
	put_u16(buf + 6, LOGFMT_HEADER_SIZE);
	put_u32(buf + 8, h->device_id);
	memcpy(buf + 12, h->session, 4);
	put_u32(buf + 16, h->start_unix);
	put_u32(buf + 20, h->boot_count);
	/* 24..27 رزرو (صفر) */
	put_u32(buf + 28, logfmt_crc32(buf, 28));
}

size_t logfmt_record_encode(const struct logfmt_record *r, uint8_t *buf, size_t buf_size)
{
	size_t len = LOGFMT_REC_FIXED + 2u * r->n;
	uint8_t *p = buf;

	if (r->n > LOGFMT_MAX_VALUES || r->flags > 0xF || buf_size < len + 2) {
		return 0;
	}
	put_u32(p, r->seq);
	put_u64(p + 4, r->t_us);
	p[12] = r->organ;
	p[13] = (uint8_t)((r->flags << 4) | r->n);
	for (unsigned i = 0; i < r->n; i++) {
		put_u16(p + LOGFMT_REC_FIXED + 2 * i, (uint16_t)r->v[i]);
	}
	put_u16(p + len, logfmt_crc16(p, len));
	return len + 2;
}
