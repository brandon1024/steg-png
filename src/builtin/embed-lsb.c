
#include "parse-options.h"

int cmd_embed_lsb_strategy(int argc, char *argv[])
{
	char *message = NULL, *file_to_embed = NULL, *output_file = NULL;
	int help = 0;
	int quiet = 0;

	const struct usage_string embed_cmd_usage[] = {
			USAGE("steg-png embed [options] (-m | --message <message>) [(-q | --quiet)] <file>"),
			USAGE("steg-png embed [options] (-f | --file <file>) [(-q | --quiet)] <file>"),
			USAGE("steg-png embed (-h | --help)"),
			USAGE_END()
	};

	const struct command_option embed_cmd_options[] = {
			OPT_STRING('m', "message", "message", "specify the message to embed in the png image", &message),
			OPT_STRING('f', "file", "file", "specify a file to embed in the png image", &file_to_embed),
			OPT_STRING('o', "output", "file", "output to a specific file", &output_file),
			OPT_BOOL('q', "quiet", "suppress informational summary to stdout", &quiet),
			OPT_BOOL('h', "help", "show help and exit", &help),
			OPT_END()
	};

	argc = parse_options(argc, argv, embed_cmd_options, 1, 1);
	if (help) {
		show_usage_with_options(embed_cmd_usage, embed_cmd_options, 0, NULL);
		return 0;
	}

	if (argc > 1) {
		show_usage_with_options(embed_cmd_usage, embed_cmd_options, 1, "unknown option '%s'", argv[0]);
		return 1;
	}

	if (argc < 1) {
		show_usage_with_options(embed_cmd_usage, embed_cmd_options, 1, "nothing to do");
		return 1;
	}

	if (file_to_embed && message) {
		show_usage_with_options(embed_cmd_usage, embed_cmd_options, 1, "--file and --message are mutually exclusive options");
		return 1;
	}

	return 0;
}