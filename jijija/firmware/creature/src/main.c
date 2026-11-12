/* main.c — اسکلت Runtime موجود روی Zephyr (فاز 0.5).
 * بوت: شناسنامه‌های اندام (داخل فریمور) را پارس می‌کند و دستور Shell «organ list» را می‌دهد.
 * وضعیت: تست‌نشده با west؛ منطق پارس در firmware/creature/test با gcc تست شده است. */
#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>

#include "organ.h"

extern const unsigned char *const organ_embedded_data[];
extern const size_t organ_embedded_len[];
extern const int organ_embedded_count;

static organ_registry_t registry;

static int cmd_organ_list(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(sh, "organs: %d", registry.count);
	for (int i = 0; i < registry.count; i++) {
		char line[160];

		organ_format_line(&registry.organs[i], line, sizeof(line));
		shell_print(sh, "  %s", line);
	}
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(organ_cmds,
	SHELL_CMD(list, NULL, "List loaded organs", cmd_organ_list),
	SHELL_SUBCMD_SET_END);
SHELL_CMD_REGISTER(organ, &organ_cmds, "Organ commands", NULL);

int main(void)
{
	organ_registry_init(&registry);

	for (int i = 0; i < organ_embedded_count; i++) {
		organ_t o;
		int line = 0;
		organ_err_t err = organ_parse((const char *)organ_embedded_data[i],
					      organ_embedded_len[i], &o, &line);

		if (err == ORGAN_OK) {
			err = organ_registry_add(&registry, &o);
		}
		if (err != ORGAN_OK) {
			printk("organ #%d rejected: %s (line %d)\n", i, organ_err_str(err), line);
		}
	}
	printk("creature: %d organs loaded\n", registry.count);
	return 0;
}
