#include <string.h>

#include "parse-options.h"
#include "builtin.h"

int cmd_embed(int argc, char *argv[])
{
	char **orig_argv = argv;

	const char *strategy = NULL;
	int help = 0;

	const struct usage_string embed_cmd_usage[] = {
			USAGE("steg-png embed [(-s | --strategy) <embed strategy>] [options]"),
			USAGE("steg-png embed (-h | --help)"),
			USAGE_END()
	};

	const struct command_option embed_cmd_options[] = {
			OPT_STRING('s', "strategy", "embed strategy", "specify which steganography strategy to use (default: zlib)", &strategy),
			OPT_BOOL('h', "help", "show help and exit", &help),
			OPT_END()
	};

	parse_options(argc, argv, embed_cmd_options, 0, 0);
	if (help) {
		show_usage_with_options(embed_cmd_usage, embed_cmd_options, 0, NULL);
		return 0;
	}

	if (strategy) {
		if (!strcmp(strategy, "zlib"))
			return cmd_embed_zlib_strategy(argc, orig_argv);
		if (!strcmp(strategy, "lsb"))
			return cmd_embed_zlib_strategy(argc, orig_argv);

		show_usage_with_options(embed_cmd_usage, embed_cmd_options, 1, "Unknown embed strategy '%s'", strategy);
		return 1;
	}

	return cmd_embed_zlib_strategy(argc, orig_argv);
}
