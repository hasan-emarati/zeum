/* test_organ.c — تست واحد پارسر اندام روی PC (gcc) */
#include "organ.h"

#include <stdio.h>
#include <string.h>

static int g_fail = 0;
static int g_pass = 0;

#define CHECK(cond)                                                            \
	do {                                                                   \
		if (cond) {                                                    \
			g_pass++;                                              \
		} else {                                                       \
			g_fail++;                                              \
			printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);  \
		}                                                              \
	} while (0)

static organ_err_t parse(const char *s, organ_t *o, int *line)
{
	return organ_parse(s, strlen(s), o, line);
}

static const char *GOOD_SENSOR =
	"[organ]\nid = \"light\"\nkind = \"sensor\"\nunit = \"adc_count\"\n"
	"range = [0, 4095]\nrate_hz = 100\n\n[reflex]\nnone = true\n\n"
	"[vocabulary]\nwords = [\"read-light\", \"light-avg\"]\n";

static const char *GOOD_ACTUATOR =
	"[organ]\nid = \"led\"\nkind = \"actuator\"\nrange = [0, 255]\n"
	"[reflex]\nnone = false\nmax_duty = 200\n"
	"[vocabulary]\nwords = [\"led!\"]\n";

static void test_good(void)
{
	organ_t o;
	int line = -1;

	CHECK(parse(GOOD_SENSOR, &o, &line) == ORGAN_OK);
	CHECK(strcmp(o.id, "light") == 0);
	CHECK(o.kind == ORGAN_SENSOR);
	CHECK(o.range_min == 0 && o.range_max == 4095);
	CHECK(o.rate_hz == 100);
	CHECK(o.channels == 1 && o.version == 1); /* پیش‌فرض‌ها */
	CHECK(o.reflex_none);
	CHECK(o.word_count == 2);
	CHECK(strcmp(o.words[1], "light-avg") == 0);

	CHECK(parse(GOOD_ACTUATOR, &o, &line) == ORGAN_OK);
	CHECK(o.kind == ORGAN_ACTUATOR);
	CHECK(!o.reflex_none && o.max_duty == 200);
	CHECK(o.max_rate_hz == ORGAN_UNSET && o.min_off_ms == ORGAN_UNSET);
}

static void test_format_tolerance(void)
{
	organ_t o;
	/* CRLF، توضیح، فاصله‌های اضافه، مقدار منفی */
	const char *s =
		"# comment\r\n[organ]   # trailing\r\n  id=\"x1\"\r\nkind = \"internal\" # c\r\n"
		"range = [ -400 , 800 ]\r\n[reflex]\r\nnone=true\r\n[vocabulary]\r\nwords = []\r\n";

	CHECK(parse(s, &o, NULL) == ORGAN_OK);
	CHECK(strcmp(o.id, "x1") == 0);
	CHECK(o.kind == ORGAN_INTERNAL);
	CHECK(o.range_min == -400 && o.range_max == 800);
	CHECK(o.word_count == 0);

	/* بدون newline پایانی */
	CHECK(parse("[organ]\nid=\"a\"\nkind=\"sensor\"\nrange=[0,1]\n[reflex]\nnone=true\n[vocabulary]\nwords=[\"w\"]", &o, NULL) == ORGAN_OK);
}

static void test_errors(void)
{
	organ_t o;
	int line;

	/* بخش‌های الزامی */
	CHECK(parse("[organ]\nid=\"a\"\nkind=\"sensor\"\nrange=[0,1]\n[vocabulary]\nwords=[]\n", &o, &line) == ORGAN_ERR_MISSING);
	CHECK(parse("[organ]\nid=\"a\"\nkind=\"sensor\"\nrange=[0,1]\n[reflex]\nnone=true\n", &o, &line) == ORGAN_ERR_MISSING);
	CHECK(parse("[organ]\nkind=\"sensor\"\nrange=[0,1]\n[reflex]\nnone=true\n[vocabulary]\nwords=[]\n", &o, &line) == ORGAN_ERR_MISSING);
	CHECK(parse("", &o, &line) == ORGAN_ERR_MISSING);

	/* range برعکس */
	CHECK(parse("[organ]\nid=\"a\"\nkind=\"sensor\"\nrange=[5,1]\n[reflex]\nnone=true\n[vocabulary]\nwords=[]\n", &o, &line) == ORGAN_ERR_RANGE);

	/* ابزار بدون رفلکس */
	CHECK(parse("[organ]\nid=\"a\"\nkind=\"actuator\"\nrange=[0,1]\n[reflex]\nnone=true\n[vocabulary]\nwords=[]\n", &o, &line) == ORGAN_ERR_REFLEX);
	/* ابزار با none=false ولی بدون هیچ سقفی */
	CHECK(parse("[organ]\nid=\"a\"\nkind=\"actuator\"\nrange=[0,1]\n[reflex]\nnone=false\n[vocabulary]\nwords=[]\n", &o, &line) == ORGAN_ERR_REFLEX);
	/* none=true همراه سقف */
	CHECK(parse("[organ]\nid=\"a\"\nkind=\"sensor\"\nrange=[0,1]\n[reflex]\nnone=true\nmax_duty=5\n[vocabulary]\nwords=[]\n", &o, &line) == ORGAN_ERR_REFLEX);

	/* کلید ناشناخته در organ با شماره خط */
	CHECK(parse("[organ]\nid=\"a\"\nfoo=1\n", &o, &line) == ORGAN_ERR_UNKNOWN_KEY);
	CHECK(line == 3);

	/* بخش ناشناخته */
	CHECK(parse("[organs]\n", &o, &line) == ORGAN_ERR_UNKNOWN_SECTION);
	CHECK(line == 1);

	/* مقادیر نامعتبر */
	CHECK(parse("[organ]\nid=\"A\"\nkind=\"sensor\"\nrange=[0,1]\n[reflex]\nnone=true\n[vocabulary]\nwords=[]\n", &o, &line) == ORGAN_ERR_VALUE);
	CHECK(parse("[organ]\nid=\"a-b\"\nkind=\"sensor\"\nrange=[0,1]\n[reflex]\nnone=true\n[vocabulary]\nwords=[]\n", &o, &line) == ORGAN_ERR_VALUE);
	CHECK(parse("[organ]\nid=\"abcdefghijklmnop\"\nkind=\"sensor\"\nrange=[0,1]\n[reflex]\nnone=true\n[vocabulary]\nwords=[]\n", &o, &line) == ORGAN_ERR_VALUE); /* 16 حرف */
	CHECK(parse("[organ]\nid=\"a\"\nkind=\"mineral\"\n", &o, &line) == ORGAN_ERR_VALUE);
	CHECK(parse("[organ]\nid=\"a\"\nkind=\"sensor\"\nrate_hz=-5\n", &o, &line) == ORGAN_ERR_VALUE);
	CHECK(parse("[organ]\nid=\"a\"\nkind=\"sensor\"\nchannels=0\n", &o, &line) == ORGAN_ERR_VALUE);

	/* نحو */
	CHECK(parse("id = \"a\"\n", &o, &line) == ORGAN_ERR_SYNTAX);            /* قبل از بخش */
	CHECK(parse("[organ]\nid \"a\"\n", &o, &line) == ORGAN_ERR_SYNTAX);      /* بدون = */
	CHECK(parse("[organ]\nid = \"a\n", &o, &line) == ORGAN_ERR_SYNTAX);      /* رشته باز */
	CHECK(parse("[organ]\nrate_hz = 1.5\n", &o, &line) == ORGAN_ERR_SYNTAX);/* اعشار پشتیبانی نمی‌شود */
	CHECK(parse("[organ]\nrate_hz = 12abc\n", &o, &line) == ORGAN_ERR_SYNTAX);
	CHECK(parse("[organ\nid=\"a\"\n", &o, &line) == ORGAN_ERR_SYNTAX);       /* سرتیتر ناقص */
	CHECK(parse("[organ]\nrange = [1]\n", &o, &line) == ORGAN_ERR_SYNTAX);
	CHECK(parse("[organ]\nrate_hz = 99999999999\n", &o, &line) == ORGAN_ERR_SYNTAX); /* سرریز */

	/* طول رشته و تعداد */
	CHECK(parse("[organ]\nid=\"a\"\nunit=\"0123456789012345678901234567890123\"\n", &o, &line) == ORGAN_ERR_TOO_LONG);
	CHECK(parse("[vocabulary]\nwords=[\"1\",\"2\",\"3\",\"4\",\"5\",\"6\",\"7\",\"8\",\"9\"]\n", &o, &line) == ORGAN_ERR_TOO_MANY);
}

static void test_hardware_truncate(void)
{
	organ_t o;
	char big[200];
	char doc[512];

	memset(big, 'x', sizeof(big) - 1);
	big[sizeof(big) - 1] = '\0';
	snprintf(doc, sizeof(doc),
		 "[organ]\nid=\"a\"\nkind=\"sensor\"\nrange=[0,1]\n[hardware]\npart=\"%s\"\nfoo=\"bar\"\nbaz=7\n"
		 "[reflex]\nnone=true\n[vocabulary]\nwords=[]\n", big);
	CHECK(parse(doc, &o, NULL) == ORGAN_OK);
	CHECK(strlen(o.hw_part) == ORGAN_HW_STR - 1);

	/* برش وسط UTF-8 نباشد: «ا» دو بایت است */
	{
		char fa[256] = "";
		char d2[512];
		size_t n = 0;

		while (n < 70) {
			strcat(fa, "\xD8\xA7"); /* U+0627 */
			n += 2;
		}
		snprintf(d2, sizeof(d2),
			 "[organ]\nid=\"a\"\nkind=\"sensor\"\nrange=[0,1]\n[hardware]\npart=\"%s\"\n"
			 "[reflex]\nnone=true\n[vocabulary]\nwords=[]\n", fa);
		CHECK(parse(d2, &o, NULL) == ORGAN_OK);
		CHECK(strlen(o.hw_part) % 2 == 0); /* فقط نویسه‌های کامل */
	}
}

static void test_registry(void)
{
	organ_registry_t reg;
	organ_t o;

	organ_registry_init(&reg);
	CHECK(parse(GOOD_SENSOR, &o, NULL) == ORGAN_OK);
	CHECK(organ_registry_add(&reg, &o) == ORGAN_OK);
	CHECK(organ_registry_add(&reg, &o) == ORGAN_ERR_DUPLICATE);
	CHECK(organ_registry_find(&reg, "light") != NULL);
	CHECK(organ_registry_find(&reg, "nope") == NULL);

	for (int i = 1; i < ORGAN_MAX; i++) {
		snprintf(o.id, sizeof(o.id), "o%d", i);
		CHECK(organ_registry_add(&reg, &o) == ORGAN_OK);
	}
	snprintf(o.id, sizeof(o.id), "extra");
	CHECK(organ_registry_add(&reg, &o) == ORGAN_ERR_TOO_MANY);
}

static int read_file(const char *path, char *buf, size_t cap, size_t *n)
{
	FILE *f = fopen(path, "rb");

	if (!f) {
		return -1;
	}
	*n = fread(buf, 1, cap, f);
	fclose(f);
	return 0;
}

/* شناسنامه‌های واقعی پروژه باید همه معتبر باشند */
static void test_real_files(void)
{
	static const char *names[] = {"light", "thermo", "touch", "hunger", "led", "buzzer"};
	static char buf[8192];
	organ_registry_t reg;

	organ_registry_init(&reg);
	for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
		char path[256];
		size_t n = 0;
		organ_t o;
		int line = 0;
		organ_err_t err;

		snprintf(path, sizeof(path), "%s/%s.toml", GENOME_DIR, names[i]);
		if (read_file(path, buf, sizeof(buf), &n) != 0) {
			g_fail++;
			printf("FAIL cannot read %s\n", path);
			continue;
		}
		err = organ_parse(buf, n, &o, &line);
		if (err != ORGAN_OK) {
			printf("FAIL %s: %s (line %d)\n", path, organ_err_str(err), line);
		}
		CHECK(err == ORGAN_OK);
		CHECK(strcmp(o.id, names[i]) == 0);
		CHECK(organ_registry_add(&reg, &o) == ORGAN_OK);
	}
	CHECK(reg.count == 6);
	CHECK(organ_registry_find(&reg, "touch")->channels == 6);
	CHECK(organ_registry_find(&reg, "led")->kind == ORGAN_ACTUATOR);
	CHECK(organ_registry_find(&reg, "buzzer")->min_off_ms == 20);
}

int main(void)
{
	test_good();
	test_format_tolerance();
	test_errors();
	test_hardware_truncate();
	test_registry();
	test_real_files();
	printf("%d passed, %d failed\n", g_pass, g_fail);
	return g_fail ? 1 : 0;
}
