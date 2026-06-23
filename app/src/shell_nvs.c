#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>

#include "setup/setup_hw.h"

static int cmd_nvs_show(const struct shell *sh, size_t argc, char **argv)
{
	uint16_t led_default = 0;
	uint32_t press_count = 0;
	int rc;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	rc = setup_config_read_saved(&led_default, &press_count);
	switch (rc) {
	case 0:
		shell_print(sh, "Saved config (NVS id 1):");
		shell_print(sh, "  led_default   = %u", led_default);
		shell_print(sh, "  press_count   = %u", press_count);
		break;
	case -ENOENT:
		shell_print(sh, "No config saved yet — running on defaults.");
		break;
	default:
		shell_error(sh, "NVS not available (%d).", rc);
		break;
	}
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(nvs_sub,
	SHELL_CMD(show, NULL, "Show the config values saved in NVS", cmd_nvs_show),
	SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(nvs, &nvs_sub, "Application NVS config", NULL);
