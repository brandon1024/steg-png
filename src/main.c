#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <fcntl.h>
#include <stdint.h>
#include <unistd.h>

#include "crc.h"
#include "strbuf.h"
#include "usage.h"
#include "utils.h"

#define BUFF_LEN 1024

static const struct usage_description main_cmd_usage[] = {
		USAGE("steg-png [--embed] (--message | -m) <message> <file>"),
		USAGE("steg-png --extract <file>"),
		USAGE_END()
};

static const struct option_description main_cmd_options[] = {
		OPT_LONG_BOOL("embed", "embed a message in a png image"),
		OPT_STRING('m', "message", "message", "specify the message to embed in the png image"),
		OPT_LONG_BOOL("extract", "extract a message in a png image"),
		OPT_BOOL('h', "help", "show help and exit"),
		OPT_END()
};

static int embed_message(const char *file_path, const char *message);
static int extract_message(const char *file_path);
static void compute_chunk_data(struct strbuf *buffer, const char *message);
static void show_main_usage(int err, const char *optional_message_format, ...);

int main(int argc, char *argv[])
{
	//Show usage and return if no arguments provided
	if (argc < 2) {
		show_main_usage(0, NULL);
		return 0;
	}

	const char *message = NULL;
	const char *file_path = NULL;
	int embed = 0, extract = 0;

	for (size_t arg_index = 0; arg_index < argc; arg_index++) {
		char *arg = argv[arg_index];

		if (argument_matches_option(arg, main_cmd_options[0])) {
			embed = 1;
			continue;
		}

		if (argument_matches_option(arg, main_cmd_options[1])) {
			if (++arg_index >= argc) {
				show_main_usage(1, "error: no message provided with %s", arg);
				return 1;
			}

			message = argv[arg_index];
			continue;
		}

		if (argument_matches_option(arg, main_cmd_options[2])) {
			extract = 1;
			continue;
		}

		if (argument_matches_option(arg, main_cmd_options[3])) {
			show_main_usage(0, NULL);
			return 0;
		}

		file_path = arg;
	}

	if (embed && extract) {
		show_main_usage(1, "error: mixing --embed and --extract is undefined");
		return 1;
	}

	if (!embed && !extract)
		embed = 1;

	if (embed && !message) {
		show_main_usage(1, "error: no message provided");
		return 1;
	}

	if (!file_path) {
		show_main_usage(1, "error: no input file specified");
		return 1;
	}

	if (embed)
		return embed_message(file_path, message);

	return extract_message(file_path);
}

static int embed_message(const char *file_path, const char *message)
{
	char buffer[1024];

	int in_fd = open(file_path, O_RDONLY);
	if (in_fd < 0)
		FATAL(FILE_OPEN_FAILED, file_path);

	struct strbuf output_file_path;
	strbuf_init(&output_file_path);
	strbuf_attach_fmt(&output_file_path, "%s.steg", file_path);

	int out_fd = open(output_file_path.buff, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
	if (out_fd < 0)
		FATAL(FILE_OPEN_FAILED, output_file_path.buff);

	struct strbuf file_contents;
	strbuf_init(&file_contents);

	// read content of file into memory
	ssize_t bytes_read = 0;
	while ((bytes_read = recoverable_read(in_fd, buffer, BUFF_LEN)) > 0)
		strbuf_attach_bytes(&file_contents, buffer, bytes_read);

	if (bytes_read < 0)
		FATAL("unable to read from file '%s.", file_path);

	// prepare chunk
	struct strbuf chunk_data;
	strbuf_init(&chunk_data);
	compute_chunk_data(&chunk_data, message);

	// find position of IEND chunk
	int IEND_found = 0;
	char *chunk_type_begin = file_contents.buff;
	while ((chunk_type_begin = memchr(chunk_type_begin, 'I', file_contents.buff + file_contents.len - chunk_type_begin))) {
		if ((chunk_type_begin + 3) >= file_contents.buff + file_contents.len)
			break;

		if (*(chunk_type_begin + 1) != 'E') {
			chunk_type_begin += 1;
			continue;
		}

		if (*(chunk_type_begin + 2) != 'N') {
			chunk_type_begin += 2;
			continue;
		}

		if (*(chunk_type_begin + 3) != 'D') {
			chunk_type_begin += 3;
			continue;
		}

		IEND_found = 1;
		break;
	}

	if (!IEND_found)
		FATAL("corrupted input file; missing critical IEND chunk");

	char *insert_point = chunk_type_begin - 4;
	ssize_t expected_bytes_written = insert_point - file_contents.buff;
	if (recoverable_write(out_fd, file_contents.buff, expected_bytes_written) != expected_bytes_written)
		FATAL("failed to write to file '%s'", output_file_path.buff);

	expected_bytes_written = chunk_data.len;
	if (recoverable_write(out_fd, chunk_data.buff, expected_bytes_written) != expected_bytes_written)
		FATAL("failed to write to file '%s'", output_file_path.buff);

	expected_bytes_written = file_contents.buff + file_contents.len - insert_point;
	if (recoverable_write(out_fd, insert_point, expected_bytes_written) != expected_bytes_written)
		FATAL("failed to write to file '%s'", output_file_path.buff);

	strbuf_release(&chunk_data);
	strbuf_release(&file_contents);
	strbuf_release(&output_file_path);

	close(in_fd);
	close(out_fd);

	return 0;
}

static void compute_chunk_data(struct strbuf *buffer, const char *message)
{
	struct strbuf chunk_data;
	strbuf_init(&chunk_data);
	strbuf_attach_str(&chunk_data, "stEG");
	strbuf_attach(&chunk_data, message, strlen(message));

	uint32_t msg_len = strlen(message);
	uint32_t crc = crc32(chunk_data.buff, chunk_data.len);

	strbuf_attach_bytes(buffer, &msg_len, sizeof(uint32_t));
	strbuf_attach_bytes(buffer, chunk_data.buff, chunk_data.len);
	strbuf_attach_bytes(buffer, &crc, sizeof(uint32_t));

	strbuf_release(&chunk_data);
}

static int extract_message(const char *file_path)
{
	return 1;
}

static void show_main_usage(int err, const char *optional_message_format, ...)
{
	va_list varargs;
	va_start(varargs, optional_message_format);

	variadic_show_usage_with_options(main_cmd_usage, main_cmd_options,
			optional_message_format, varargs, err);

	va_end(varargs);
}
