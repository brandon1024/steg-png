#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#include <time.h>

#include "crc.h"
#include "md5.h"
#include "strbuf.h"
#include "parse-options.h"
#include "png-chunk-processor.h"
#include "utils.h"

#define BUFF_LEN 1024
#define DATA_TOKEN (0x4c)

static int embed_message(const char *, const char *, const char *, int);
static int embed_file(const char *, const char *, const char *, int);
static off_t embed_data(int, int, int, const void *, size_t);
static u_int32_t write_and_update_crc(int, const void *, size_t, u_int32_t);
static void print_summary(const char *, const char *, off_t);
static void print_file_summary(const char *, int);
static int compute_md5_sum(int, unsigned char[]);

int cmd_embed(int argc, char *argv[])
{
	const char *message = NULL;
	const char *output_file = NULL;
	const char *file_to_embed = NULL;
	int help = 0;
	int quiet = 0;

	const struct usage_string main_cmd_usage[] = {
			USAGE("steg-png embed (-m | --message <message>) [-o | --output <file>] <file>"),
			USAGE("steg-png embed (-f | --file <file>) [-o | --output <file>] <file>"),
			USAGE("steg-png embed (-h | --help)"),
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
 * The file_path argument must be the path to a PNG image. If the image is not a
 * PNG or is corrupt the application will exit abruptly.
 *
 * The output_file must be the path and filename where the new PNG will be saved.
 * If the file already exists, the file will be truncated to zero bytes and
 * overwritten.
 *
 * The provided message will be encoded in a chunk of type "stEG" that is located
 * directly before the IEND chunk.
 *
 * If quiet is zero, the summary details are suppressed. Error messages will
 * still be printed to stderr.
 * */
static int embed_message(const char *file_path, const char *output_file,
		const char *message, int quiet)
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

	size_t chunk_pos = embed_data(in_fd, out_fd, -1, message_buf.buff, message_buf.len);
	if (!quiet)
		print_summary(file_path, output_file_path.buff, chunk_pos);

	close(in_fd);
	close(out_fd);

	strbuf_release(&message_buf);
	strbuf_release(&output_file_path);

	return 0;
}

/**
 * Embed the contents of a file in a PNG image.
 *
 * The fie_path argument must be the path to a PNG image. If the image is not a
 * PNG or is corrupt the application will exit abruptly.
 *
 * The output_file must be the path and filename where the new PNG will be saved.
 * If the file already exists, the file will be truncated to zero bytes and
 * overwritten.
 *
 * The provided file will be read in a chunk of type "stEG" that is located
 * before the IEND chunk.
 *
 * If quiet is nonzero, the summary details are suppressed. Error messages will
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

	int file_to_embed_fd = open(file_to_embed, O_RDONLY);
	if (file_to_embed_fd < 0)
		FATAL(FILE_OPEN_FAILED, file_to_embed);

	long chunk_pos = embed_data(in_fd, out_fd, file_to_embed_fd, NULL, 0);
	if (!quiet)
		print_summary(file_path, output_file_path.buff, chunk_pos);

	close(in_fd);
	close(out_fd);
	close(file_to_embed_fd);

	return 0;
}

/**
 * Embed arbitrary data from a file or string to a PNG file.
 *
 * in_fd must be an open file descriptor to the source PNG file.
 *
 * out_fd must be an open file descriptor to the destination PNG file.
 *
 * If embedding a file, data_fd must be an open file descriptor to the file
 * that will be embedded. Otherwise, if embedding a string message, message must
 * be non-null.
 *
 * Returns the position in the output file of the first byte of the embedded chunk.
 * This value can be used to seek to that position in the file for further use.
 * */
static off_t embed_data(int in_fd, int out_fd, int data_fd, const void *data,
		size_t message_len)
{
	if (data && data_fd != -1)
		BUG("either data or data_fd must be defined, not both");
	if (!data && data_fd == -1)
		BUG("either data or data_fd must be defined");

	// compute the chunk data length
	u_int32_t file_size = 0;
	if (data) {
		file_size = message_len;
	} else {
		struct stat st;
		if (fstat(data_fd, &st) && errno == ENOENT)
			FATAL("failed to stat file descriptor %d", data_fd);

		file_size = st.st_size;
	}

	struct chunk_iterator_ctx ctx;
	int status = chunk_iterator_init_ctx(&ctx, in_fd);
	if (status < 0)
		DIE("failed to read from input file");
	else if(status > 0)
		DIE("input file is not a PNG (does not conform to RFC 2083)");

	if (recoverable_write(out_fd, PNG_SIG, SIGNATURE_LENGTH) != SIGNATURE_LENGTH)
		FATAL("failed to write PNG file signature to output file");

	int64_t unix_time = time(NULL);
	unsigned char temporary_buffer[BUFF_LEN];
	off_t embedded_chunk_offset = 0;

	int has_next_chunk, IEND_found = 0;
	while ((has_next_chunk = chunk_iterator_has_next(&ctx)) > 0) {
		if (chunk_iterator_next(&ctx) != 0)
			FATAL("unexpected error while parsing input file");

		struct png_chunk_detail chunk = ctx.current_chunk;

		// if the current chunk is the IEND chunk, then write our chunk
		if (!memcmp(chunk.chunk_type, IEND_CHUNK_TYPE, CHUNK_TYPE_LENGTH)) {
			if (IEND_found)
				DIE("non-compliant input file with IEND chunk defined twice (does not conform to RFC 2083)");

			embedded_chunk_offset = lseek(out_fd, 0, SEEK_CUR);
			if (embedded_chunk_offset < 0)
				FATAL("failed to read the file position in the output file");

			IEND_found = 1;
			u_int32_t crc = 0;

			// write data size
			unsigned long header_len = sizeof(int64_t) + sizeof(unsigned char);
			u_int32_t len_net_order = htonl(file_size + header_len);
			if (recoverable_write(out_fd, &len_net_order, sizeof(u_int32_t)) != sizeof(u_int32_t))
				FATAL("failed to write data chunk length to output file");

			// write chunk type
			char chunk_type[CHUNK_TYPE_LENGTH] = {'s', 't', 'E', 'G'};
			crc = write_and_update_crc(out_fd, chunk_type, sizeof(char) * CHUNK_TYPE_LENGTH, crc);

			// write header (time and token)
			unsigned char token = DATA_TOKEN;
			crc = write_and_update_crc(out_fd, &unix_time, sizeof(u_int64_t), crc);
			crc = write_and_update_crc(out_fd, &token, sizeof(unsigned char), crc);

			if (data) {
				crc = write_and_update_crc(out_fd, data, message_len, crc);
			} else {
				// write chunk data and compute CRC
				char buffer[BUFF_LEN];
				size_t total_bytes_read = 0;
				ssize_t bytes_read = 0;
				while ((bytes_read = recoverable_read(data_fd, buffer, BUFF_LEN)) > 0) {
					total_bytes_read += bytes_read;
					crc = write_and_update_crc(out_fd, buffer, bytes_read, crc);
				}

				if (bytes_read < 0)
					FATAL("failed to read from fd %d", data_fd);
				if (total_bytes_read != file_size)
					FATAL("stat file size mismatch");
			}

			// write CRC
			u_int32_t crc_net_order = htonl(crc);
			if (recoverable_write(out_fd, &crc_net_order, sizeof(u_int32_t)) != sizeof(u_int32_t))
				FATAL("failed to write CRC field to output file");
		}

		// write the chunk data length to output file
		u_int32_t data_len_net_order = htonl(chunk.data_length);
		if (recoverable_write(out_fd, &data_len_net_order, sizeof(u_int32_t)) != sizeof(u_int32_t))
			FATAL("failed to write data length field to output file");

		// write the chunk type to output file
		u_int32_t chunk_crc = 0;
		chunk_crc = write_and_update_crc(out_fd, chunk.chunk_type, sizeof(char) * CHUNK_TYPE_LENGTH, chunk_crc);

		// write the chunk data to output file
		while (1) {
			ssize_t bytes_read = chunk_iterator_read_data(&ctx, temporary_buffer, BUFF_LEN);
			if (bytes_read < 0)
				FATAL("unexpected error while parsing input file");
			if (bytes_read == 0)
				break;

			chunk_crc = write_and_update_crc(out_fd, temporary_buffer, bytes_read, chunk_crc);
		}

		if (chunk_crc != chunk.chunk_crc)
			WARN("%.*s chunk at file offset %d has invalid CRC -- file may be corrupted",
					CHUNK_TYPE_LENGTH, chunk.chunk_type, ctx.chunk_file_offset);

		// write the chunk CRC to output file
		u_int32_t crc_net_order = htonl(chunk.chunk_crc);
		if (recoverable_write(out_fd, &crc_net_order, sizeof(u_int32_t)) != sizeof(u_int32_t))
			FATAL("failed to write CRC field to output file");
	}

	if (has_next_chunk < 0)
		FATAL("unexpected error while parsing input file");

	if (!IEND_found)
		DIE("non-compliant input file no IEND chunk defined (does not conform to RFC 2083)");

	chunk_iterator_destroy_ctx(&ctx);
	return embedded_chunk_offset;
}

/**
 * Write arbitrary data to a given file descriptor, and return an updated CRC.
 * */
static u_int32_t write_and_update_crc(int fd, const void *data, size_t length, u_int32_t crc)
{
	if (recoverable_write(fd, data, length) != length)
		FATAL("failed to write new chunk to output file descriptor %d", fd);

	return crc32_update(crc, data, length);
}

/**
 * Print a summary of a embedded chunk operation.
 *
 * Prints the input file and output file to stdout in the following format:
 * in  <filename> <file mode> <file length> <md5 hash>
 * out <filename> <file mode> <file length> <md5 hash>
 *
 * embedded chunk details:
 * chunk file offset: <offset of embedded chunk>
 * chunk data length: <length> (network byte order <length>)
 * chunk type: <type>
 * timestamp: <timestamp>
 * cyclic redundancy check: <32-bit CRC> (network byte order <32-bit CRC>)
 * */
static void print_summary(const char *original_file_path,
		const char *new_file_path, off_t chunk_file_offset)
{
	const char *filename_from = strrchr(original_file_path, '/');
	filename_from = !filename_from ? original_file_path : filename_from + 1;
	const char *filename_to = strrchr(new_file_path, '/');
	filename_to = !filename_to ? new_file_path : filename_to + 1;

	// compute table boundaries
	size_t filename_from_len = strlen(filename_from);
	size_t filename_to_len = strlen(filename_to);
	size_t max_filename_len = (filename_from_len >= filename_to_len) ? (filename_from_len) : (filename_to_len);

	// print input and output file details
	fprintf(stdout, "%-3s ", "in");
	print_file_summary(original_file_path, (int)(max_filename_len - filename_from_len + 1));

	fprintf(stdout, "%-3s ", "out");
	print_file_summary(new_file_path, (int)(max_filename_len - filename_to_len + 1));

	// print embedded chunk details
	int fd = open(new_file_path, O_RDONLY);
	if (fd < 0)
		FATAL(FILE_OPEN_FAILED, new_file_path);

	u_int32_t data_length;
	char type[CHUNK_TYPE_LENGTH];
	u_int64_t unix_time;
	u_int32_t crc;

	chunk_file_offset = lseek(fd, chunk_file_offset, SEEK_SET);
	if (recoverable_read(fd, &data_length, sizeof(u_int32_t)) != sizeof(u_int32_t))
		FATAL("failed to read chunk data length from file '%s'", new_file_path);
	if (recoverable_read(fd, &type, sizeof(char) * CHUNK_TYPE_LENGTH) != sizeof(char) * CHUNK_TYPE_LENGTH)
		FATAL("failed to read chunk type from file '%s'", new_file_path);
	if (recoverable_read(fd, &unix_time, sizeof(u_int64_t)) != sizeof(u_int64_t))
		FATAL("failed to read chunk timestamp from file '%s'", new_file_path);

	// seek to CRC field
	lseek(fd, chunk_file_offset + ntohl(data_length), SEEK_SET);
	if (recoverable_read(fd, &crc, sizeof(u_int32_t)) != sizeof(u_int32_t))
		FATAL("failed to read chunk CRC from file '%s'", new_file_path);

	unix_time = unix_time;
	struct tm *timeinfo = localtime((time_t *) &unix_time);

	fprintf(stdout, "\nembedded chunk details:\n");
	fprintf(stdout, "chunk file offset: %llu\n", (unsigned long long int)chunk_file_offset);
	fprintf(stdout, "chunk data length: %u (network byte order %#x)\n", ntohl(data_length), data_length);
	fprintf(stdout, "chunk type: %.*s\n", CHUNK_TYPE_LENGTH, type);
	fprintf(stdout, "timestamp: %s", asctime(timeinfo));
	fprintf(stdout, "cyclic redundancy check: %u (network byte order %#x)\n", ntohl(crc), crc);

	close(fd);
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
	fprintf(stdout, "%o %lld ", st.st_mode, (unsigned long long int)st.st_size);
	for (size_t i = 0; i < MD5_DIGEST_SIZE; i++)
		fprintf(stdout, "%02x", md5_hash[i]);
	fprintf(stdout, "\n");

	close(fd);
}

/**
 * Compute the md5 sum of an open file. The md5_hash argument must be an array of
 * length MD5_DIGEST_SIZE. The file offset is assumed to be positioned at
 * byte zero.
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
