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

static int embed_message(const char *file_path, const char *output_file,
		const char *message, int quiet);
static int embed_file(const char *file_path, const char *output_file,
		const char *file_to_embed, int quiet);
static void embed_message_chunk(int input_fd, int output_fd, void *new_chunk,
		size_t chunk_len);
static void compute_chunk_data(struct strbuf *buffer, const char *message);
static void print_summary(const char *original_file_path,
		const char *new_file_path, const struct strbuf *data_chunk);
static void print_file_summary(const char *file_path, int filename_table_len);
static void print_hex_dump(const char *data, size_t len);
static int compute_md5_sum(int fd, unsigned char md5_hash[]);

int cmd_embed(int argc, char *argv[])
{
	const char *message = NULL;
	const char *output_file = NULL;
	const char *file_to_embed = NULL;
	int help = 0;
	int quiet = 0;

	const struct usage_string main_cmd_usage[] = {
			USAGE("steg-png --embed (-m | --message <message>) [-o | --output <file>] <file>"),
			USAGE("steg-png --embed (-f | --file <file>) [-o | --output <file>] <file>"),
			USAGE("steg-png --embed (-h | --help)"),
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

/**
 * Embed a simple string message in a PNG image.
 *
 * The fie_path argument must be the path to a PNG image. If the image is not a
 * PNG or is corrupt the application will DIE().
 *
 * The output_file must be the path and filename where the new PNG will be saved.
 * If the file already exists, the file will be truncated to zero bytes and
 * overwritten.
 *
 * The provided message will be encoded in a chunk of type "stEG" that is located
 * before the IEND chunk.
 *
 * If quiet is zero, the summary details are suppressed. Error messages will
 * still be printed to stderr.
 * */
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

/**
 * Embed the contents of a file in a PNG image.
 *
 * The fie_path argument must be the path to a PNG image. If the image is not a
 * PNG or is corrupt the application will DIE().
 *
 * The output_file must be the path and filename where the new PNG will be saved.
 * If the file already exists, the file will be truncated to zero bytes and
 * overwritten.
 *
 * The provided file will be read in a chunk of type "stEG" that is located
 * before the IEND chunk.
 *
 * If quiet is zero, the summary details are suppressed. Error messages will
 * still be printed to stderr.
 * */
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

/**
 * Embed a given chunk in a file represented by an open file descriptor input_fd,
 * into a new file represented by an open file descriptor output_fd.
 *
 * The new chunk is written in the file directly preceeding the IEND chunk that
 * signals the end of a PNG file.
 * */
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

/**
 * From a message string, generate and attach to the given strbuf a correctly
 * formatted chunk of type "stEG".
 * */
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

/**
 * Print a summary of a embedded chunk operation.
 *
 * Prints the input file and output file to stdout in the following format:
 * in  <filename> <file mode> <file length> <md5 hash>
 * out <filename> <file mode> <file length> <md5 hash>
 *
 * embedded message chunk:
 * type: <string>	length: <unsigned long>	crc: <unsigned hex>
 * <hexdump of message chunk>
 * */
static void print_summary(const char *original_file_path,
		const char *new_file_path, const struct strbuf *data_chunk)
{
	const char *filename_from = strrchr(original_file_path, '/');
	filename_from = !filename_from ? original_file_path : filename_from + 1;
	const char *filename_to = strrchr(new_file_path, '/');
	filename_to = !filename_to ? new_file_path : filename_to + 1;

	// compute table boundaries
	size_t filename_from_len = strlen(filename_from);
	size_t filename_to_len = strlen(filename_to);
	size_t max_filename_len = (filename_from_len >= filename_to_len) ? (filename_from_len) : (filename_to_len);

	fprintf(stdout, "%-3s ", "in");
	print_file_summary(original_file_path, (int)(max_filename_len - filename_from_len + 1));

	fprintf(stdout, "%-3s ", "out");
	print_file_summary(new_file_path, (int)(max_filename_len - filename_to_len + 1));

	if (data_chunk) {
		char type[CHUNK_TYPE_LENGTH];
		u_int32_t length;
		u_int32_t crc;

		if (png_parse_chunk_type(data_chunk, type))
			BUG("failed to parse chunk type");

		if (png_parse_chunk_data_length(data_chunk, &length))
			BUG("failed to parse chunk data length");

		if (png_parse_chunk_crc(data_chunk, &crc))
			BUG("failed to parse chunk CRC");

		fprintf(stdout, "\nembedded message chunk:\n");
		fprintf(stdout, "type: %.*s\t", CHUNK_TYPE_LENGTH, type);
		fprintf(stdout, "length: %lu\t", (unsigned long) length);
		fprintf(stdout, "crc: %lx\n", (unsigned long) crc);

		// print hexdump
		print_hex_dump(data_chunk->buff, data_chunk->len);
	}
}

/**
 * Print a summary of a file, with table formatting capability.
 *
 * Prints the file summary in the following format:
 * <filename> <file mode> <file length> <md5 hash>
 *
 * The filename may be padded with whitespace using the filename_table_len
 * argument.
 * */
static void print_file_summary(const char *file_path, int filename_table_len)
{
	unsigned char md5_hash[MD5_DIGEST_SIZE];
	struct stat st;
	int fd;

	const char *filename_from = strrchr(file_path, '/');
	filename_from = !filename_from ? file_path : filename_from + 1;

	// print input file summary
	if (lstat(file_path, &st) && errno == ENOENT)
		FATAL("failed to stat %s'", file_path);

	fd = open(file_path, O_RDONLY);
	if (fd < 0)
		FATAL(FILE_OPEN_FAILED, file_path);

	if (compute_md5_sum(fd, md5_hash))
		FATAL("failed to compute md5 hash of file '%s'", file_path);

	fprintf(stdout, "%s %*s", filename_from, filename_table_len, " ");
	fprintf(stdout, "%o %lu ", st.st_mode, st.st_size);
	for (size_t i = 0; i < MD5_DIGEST_SIZE; i++)
		fprintf(stdout, "%02x", md5_hash[i]);
	fprintf(stdout, "\n");

	close(fd);
}

/**
 * Print a hexdump of a given buffer. The hexdump is formatted similar to
 * the hexdump tool.
 * */
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

/**
 * Compute the md5 sum of an open file. The md5_hash argument must be an array of
 * length MD5_DIGEST_SIZE.
 * */
static int compute_md5_sum(int fd, unsigned char md5_hash[])
{
	struct md5_ctx ctx;
	ssize_t bytes_read = 0;

	md5_init_ctx(&ctx);

	char buffer[BUFF_LEN];
	while ((bytes_read = recoverable_read(fd, buffer, BUFF_LEN)) > 0)
		md5_process_bytes(buffer, bytes_read, &ctx);

	if (bytes_read < 0)
		return 1;

	md5_finish_ctx(&ctx, md5_hash);
	return 0;
}
