#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#include <time.h>

#include "md5.h"
#include "strbuf.h"
#include "parse-options.h"
#include "png-chunk-processor.h"
#include "utils.h"
#include "zlib.h"

#define BUFF_LEN 1024
#define DEFLATE_CHUNK_DATA_LENGTH 8192
#define DEFLATE_STREAM_BUFFER_SIZE 16384

struct chunk_summary {
	size_t bytes_in;
	size_t bytes_out;
	double compression_ratio;
	int chunks_written;
};

static int compression_level = Z_DEFAULT_COMPRESSION;

static int embed(const char *, const char *, const char *, const char *, int,
		struct chunk_summary *);
static void print_summary(const char *, const char *, struct chunk_summary *);

int cmd_embed(int argc, char *argv[])
{
	const char *message = NULL;
	const char *output_file = NULL;
	const char *file_to_embed = NULL;
	int help = 0;
	int quiet = 0;

	const struct usage_string embed_cmd_usage[] = {
			USAGE("steg-png embed [options] (-m | --message <message>) <file>"),
			USAGE("steg-png embed [options] (-f | --file <file>) <file>"),
			USAGE("steg-png embed (-h | --help)"),
			USAGE_END()
	};

	const struct command_option embed_cmd_options[] = {
			OPT_STRING('m', "message", "message", "specify the message to embed in the png image", &message),
			OPT_STRING('f', "file", "file", "specify a file to embed in the png image", &file_to_embed),
			OPT_STRING('o', "output", "file", "output to a specific file", &output_file),
			OPT_INT('l', "compression-level", "alternate compression level (0 none, 1 fastest - 9 slowest)", &compression_level),
			OPT_BOOL('q', "quiet", "suppress informational summary to stdout", &quiet),
			OPT_BOOL('h', "help", "show help and exit", &help),
			OPT_END()
	};

	argc = parse_options(argc, argv, embed_cmd_options, 0, 0);
	if (help) {
		show_usage_with_options(embed_cmd_usage, embed_cmd_options, 0, NULL);
		return 0;
	}

	if (argc != 1) {
		show_usage_with_options(embed_cmd_usage, embed_cmd_options, 1, "too many options");
		return 1;
	}

	if (file_to_embed && message) {
		show_usage_with_options(embed_cmd_usage, embed_cmd_options, 1, "cannot mix --file and --message options");
		return 1;
	}

	if (compression_level != Z_DEFAULT_COMPRESSION && (compression_level > 9 || compression_level < 0)) {
		show_usage_with_options(embed_cmd_usage, embed_cmd_options, 1, "invalid compression level %d", compression_level);
		return 1;
	}

	struct strbuf output_file_path;
	int ret = 0;

	// if output file name not specified, generate one
	strbuf_init(&output_file_path);
	if (output_file)
		strbuf_attach_str(&output_file_path, output_file);
	else
		strbuf_attach_fmt(&output_file_path, "%s.steg", argv[0]);

	// embed the message/file in a chunk, and get a summary of the chunk that was embedded
	struct chunk_summary result = {
			.chunks_written = 0,
			.bytes_in = 0,
			.bytes_out = 0,
			.compression_ratio = 0
	};

	ret = embed(argv[0], output_file_path.buff, file_to_embed, message, compression_level, &result);

	if (!quiet)
		print_summary(argv[0], output_file_path.buff, &result);

	strbuf_release(&output_file_path);

	return ret;
}

static int embed_data(int, int, int, struct strbuf *, int, struct chunk_summary *);

/**
 * Embed a file or message into a PNG image with the file path `input_file`, and
 * write to the `output_file`.
 *
 * If file_to_embed is nonnull, the content of the file is embedded. Otherwise,
 * if message is nonnull, the message string is embedded. If both are null, the
 * message is read from stdin.
 *
 * Once complete, the chunk_summary is populated with the details of the embedded
 * chunk, which can be used to print diagnostic/informational messages.
 *
 * Note: to avoid leaving partially written or corrupted output files on error,
 * the output PNG is first written to a temporary file and then copied over to
 * its final location if successful.
 * */
static int embed(const char *input_file, const char *output_file,
		const char *file_to_embed, const char *message, int compression_level,
		struct chunk_summary *result)
{
	// stat and open descriptor to input file
	struct stat st;
	if (lstat(input_file, &st) && errno == ENOENT)
		FATAL("failed to stat %s'", input_file);

	int in_fd = open(input_file, O_RDONLY);
	if (in_fd < 0)
		FATAL(FILE_OPEN_FAILED, input_file);

	// create and unlink a temporary file
	char tmp_file_name_template[] = "/tmp/steg-png_XXXXXX";
	int tmp_fd = mkstemp(tmp_file_name_template);
	if (tmp_fd < 0)
		FATAL("unable to create temporary file");
	if (unlink(tmp_file_name_template) < 0)
		FATAL("failed to unlink temporary file from filesystem");

	if (file_to_embed) {
		// open descriptor to file that will be embedded
		int file_to_embed_fd = open(file_to_embed, O_RDONLY);
		if (file_to_embed_fd < 0)
			FATAL(FILE_OPEN_FAILED, file_to_embed);

		embed_data(in_fd, tmp_fd, file_to_embed_fd, NULL, compression_level, result);
		close(file_to_embed_fd);
	} else {
		// if no message was given, take from stdin
		struct strbuf message_buf;
		strbuf_init(&message_buf);
		if (!message) {
			char buffer[BUFF_LEN];
			ssize_t bytes_read = 0;
			while ((bytes_read = recoverable_read(STDIN_FILENO, buffer, BUFF_LEN)) > 0)
				strbuf_attach_bytes(&message_buf, buffer, bytes_read);

			if (bytes_read < 0)
				FATAL("unable to read message from stdin");
		} else {
			strbuf_attach_str(&message_buf, message);
		}

		embed_data(in_fd, tmp_fd, -1, &message_buf, compression_level, result);
		strbuf_release(&message_buf);
	}

	close(in_fd);

	if (lseek(tmp_fd, 0, SEEK_SET) < 0)
		FATAL("failed to set the file offset for temporary file");

	int out_fd = open(output_file, O_WRONLY | O_CREAT | O_TRUNC, st.st_mode);
	if (out_fd < 0)
		FATAL(FILE_OPEN_FAILED, output_file);

	fstat(tmp_fd, &st);
	ssize_t file_size = st.st_size;

	if (copy_file_fd(out_fd, tmp_fd) != file_size)
		FATAL("failed to write temporary file to destination %s", output_file);

	close(tmp_fd);
	close(out_fd);

	return 0;
}

/**
 * Write arbitrary data to a given file descriptor, and return an updated CRC.
 * */
static inline u_int32_t write_and_update_crc(int fd, const void *data, size_t length, u_int32_t crc)
{
	if (recoverable_write(fd, data, length) != length)
		FATAL("failed to write new chunk to output file descriptor %d", fd);

	return crc32_z(crc, data, length);
}

/**
 * Write the entire chunk from the current chunk_iterator context to a file
 * with the given file descriptor.
 * */
void write_chunk_to_file_from_ctx(int dest_fd, struct chunk_iterator_ctx *ctx)
{
	struct png_chunk_detail chunk = ctx->current_chunk;
	unsigned char temporary_buffer[BUFF_LEN];

	// write the chunk data length to output file
	u_int32_t data_len_net_order = htonl(chunk.data_length);
	if (recoverable_write(dest_fd, &data_len_net_order, sizeof(u_int32_t)) != sizeof(u_int32_t))
		FATAL("failed to write data length field to output file");

	// write the chunk type to output file
	u_int32_t chunk_crc = 0;
	chunk_crc = write_and_update_crc(dest_fd, chunk.chunk_type,
			sizeof(char) * CHUNK_TYPE_LENGTH, chunk_crc);

	// write the chunk data to output file
	while (1) {
		ssize_t bytes_read = chunk_iterator_read_data(ctx, temporary_buffer, BUFF_LEN);
		if (bytes_read < 0)
			FATAL("unexpected error while parsing input file");
		if (bytes_read == 0)
			break;

		chunk_crc = write_and_update_crc(dest_fd, temporary_buffer, bytes_read, chunk_crc);
	}

	if (chunk_crc != chunk.chunk_crc)
		WARN("%.*s chunk at file offset %d has invalid CRC -- file may be corrupted",
			 CHUNK_TYPE_LENGTH, chunk.chunk_type, ctx->chunk_file_offset);

	// write the chunk CRC to output file
	u_int32_t crc_net_order = htonl(chunk.chunk_crc);
	if (recoverable_write(dest_fd, &crc_net_order, sizeof(u_int32_t)) != sizeof(u_int32_t))
		FATAL("failed to write CRC field to output file");
}

void write_steg_chunk_to_file_from_buffer(int dest_fd, void *buffer, size_t length)
{
	u_int32_t crc = 0;

	// write length
	u_int32_t len_net_order = htonl((u_int32_t) length);
	if (recoverable_write(dest_fd, &len_net_order, sizeof(u_int32_t)) != sizeof(u_int32_t))
		FATAL("failed to write data length field to output file");

	// write type
	char chunk_type[] = { 's', 't', 'E', 'G' };
	crc = write_and_update_crc(dest_fd, chunk_type, CHUNK_TYPE_LENGTH, crc);

	// write data
	crc = write_and_update_crc(dest_fd, buffer, length, crc);

	// write crc
	u_int32_t crc_net_order = htonl(crc);
	if (recoverable_write(dest_fd, &crc_net_order, sizeof(u_int32_t)) != sizeof(u_int32_t))
		FATAL("failed to write CRC field to output file");
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
 * Returns the number of chunks created.
 * */
static int embed_data(int in_fd, int out_fd, int data_fd, struct strbuf *data,
		int compression_level, struct chunk_summary *result)
{
	if (data && data_fd != -1)
		BUG("either data or data_fd must be defined, not both");
	if (!data && data_fd == -1)
		BUG("either data or data_fd must be defined");

	struct chunk_iterator_ctx ctx;
	int status = chunk_iterator_init_ctx(&ctx, in_fd);
	if (status < 0)
		FATAL("failed to read from input file");
	else if(status > 0)
		DIE("input file is not a PNG (does not conform to RFC 2083)");

	// write PNG header signature to output file
	if (recoverable_write(out_fd, PNG_SIG, SIGNATURE_LENGTH) != SIGNATURE_LENGTH)
		FATAL("failed to write PNG file signature to output file");

	// allocate buffers for deflate input/output
	unsigned char *input_buffer = malloc(sizeof(unsigned char) * DEFLATE_STREAM_BUFFER_SIZE);
	if (!input_buffer)
		FATAL(MEM_ALLOC_FAILED);

	unsigned char *output_buffer = malloc(sizeof(unsigned char) * DEFLATE_STREAM_BUFFER_SIZE);
	if (!output_buffer)
		FATAL(MEM_ALLOC_FAILED);

	// set up zlib for deflate
	struct z_stream_s strm;
	strm.zalloc = Z_NULL;
	strm.zfree = Z_NULL;
	strm.opaque = Z_NULL;

	int ret, flush;
	ret = deflateInit(&strm, compression_level);
	if (ret != Z_OK)
		FATAL("failed to initialize zlib for DEFLATE: %s", zError(ret));

	int has_next_chunk, IEND_found = 0;
	while ((has_next_chunk = chunk_iterator_has_next(&ctx)) != 0) {
		if (has_next_chunk < 0)
			FATAL("unexpected error while parsing input file");

		if (chunk_iterator_next(&ctx) != 0)
			FATAL("unexpected error while parsing input file");

		// if the current chunk is the IEND chunk, then write our chunk
		if (!memcmp(ctx.current_chunk.chunk_type, IEND_CHUNK_TYPE, CHUNK_TYPE_LENGTH)) {
			if (IEND_found)
				DIE("non-compliant input file with IEND chunk defined twice (does not conform to RFC 2083)");
			IEND_found = 1;

			// deflate input file/buffer
			strm.avail_out = DEFLATE_STREAM_BUFFER_SIZE;
			strm.next_out = output_buffer;

			do {
				flush = Z_NO_FLUSH;

				/*
				 * Copy the input data (from given strbuf or file) into data
				 * buffer for zlib deflate.
				 * */
				if (data) {
					// if only a portion of chunk is remaining, use length of buffer
					size_t len = DEFLATE_STREAM_BUFFER_SIZE;
					if (data->len < DEFLATE_STREAM_BUFFER_SIZE) {
						len = data->len;
						flush = Z_FINISH;
					}

					// move from data buffer to input buffer
					memcpy(input_buffer, data->buff, len);
					strbuf_remove(data, 0, len);

					strm.avail_in = len;
				} else {
					ssize_t bytes_read = recoverable_read(data_fd, input_buffer, DEFLATE_STREAM_BUFFER_SIZE);
					if (bytes_read < 0)
						FATAL("failed to read from data input file");

					strm.avail_in = bytes_read;
					if (bytes_read < DEFLATE_STREAM_BUFFER_SIZE)
						flush = Z_FINISH;
				}

				strm.next_in = input_buffer;
				result->bytes_in += strm.avail_in;

				/*
				 * Run zlib deflate on input data while the output buffer is not full.
				 * If the output buffer is full, or we reached the end of the input data,
				 * the output buffer is written as stEG chunks to the output file in chunks
				 * of 8192 bytes in length.
				 * */
				unsigned pending = 0;
				do {
					ret = deflate(&strm, flush);
					if (ret == Z_STREAM_ERROR)
						FATAL("zlib DEFLATE failed with unexpected error: %s", zError(ret));

					// if we filled the output buffer or reached end of input data, write chunks
					if (strm.avail_out == 0 || flush == Z_FINISH) {
						size_t data_to_write = DEFLATE_STREAM_BUFFER_SIZE - strm.avail_out;

						do {
							size_t chunk_size = data_to_write > DEFLATE_CHUNK_DATA_LENGTH ? DEFLATE_CHUNK_DATA_LENGTH : data_to_write;

							write_steg_chunk_to_file_from_buffer(out_fd, output_buffer, chunk_size);
							result->bytes_out += chunk_size;
							result->chunks_written++;

							data_to_write -= chunk_size;

							// shift data in output buffer to beginning of buffer
							memmove(output_buffer, output_buffer + chunk_size, DEFLATE_STREAM_BUFFER_SIZE - chunk_size);

							// update zlib stream state
							strm.avail_out = DEFLATE_STREAM_BUFFER_SIZE - data_to_write;
							strm.next_out = output_buffer + data_to_write;
						} while (data_to_write >= DEFLATE_CHUNK_DATA_LENGTH || (data_to_write > 0 && flush == Z_FINISH));
					}

					/*
					 * Even though we may have fully consumed the output buffer, zlib may still have some pending
					 * data that it is waiting to write to the output buffer. If so (determined by deflatePending),
					 * run deflate again to write it to the output buffer.
					 * */
					ret = deflatePending(&strm, &pending, Z_NULL);
					if (ret != Z_OK)
						FATAL("zlib DEFLATE failed with unexpected error: %s", zError(ret));
				} while (strm.avail_out == 0 || pending);
			} while (flush != Z_FINISH);
		}

		write_chunk_to_file_from_ctx(out_fd, &ctx);
	}

	(void)deflateEnd(&strm);
	free(input_buffer);
	free(output_buffer);

	result->compression_ratio = result->bytes_out == 0 ? 0.0 : (float)result->bytes_out / (float)result->bytes_in;

	if (!IEND_found)
		DIE("non-compliant input file no IEND chunk defined (does not conform to RFC 2083)");

	chunk_iterator_destroy_ctx(&ctx);
	return 0;
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

	// print input file summary
	if (lstat(file_path, &st) && errno == ENOENT)
		FATAL("failed to stat %s'", file_path);

	fd = open(file_path, O_RDONLY);
	if (fd < 0)
		FATAL(FILE_OPEN_FAILED, file_path);

	if (compute_md5_sum(fd, md5_hash))
		FATAL("failed to compute md5 hash of file '%s'", file_path);

	const char *filename = strrchr(file_path, '/');
	filename = !filename ? file_path : filename + 1;

	fprintf(stdout, "%s %*s", filename, filename_table_len, " ");
	fprintf(stdout, "%o %lld ", st.st_mode, (unsigned long long int)st.st_size);
	for (size_t i = 0; i < MD5_DIGEST_SIZE; i++)
		fprintf(stdout, "%02x", md5_hash[i]);
	fprintf(stdout, "\n");

	close(fd);
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
		const char *new_file_path, struct chunk_summary *result)
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
	printf("%-3s ", "in");
	print_file_summary(original_file_path, (int)(max_filename_len - filename_from_len + 1));

	printf("%-3s ", "out");
	print_file_summary(new_file_path, (int)(max_filename_len - filename_to_len + 1));

	printf("\nsummary:\n");
	printf("compression factor: %.2f (%lu in, %lu out)\n",
			result->compression_ratio, result->bytes_in, result->bytes_out);
	printf("chunks embedded in file: %d\n",
			result->chunks_written);
}
