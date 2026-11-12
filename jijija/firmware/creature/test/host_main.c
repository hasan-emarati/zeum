#define _POSIX_C_SOURCE 200809L
/* host_main.c — اجرای «organ list» روی PC با همان هسته‌ای که روی Zephyr می‌رود.
 * استفاده: creature_host <پوشه شناسنامه‌ها> */
#include "organ.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int cmp_str(const void *a, const void *b)
{
	return strcmp(*(const char *const *)a, *(const char *const *)b);
}

int main(int argc, char **argv)
{
	static char buf[8192];
	organ_registry_t reg;
	char *names[64];
	int n = 0;
	int rc = 0;
	DIR *d;
	struct dirent *de;

	if (argc != 2) {
		fprintf(stderr, "usage: %s <organs-dir>\n", argv[0]);
		return 2;
	}
	d = opendir(argv[1]);
	if (!d) {
		perror("opendir");
		return 2;
	}
	while ((de = readdir(d)) != NULL && n < 64) {
		size_t l = strlen(de->d_name);

		if (l > 5 && strcmp(de->d_name + l - 5, ".toml") == 0) {
			names[n++] = strdup(de->d_name);
		}
	}
	closedir(d);
	qsort(names, (size_t)n, sizeof(names[0]), cmp_str);

	organ_registry_init(&reg);
	for (int i = 0; i < n; i++) {
		char path[512];
		organ_t o;
		int line = 0;
		FILE *f;
		size_t len;
		organ_err_t err;

		snprintf(path, sizeof(path), "%s/%s", argv[1], names[i]);
		f = fopen(path, "rb");
		if (!f) {
			perror(path);
			rc = 1;
			continue;
		}
		len = fread(buf, 1, sizeof(buf), f);
		fclose(f);
		err = organ_parse(buf, len, &o, &line);
		if (err == ORGAN_OK) {
			err = organ_registry_add(&reg, &o);
		}
		if (err != ORGAN_OK) {
			fprintf(stderr, "%s:%d: %s\n", path, line, organ_err_str(err));
			rc = 1;
		}
		free(names[i]);
	}

	printf("organs: %d\n", reg.count);
	for (int i = 0; i < reg.count; i++) {
		char line[160];

		organ_format_line(&reg.organs[i], line, sizeof(line));
		printf("  %s\n", line);
	}
	return rc;
}
