#include <string.h>

#include "parse-options.h"
#include "builtin.h"

static struct steg_png_builtin builtins[] = {
		{ "embed", &cmd_embed },
		{ "extract", &cmd_extract },
		{ "inspect", &cmd_inspect },
		{ NULL, NULL }
};

static struct steg_png_builtin *find_builtin(int argc, char *argv[]);

int main(int argc, char *argv[])
{
	int help = 0;

	const struct usage_string main_cmd_usage[] = {
			USAGE("steg-png <subcommand> [options...]"),
			USAGE("steg-png (-h | --help)"),
			USAGE_END()
	};

	const struct command_option main_cmd_options[] = {
			OPT_GROUP("subcommands"),
			OPT_CMD("embed", "embed a message in a PNG image", NULL),
			OPT_CMD("extract", "extract a message in a PNG image", NULL),
			OPT_CMD("inspect", "inspect the contents of a PNG image", NULL),
			OPT_GROUP("options"),
			OPT_BOOL('h', "help", "show help and exit", &help),
			OPT_END()
	};

	argc = parse_options(argc, argv, main_cmd_options, 1, 1);
	if (help) {
		show_usage_with_options(main_cmd_usage, main_cmd_options,0, NULL);
		return 0;
	}

	if (argc) {
		struct steg_png_builtin *builtin = find_builtin(argc, argv);
		if (builtin)
			return builtin->fn(argc - 1, argv + 1);

		show_usage_with_options(main_cmd_usage, main_cmd_options,1, "unknown option '%s'", argv[0]);
		return 1;
	}

	show_usage_with_options(main_cmd_usage, main_cmd_options,0, NULL);
	return 0;
}

/**
 * Attempt to find, from the first entry in the list of arguments, a registered
 * subcommand.
 * */
static struct steg_png_builtin *find_builtin(int argc, char *argv[])
{
	if (argc <= 0)
		return NULL;

	struct steg_png_builtin *builtin = builtins;
	while (builtin->cmd != NULL) {
		if (!strcmp(argv[0], builtin->cmd))
			return builtin;

		builtin++;
	}

	return NULL;
}
