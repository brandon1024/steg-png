#ifndef STEG_PNG_PARSE_OPTIONS_H
#define STEG_PNG_PARSE_OPTIONS_H

#include <stdarg.h>

enum opt_type {
	OPTION_BOOL_T,
	OPTION_INT_T,
	OPTION_STRING_T,
	OPTION_STRING_LIST_T,
	OPTION_COMMAND_T,
	OPTION_GROUP_T,
	OPTION_END
};

struct command_option;
struct command_option {
	const char s_flag;
	const char *l_flag;
	const char *str_name;
	const char *desc;
	enum opt_type type;
	void *arg_value;
};

struct usage_string;
struct usage_string {
	const char *usage_desc;
};

#define OPT_SHORT_BOOL(S,D,V)			{ (S), NULL,  NULL, (D), OPTION_BOOL_T, (V) }
#define OPT_SHORT_INT(S,D,V)			{ (S), NULL,  NULL, (D), OPTION_INT_T, (V) }
#define OPT_SHORT_STRING(S,N,D,V)		{ (S), NULL,  (N), (D), OPTION_STRING_T, (V) }
#define OPT_SHORT_STRING_LIST(S,N,D,V)		{ (S), NULL,  (N), (D), OPTION_STRING_LIST_T, (V) }
#define OPT_LONG_BOOL(L,D,V)			{ 0, (L),  NULL, (D), OPTION_BOOL_T, (V) }
#define OPT_LONG_INT(L,D,V)				{ 0, (L),  NULL, (D), OPTION_INT_T, (V) }
#define OPT_LONG_STRING(L,N,D,V)		{ 0, (L),  (N), (D), OPTION_STRING_T, (V) }
#define OPT_LONG_STRING_LIST(L,N,D,V)		{ 0, (L),  (N), (D), OPTION_STRING_LIST_T, (V) }
#define OPT_BOOL(S,L,D,V)				{ (S), (L), NULL, (D), OPTION_BOOL_T, (V) }
#define OPT_INT(S,L,D,V)				{ (S), (L), NULL, (D), OPTION_INT_T, (V) }
#define OPT_STRING(S,L,N,D,V)			{ (S), (L), (N), (D), OPTION_STRING_T, (V) }
#define OPT_STRING_LIST(S,L,N,D,V)			{ (S), (L), (N), (D), OPTION_STRING_LIST_T, (V) }
#define OPT_CMD(N,D,V)					{ 0, NULL, (N), (D), OPTION_COMMAND_T, (V) }
#define OPT_GROUP(N)					{ 0, NULL, NULL, N, OPTION_GROUP_T, NULL }
#define OPT_END()						{ 0, NULL, NULL, NULL, OPTION_END, NULL }

#define USAGE(DESC)					{ (DESC) }
#define USAGE_END()					{ NULL }


/**
 * Parse command line arguments against an array of command_option's which describe
 * acceptable valid arguments.
 *
 * If an argument in the argument vector matches a command_option, the argument
 * (along with any applicable arg values) are shifted from the argument vector.
 *
 * If `--` is encountered, processing stops and all remaining args are left in the
 * argument vector.
 *
 * If skip_first is non-zero, the first argument in argv is skipped. This is useful
 * when processing arguments with the first argument being the program name.
 *
 * If stop_on_unknown is non-zero, all arguments are parsed even if the argument
 * is not recognized.
 *
 * Returns the number of arguments left in the argv array.
 * */
int parse_options(int argc, char *argv[], const struct command_option options[],
		int skip_first, int stop_on_unknown);

/**
 * Print usage of a command by supplying a list of usage_strings. Provide
 * an optional format string to display in the case of an error, along with the
 * associated arguments.
 *
 * If err is non-zero, outputs to stderr. Otherwise, outputs to stdout.
 *
 * See stdio printf() for format string specification
 * */
void show_usage(const struct usage_string cmd_usage[], int err,
		const char *optional_message_format, ...);

/**
 * Variadic form of show_usage, allowing the use of va_list rather than
 * arbitrary arguments.
 * */
void variadic_show_usage(const struct usage_string cmd_usage[],
		const char *optional_message_format, va_list varargs, int err);

/**
 * Print usage of a command to stdout by supplying a list of usage_descriptions.
 *
 * If err is non-zero, outputs to stderr. Otherwise, outputs to stdout.
 * */
void show_options(const struct command_option opts[], int err);

/**
 * Combination of show_usage() and show_options().
 * */
void show_usage_with_options(const struct usage_string cmd_usage[],
		const struct command_option opts[], int err,
		const char *optional_message_format, ...);

/**
 * Variadic form of show_usage_with_options, allowing the use of va_list rather
 * than arbitrary arguments.
 * */
void variadic_show_usage_with_options(const struct usage_string cmd_usage[],
		const struct command_option opts[],
		const char *optional_message_format, va_list varargs, int err);

#endif //STEG_PNG_PARSE_OPTIONS_H
