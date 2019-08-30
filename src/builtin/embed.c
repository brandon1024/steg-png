#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <ctype.h>
#include <errno.h>
#include <sys/stat.h>
#include <arpa/inet.h>

#include "crc.h"
#include "md5.h"
#include "strbuf.h"
#include "parse-options.h"
#include "png-chunk-processor.h"
#include "utils.h"

#define BUFF_LEN 1024

// TODO fix parse_options with string args (?)
// TODO update print_summary to show length, type and CRC fields
// TODO update usage string with new options

static int embed_message(const char *file_path, const char *output_file,
		const char *message, int quiet);
static int embed_file(const char *file_path, const char *output_file,
		const char *file_to_embed, int quiet);
static void embed_message_chunk(int input_fd, int output_fd, void *new_chunk,
		size_t chunk_len);
static void compute_chunk_data(struct strbuf *buffer, const char *message);
static void print_summary(const char *original_file_path,
		const char *new_file_path, const struct strbuf *data_chunk);
static void print_hex_dump(const char *data, size_t len);

int cmd_embed(int argc, char *argv[])
{
	const char *message = NULL;
	const char *output_file = NULL;
	const char *file_to_embed = NULL;
	int help = 0;
	int quiet = 0;

	const struct usage_string main_cmd_usage[] = {
			USAGE("steg-png [--embed] (--message | -m <message>) <file>"),
			USAGE_END()
	};

	const struct command_option main_cmd_options[] = {
			OPT_STRING('m', "message", "message", "specify the message to embed in the png image", &message),
			OPT_STRING('f', "file", "file", "specify a file to embed in the png image", &file_to_embed),
			OPT_STRING('o', "output", "file", "output to a specific file", &output_file),
			OPT_BOOL('q', "quiet", "suppress output to stdout", &quiet),
			OPT_BOOL('h', "help", "show help and exit", &help),
			OPT_END()
	};

	argc = parse_options(argc, argv, main_cmd_options, 0, 1);
	if (help) {
		show_usage_with_options(main_cmd_usage, main_cmd_options, 0, NULL);
		return 0;
	}

	if (argc != 1) {
		show_usage_with_options(main_cmd_usage, main_cmd_options, 1, "too many options");
		return 1;
	}

	if (file_to_embed && message) {
		show_usage_with_options(main_cmd_usage, main_cmd_options, 1, "cannot mix --file and --message options");
		return 1;
	}

	if (file_to_embed)
		return embed_file(argv[0], output_file, file_to_embed, quiet);

	return embed_message(argv[0], output_file, message, quiet);
}

static int embed_message(const char *file_path, const char *output_file, const char *message, int quiet)
{
	struct stat st;
	if (lstat(file_path, &st) && errno == ENOENT)
		FATAL("failed to stat %s'", file_path);

	int in_fd = open(file_path, O_RDONLY);
	if (in_fd < 0)
		FATAL(FILE_OPEN_FAILED, file_path);

	struct strbuf output_file_path;
	strbuf_init(&output_file_path);

	if (output_file)
		strbuf_attach_str(&output_file_path, output_file);
	else
		strbuf_attach_fmt(&output_file_path, "%s.steg", file_path);

	int out_fd = open(output_file_path.buff, O_WRONLY | O_CREAT | O_TRUNC, st.st_mode);
	if (out_fd < 0)
		FATAL(FILE_OPEN_FAILED, output_file_path.buff);

	// build message buffer
	struct strbuf message_buf;
	strbuf_init(&message_buf);
	if (!message) {
		char buffer[BUFF_LEN];
		ssize_t bytes_read = 0;
		while ((bytes_read = recoverable_read(STDIN_FILENO, buffer, BUFF_LEN)) > 0)
			strbuf_attach_bytes(&message_buf, buffer, bytes_read);

		if (bytes_read < 0)
			FATAL("unable to read message from stdin", file_path);
	} else {
		strbuf_attach_str(&message_buf, message);
	}

	// prepare chunk
	struct strbuf chunk_data;
	strbuf_init(&chunk_data);
	compute_chunk_data(&chunk_data, message_buf.buff);

	embed_message_chunk(in_fd, out_fd, message_buf.buff, message_buf.len);

	close(in_fd);
	close(out_fd);

	if (!quiet)
		print_summary(file_path, output_file_path.buff, &chunk_data);

	strbuf_release(&message_buf);
	strbuf_release(&chunk_data);
	strbuf_release(&output_file_path);

	return 0;
}

static int embed_file(const char *file_path, const char *output_file,
		const char *file_to_embed, int quiet)
{
	struct stat st;
	if (lstat(file_path, &st) && errno == ENOENT)
		FATAL("failed to stat %s'", file_path);

	int in_fd = open(file_path, O_RDONLY);
	if (in_fd < 0)
		FATAL(FILE_OPEN_FAILED, file_path);

	struct strbuf output_file_path;
	strbuf_init(&output_file_path);

	if (output_file)
		strbuf_attach_str(&output_file_path, output_file);
	else
		strbuf_attach_fmt(&output_file_path, "%s.steg", file_path);

	int out_fd = open(output_file_path.buff, O_WRONLY | O_CREAT | O_TRUNC, st.st_mode);
	if (out_fd < 0)
		FATAL(FILE_OPEN_FAILED, output_file_path.buff);

	if (lstat(file_to_embed, &st) && errno == ENOENT)
		FATAL("failed to stat %s'", file_to_embed);

	u_int32_t file_size = st.st_size;

	int file_to_embed_fd = open(file_to_embed, O_RDONLY);
	if (file_to_embed_fd < 0)
		FATAL(FILE_OPEN_FAILED, file_to_embed);

	struct chunk_iterator_ctx ctx;
	int status = chunk_iterator_init_ctx(&ctx, in_fd);
	if (status < 0)
		DIE("failed to read from input file");
	else if(status > 0)
		DIE("input file is not a PNG (does not conform to RFC 2083)");

	if (recoverable_write(out_fd, PNG_SIG, SIGNATURE_LENGTH) != SIGNATURE_LENGTH)
		FATAL("failed to write PNG file signature to output file");

	int IEND_found = 0;
	char type_buffer[CHUNK_TYPE_LENGTH];
	char buffer[BUFF_LEN];
	struct strbuf chunk_buffer;

	strbuf_init(&chunk_buffer);
	do {
		int has_next_chunk = chunk_iterator_next_chunk(&ctx, &chunk_buffer);
		if (has_next_chunk < 0)
			DIE("failed to read corrupted chunk in input file");

		if (!has_next_chunk)
			break;

		if (png_parse_chunk_type(&chunk_buffer, type_buffer))
			DIE("unexpected chunk type in input file");

		if (!memcmp(IEND_CHUNK_TYPE, type_buffer, CHUNK_TYPE_LENGTH)) {
			if (IEND_found)
				DIE("non-compliant input file: IEND chunk found twice in input file");

			IEND_found = 1;

			// write data size
			u_int32_t len_net_order = htonl(file_size);
			if (recoverable_write(out_fd, &len_net_order, sizeof(u_int32_t)) != sizeof(u_int32_t))
				FATAL("failed to write data chunk length to output file");

			// write chunk type
			char chunk_type[] = {'s', 't', 'E', 'G'};
			if (recoverable_write(out_fd, chunk_type, sizeof(char) * ARRAY_SIZE(chunk_type)) != sizeof(char) * ARRAY_SIZE(chunk_type))
				FATAL("failed to write data chunk length to output file");

			// write chunk data and compute CRC
			u_int32_t total_bytes_read = 0;
			ssize_t bytes_read = 0;
			u_int32_t crc = 0;
			while ((bytes_read = recoverable_read(file_to_embed_fd, buffer, BUFF_LEN)) > 0) {
				total_bytes_read += bytes_read;
				crc = crc32_update(crc, buffer, bytes_read);
				if (recoverable_write(out_fd, buffer, bytes_read) != bytes_read)
					FATAL("failed to write new chunk to output file");
			}

			if (bytes_read < 0)
				FATAL("failed to read from file '%s'", file_to_embed);

			if (total_bytes_read != file_size)
				FATAL("stat file size mismatch");

			// write CRC
			u_int32_t crc_net_order = htonl(crc);
			if (recoverable_write(out_fd, &crc_net_order, sizeof(u_int32_t)) != sizeof(u_int32_t))
				FATAL("failed to write CRC field to output file");
		}

		// write the chunk
		if (recoverable_write(out_fd, chunk_buffer.buff, chunk_buffer.len) != chunk_buffer.len)
			FATAL("failed to write chunk to output file");

		strbuf_clear(&chunk_buffer);
	} while(1);

	if (!IEND_found)
		DIE("input file missing IEND chunk");

	chunk_iterator_destroy_ctx(&ctx);
	strbuf_release(&chunk_buffer);

	close(in_fd);
	close(out_fd);
	close(file_to_embed_fd);

	if (!quiet)
		print_summary(file_path, output_file_path.buff, NULL);

	return 0;
}

static void embed_message_chunk(int input_fd, int output_fd, void *new_chunk,
		size_t chunk_len)
{
	struct strbuf chunk_buffer;
	strbuf_init(&chunk_buffer);

	struct chunk_iterator_ctx ctx;
	int status = chunk_iterator_init_ctx(&ctx, input_fd);
	if (status < 0)
		DIE("failed to read from input file");
	else if(status > 0)
		DIE("input file is not a PNG (does not conform to RFC 2083)");

	if (recoverable_write(output_fd, PNG_SIG, SIGNATURE_LENGTH) != SIGNATURE_LENGTH)
		FATAL("failed to write PNG file signature to output file");

	int IEND_found = 0;
	char type_buffer[CHUNK_TYPE_LENGTH];
	do {
		int has_next_chunk = chunk_iterator_next_chunk(&ctx, &chunk_buffer);
		if (has_next_chunk < 0)
			DIE("failed to read corrupted chunk in input file");

		if (!has_next_chunk)
			break;

		if (png_parse_chunk_type(&chunk_buffer, type_buffer))
			DIE("unexpected chunk type in input file");

		if (!memcmp(IEND_CHUNK_TYPE, type_buffer, CHUNK_TYPE_LENGTH)) {
			if (IEND_found)
				DIE("non-compliant input file: IEND chunk found twice in input file");

			IEND_found = 1;
			// write new chunk
			if (recoverable_write(output_fd, new_chunk, chunk_len) != chunk_len)
				FATAL("failed to write new chunk to output file");
		}

		// write the chunk
		if (recoverable_write(output_fd, chunk_buffer.buff, chunk_buffer.len) != chunk_buffer.len)
			FATAL("failed to write chunk to output file");

		strbuf_clear(&chunk_buffer);
	} while(1);

	if (!IEND_found)
		DIE("input file missing IEND chunk");

	chunk_iterator_destroy_ctx(&ctx);
	strbuf_release(&chunk_buffer);
}

static void compute_chunk_data(struct strbuf *buffer, const char *message)
{
	struct strbuf chunk_data;
	strbuf_init(&chunk_data);
	strbuf_attach_str(&chunk_data, "stEG");
	strbuf_attach(&chunk_data, message, strlen(message));

	uint32_t msg_len = strlen(message);
	uint32_t crc = crc32(chunk_data.buff, chunk_data.len);

	// convert from host byte order to network byte order
	msg_len = htonl(msg_len);
	crc = htonl(crc);

	strbuf_attach_bytes(buffer, &msg_len, sizeof(uint32_t));
	strbuf_attach_bytes(buffer, chunk_data.buff, chunk_data.len);
	strbuf_attach_bytes(buffer, &crc, sizeof(uint32_t));

	strbuf_release(&chunk_data);
}

static void print_summary(const char *original_file_path,
		const char *new_file_path, const struct strbuf *data_chunk)
{
	struct md5_ctx ctx;
	unsigned char md5_hash[MD5_DIGEST_SIZE];
	struct stat st;
	int fd;
	ssize_t bytes_read = 0;
	char buffer[BUFF_LEN];

	const char *filename_from = strrchr(original_file_path, '/');
	filename_from = !filename_from ? original_file_path : filename_from + 1;
	const char *filename_to = strrchr(new_file_path, '/');
	filename_to = !filename_to ? new_file_path : filename_to + 1;

	// compute table boundaries
	size_t filename_from_len = strlen(filename_from);
	size_t filename_to_len = strlen(filename_to);
	size_t max_filename_len = (filename_from_len >= filename_to_len) ? (filename_from_len) : (filename_to_len);

	// print input file summary
	if (lstat(original_file_path, &st) && errno == ENOENT)
		FATAL("failed to stat %s'", original_file_path);

	fd = open(original_file_path, O_RDONLY);
	if (fd < 0)
		FATAL(FILE_OPEN_FAILED, original_file_path);

	md5_init_ctx(&ctx);
	while ((bytes_read = recoverable_read(fd, buffer, BUFF_LEN)) > 0)
		md5_process_bytes(buffer, bytes_read, &ctx);

	if (bytes_read < 0)
		FATAL("failed to read from file '%s'", original_file_path);

	md5_finish_ctx(&ctx, md5_hash);

	fprintf(stdout, "%-3s ", "in");
	fprintf(stdout, "%s %*s", filename_from, (int)(max_filename_len - filename_from_len + 1), " ");
	fprintf(stdout, "%o %lu ", st.st_mode, st.st_size);
	for (size_t i = 0; i < MD5_DIGEST_SIZE; i++)
		fprintf(stdout, "%02x", md5_hash[i]);
	fprintf(stdout, "\n");

	close(fd);

	// print output file summary
	if (lstat(new_file_path, &st) && errno == ENOENT)
		FATAL("failed to stat %s'", new_file_path);

	fd = open(new_file_path, O_RDONLY);
	if (fd < 0)
		FATAL(FILE_OPEN_FAILED, new_file_path);

	md5_init_ctx(&ctx);
	while ((bytes_read = recoverable_read(fd, buffer, BUFF_LEN)) > 0)
		md5_process_bytes(buffer, bytes_read, &ctx);

	if (bytes_read < 0)
		FATAL("failed to read from file '%s'", new_file_path);

	md5_finish_ctx(&ctx, md5_hash);

	fprintf(stdout, "%-3s ", "out");
	fprintf(stdout, "%s %*s", filename_to, (int)(max_filename_len - filename_to_len + 1), " ");
	fprintf(stdout, "%o %lu ", st.st_mode, st.st_size);
	for (size_t i = 0; i < MD5_DIGEST_SIZE; i++)
		fprintf(stdout, "%02x", md5_hash[i]);
	fprintf(stdout, "\n\n");

	if (data_chunk) {
		// print message chunk
		fprintf(stdout, "embedded message chunk:\n");
		print_hex_dump(data_chunk->buff, data_chunk->len);
	}
}

static void print_hex_dump(const char *data, size_t len)
{
	for (size_t i = 0; i < len; i += 16) {
		fprintf(stdout, "%08lu  ", i);

		for (size_t j = i; (j < i + 16) && (j < len); j++)
			fprintf(stdout, "%02hhx ", data[j]);

		fprintf(stdout, "%*s", (int)(i + 16 > len ? 3 * (i + 16 - len) + 1 : 1), " ");

		fprintf(stdout, "|");
		for (size_t j = i; (j < i + 16) && (j < len); j++)
			fprintf(stdout, "%c", isprint(data[j]) ? data[j] : '.');
		fprintf(stdout, "|\n");
	}
}
