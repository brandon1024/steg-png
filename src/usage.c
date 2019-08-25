#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include "usage.h"
#include "utils.h"

#define USAGE_OPTIONS_WIDTH		24
#define USAGE_OPTIONS_GAP		2

void show_usage(const struct usage_description *cmd_usage, int err,
		const char *optional_message_format, ...)
{
	va_list varargs;
	va_start(varargs, optional_message_format);

	variadic_show_usage(cmd_usage, optional_message_format, varargs, err);

	va_end(varargs);
}

void variadic_show_usage(const struct usage_description *cmd_usage,
		const char *optional_message_format, va_list varargs, int err)
{
	FILE *fp = err ? stderr : stdout;

	if (optional_message_format != NULL) {
		vfprintf(fp, optional_message_format, varargs);
		fprintf(fp, "\n");
	}

	size_t index = 0;
	while (cmd_usage[index].usage_desc != NULL) {
		if (index == 0)
			fprintf(fp, "%s %s\n", "usage:", cmd_usage[index].usage_desc);
		else
			fprintf(fp, "%s %s\n", "   or:", cmd_usage[index].usage_desc);

		index++;
	}

	fprintf(fp, "\n");
}

void show_options(const struct option_description *opts, int err)
{
	FILE *fp = err ? stderr : stdout;

	size_t index = 0;
	while (opts[index].type != OPTION_END) {
		struct option_description opt = opts[index++];
		int printed_chars = 0;

		if (opt.type == OPTION_GROUP_T) {
			if (index > 1)
				fprintf(fp, "\n");

			fprintf(fp, "%s:\n", opt.desc);
			continue;
		}

		printed_chars += fprintf(fp, "    ");
		if (opt.type == OPTION_BOOL_T) {
			if (opt.s_flag)
				printed_chars += fprintf(fp, "-%c", opt.s_flag);

			if (opt.s_flag && opt.l_flag)
				printed_chars += fprintf(fp, ", --%s", opt.l_flag);
			else if (opt.l_flag)
				printed_chars += fprintf(fp, "--%s", opt.l_flag);
		} else if (opt.type == OPTION_INT_T) {
			if (opt.s_flag)
				printed_chars += fprintf(fp, "-%c=<n>", opt.s_flag);

			if (opt.s_flag && opt.l_flag)
				printed_chars += fprintf(fp, ", --%s=<n>", opt.l_flag);
			else if (opt.l_flag)
				printed_chars += fprintf(fp, "--%s=<n>", opt.l_flag);
		} else if (opt.type == OPTION_STRING_T) {
			if (opt.s_flag)
				printed_chars += fprintf(fp, "-%c", opt.s_flag);

			if (opt.s_flag && opt.l_flag)
				printed_chars += fprintf(fp, ", --%s", opt.l_flag);
			else if (opt.l_flag)
				printed_chars += fprintf(fp, "--%s", opt.l_flag);

			printed_chars += fprintf(fp, " <%s>", opt.str_name);
		} else if (opt.type == OPTION_COMMAND_T) {
			printed_chars += fprintf(fp, "%s", opt.str_name);
		}

		if (printed_chars >= (USAGE_OPTIONS_WIDTH - USAGE_OPTIONS_GAP))
			fprintf(fp, "\n%*s%s\n", USAGE_OPTIONS_WIDTH, "", opt.desc);
		else
			fprintf(fp, "%*s%s\n", USAGE_OPTIONS_WIDTH - printed_chars, "",
					opt.desc);
	}

	fprintf(fp, "\n");
}

void show_usage_with_options(const struct usage_description *cmd_usage,
		const struct option_description *opts, int err,
		const char *optional_message_format, ...)
{
	va_list varargs;
	va_start(varargs, optional_message_format);

	variadic_show_usage_with_options(cmd_usage, opts, optional_message_format,
			varargs, err);

	va_end(varargs);
}

void variadic_show_usage_with_options(const struct usage_description *cmd_usage,
		const struct option_description *opts,
		const char *optional_message_format, va_list varargs, int err)
{
	variadic_show_usage(cmd_usage, optional_message_format, varargs, err);
	show_options(opts, err);
}

int argument_matches_option(const char *arg, struct option_description description)
{
	if (description.type == OPTION_GROUP_T)
		BUG("cannot match argument against type 'OPTION_GROUP_T'");

	if (description.type == OPTION_END)
		BUG("cannot match argument against type 'OPTION_END'");

	//If option is of type command
	if (description.type == OPTION_COMMAND_T)
		return !strcmp(arg, description.str_name);

	/*
	 * If argument is less than two characters in length, or is not prefixed by
	 * a dash, return false as it is not a valid command line flag
	 */
	if (strlen(arg) <= 1 || arg[0] != '-')
		return false;

	const char *arg_name = arg + 1;
	bool is_long_format = false;
	if (arg[1] == '-') {
		arg_name++;
		is_long_format = true;
	}

	//If argument is in long format
	size_t arg_name_len = strlen(arg_name);
	if (is_long_format) {
		if (description.l_flag == NULL)
			return false;

		return !strcmp(arg_name, description.l_flag);
	}

	//If argument is in short combined boolean format
	if (arg_name_len > 1) {
		if (description.type == OPTION_BOOL_T) {
			return strchr(arg_name, description.s_flag) != NULL;
		}

		return arg_name[arg_name_len - 1] == description.s_flag;
	}

	if (arg_name_len == 1)
		return *arg_name == description.s_flag;

	return false;
}

int is_valid_argument(const char *arg,
		const struct option_description arg_usage_descriptions[])
{
	size_t arg_char_len = strlen(arg);

	if (arg_char_len == 0)
		return false;

	/* argument is a string */
	if (arg[0] != '-')
		return true;

	/*
	 * Perform argument validation for short boolean combined flags to ensure
	 * they do not contain unknown flags.
	 */
	if (arg_char_len > 2 && arg[1] != '-') {
		const char *flag = arg + 1;
		while (*flag) {
			bool flag_found = false;
			const struct option_description *desc = arg_usage_descriptions;

			while (desc->type != OPTION_END) {
				if (desc->type == OPTION_COMMAND_T || desc->type == OPTION_GROUP_T) {
					desc++;
					continue;
				}

				//last flag in arg may be of any type
				if ((flag - arg + 1) == arg_char_len && *flag == desc->s_flag) {
					flag_found = true;
					break;
				}

				if (desc->type == OPTION_BOOL_T && *flag == desc->s_flag) {
					flag_found = true;
					break;
				}

				desc++;
			}

			if (!flag_found)
				return false;

			flag++;
		}

		return true;
	}

	/* perform argument validation for short flags */
	if (arg_char_len == 2 && arg[1] != '-') {
		char flag = arg[1];
		const struct option_description *desc = arg_usage_descriptions;

		while (desc->type != OPTION_END) {
			if (desc->s_flag == flag)
				return true;

			desc++;
		}

		return false;
	}

	/* perform argument validation for long flags */
	if (arg_char_len > 2 && arg[1] == '-') {
		const char *flag = arg + 2;
		const struct option_description *desc = arg_usage_descriptions;

		while (desc->type != OPTION_END) {
			if (desc->l_flag != NULL && !strcmp(desc->l_flag, flag))
				return true;

			desc++;
		}

		return false;
	}

	return false;
}

