#ifndef GITCHAT_USAGE_H
#define GITCHAT_USAGE_H

#include <stdarg.h>

enum opt_type {
	OPTION_BOOL_T,
	OPTION_INT_T,
	OPTION_STRING_T,
	OPTION_COMMAND_T,
	OPTION_GROUP_T,
	OPTION_END
};

struct option_description;
struct option_description {
	const char s_flag;
	const char *l_flag;
	const char *str_name;
	const char *desc;
	enum opt_type type;
};

struct usage_description;
struct usage_description {
	const char *usage_desc;
};

#define OPT_SHORT_BOOL(S,D)			{ (S), NULL,  NULL, (D), OPTION_BOOL_T }
#define OPT_SHORT_INT(S,D)			{ (S), NULL,  NULL, (D), OPTION_INT_T }
#define OPT_SHORT_STRING(S,N,D)		{ (S), NULL,  (N), (D), OPTION_STRING_T }
#define OPT_LONG_BOOL(L,D)			{ 0, (L),  NULL, (D), OPTION_BOOL_T }
#define OPT_LONG_INT(L,D)			{ 0, (L),  NULL, (D), OPTION_INT_T }
#define OPT_LONG_STRING(L,N,D)		{ 0, (L),  (N), (D), OPTION_STRING_T }
#define OPT_BOOL(S,L,D)				{ (S), (L), NULL, (D), OPTION_BOOL_T }
#define OPT_INT(S,L,D)				{ (S), (L), NULL, (D), OPTION_INT_T }
#define OPT_STRING(S,L,N,D)			{ (S), (L), (N), (D), OPTION_STRING_T }
#define OPT_CMD(N,D)				{ 0, NULL, (N), (D), OPTION_COMMAND_T }
#define OPT_GROUP(N)				{ 0, NULL, NULL, N, OPTION_GROUP_T }
#define OPT_END()					{ 0, NULL, NULL, NULL, OPTION_END }

#define USAGE(DESC)					{ (DESC) }
#define USAGE_END()					{ NULL }

/**
 * Print usage of a command by supplying a list of usage_descriptions. Provide
 * an optional format string to display in the case of an error, along with the
 * associated arguments.
 *
 * If err is non-zero, outputs to stderr. Otherwise, outputs to stdout.
 *
 * See stdio printf() for format string specification
 * */
void show_usage(const struct usage_description *cmd_usage, int err,
		const char *optional_message_format, ...);

/**
 * Variadic form of show_usage, allowing the use of va_list rather than
 * arbitrary arguments.
 * */
void variadic_show_usage(const struct usage_description *cmd_usage,
		const char *optional_message_format, va_list varargs, int err);

/**
 * Print usage of a command to stdout by supplying a list of usage_descriptions.
 *
 * If err is non-zero, outputs to stderr. Otherwise, outputs to stdout.
 * */
void show_options(const struct option_description *opts, int err);

/**
 * Combination of show_usage() and show_options().
 * */
void show_usage_with_options(const struct usage_description *cmd_usage,
		const struct option_description *opts, int err,
		const char *optional_message_format, ...);

/**
 * Variadic form of show_usage_with_options, allowing the use of va_list rather
 * than arbitrary arguments.
 * */
void variadic_show_usage_with_options(const struct usage_description *cmd_usage,
		const struct option_description *opts,
		const char *optional_message_format, va_list varargs, int err);

/**
 * Determine whether a command line argument matches an option description.
 *
 * This function will:
 * - if description is of type OPTION_COMMAND_T
 *   - return 1 if arg exactly matches the description str_name field, else
 *   returns 0
 * - if arg is prefixed by two dashes
 *   - return 1 if arg after the dashes matches the description l_flag field,
 *   else returns 0
 * - if arg is prefixed by a single dash
 *   - if arg has a length greater than 2 and description type field is
 *   OPTION_BOOL_T, i.e. short combined boolean format
 *     - return 1 if arg after the dash contains the description s_flag
 *     character, else returns 0
 *   - if arg has a length of exactly 2
 *     - return 1 if arg after the dash matches the description s_flag field,
 *     else returns 0
 * - return 0 if the length of arg is less than 2, or arg is not prefixed by a
 * dash (not a valid command line flag)
 * */
int argument_matches_option(const char *arg,
		struct option_description description);

/**
 * Performs argument validation to verify that the argument:
 * - matches a valid option description, e.g. -a --abc
 * - matches a valid boolean flag if is a short combined boolean flag, e.g. -abc
 *
 * This function returns true if the argument is valid, false otherwise. If the
 * argument is not prefixed by a dash, this function will return true (still a
 * valid argument). If the provided argument has a length of zero, this function
 * will return false. If the argument does not have an associated entry in
 * arg_usage_descriptions, this function will return false.
 * */
int is_valid_argument(const char *arg,
		const struct option_description arg_usage_descriptions[]);

#endif //GITCHAT_USAGE_H
