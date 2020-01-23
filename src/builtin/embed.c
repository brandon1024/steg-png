#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <limits.h>
#include <libgen.h>

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
	unsigned chunks_written;
};

static int compression_level = Z_DEFAULT_COMPRESSION;

static int embed(const char *, const char *, const char *, const char *,
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
			USAGE("steg-png embed [options] (-m | --message <message>) [(-q | --quiet)] <file>"),
			USAGE("steg-png embed [options] (-f | --file <file>) [(-q | --quiet)] <file>"),
			USAGE("steg-png embed (-h | --help)"),
			USAGE_END()
	};

	const struct command_option embed_cmd_options[] = {
			OPT_STRING('m', "message", "message", "specify the message to embed in the png image", &message),
			OPT_STRING('f', "file", "file", "specify a file to embed in the png image", &file_to_embed),
			OPT_STRING('o', "output", "file", "output to a specific file", &output_file),
			OPT_INT('l', "compression-level", "alternate compression level (0 none, 1 fastest - 9 slowest, default 6)", &compression_level),
			OPT_BOOL('q', "quiet", "suppress informational summary to stdout", &quiet),
			OPT_BOOL('h', "help", "show help and exit", &help),
			OPT_END()
	};

	argc = parse_options(argc, argv, embed_cmd_options, 0, 1);
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
		show_usage_with_options(embed_cmd_usage, embed_cmd_options, 1, "cannot mix --file and --message options");
		return 1;
	}

	if (compression_level != Z_DEFAULT_COMPRESSION && (compression_level > 9 || compression_level < 0)) {
		show_usage_with_options(embed_cmd_usage, embed_cmd_options, 1, "invalid compression level %d", compression_level);
		return 1;
	}

	if (compression_level == 0)
		WARN("using a compression level of zero is discouraged, since the embedded message\n"
			"or file will not be sufficiently obfuscated. Consider increasing the compression level\n"
			"or encrypting your input message or file.");

	struct strbuf output_file_path;
	int ret = 0;

	// if output file name not specified, generate one
	strbuf_init(&output_file_path);
	if (output_file)
		strbuf_attach_str(&output_file_path, output_file);
	else
		strbuf_attach_fmt(&output_file_path, "%s.steg", basename(argv[0]));

	// embed the message/file in a chunk, and get a summary of the chunk that was embedded
	struct chunk_summary result = {
			.chunks_written = 0,
			.bytes_in = 0,
			.bytes_out = 0,
			.compression_ratio = 0
	};

	ret = embed(argv[0], output_file_path.buff, file_to_embed, message, &result);

	if (!quiet)
		print_summary(argv[0], output_file_path.buff, &result);

	strbuf_release(&output_file_path);

	return ret;
}

static int embed_data(int, int, int, struct strbuf *, struct chunk_summary *);

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
		const char *file_to_embed, const char *message, struct chunk_summary *result)
{
	// stat and open descriptor to input file
	struct stat st;
	if (lstat(input_file, &st) && errno == ENOENT)
		FATAL("failed to stat %s'", input_file);

	int in_fd = open(input_file, O_RDONLY);
	if (in_fd < 0)
		DIE(FILE_OPEN_FAILED, input_file);

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
			DIE(FILE_OPEN_FAILED, file_to_embed);

		embed_data(in_fd, tmp_fd, file_to_embed_fd, NULL, result);
		close(file_to_embed_fd);
	} else if (!message) {
		// if no message was given, take from stdin
		char tmp_input_file_name_template[] = "/tmp/steg-png_XXXXXX";
		int tmp_in_fd = mkstemp(tmp_input_file_name_template);
		if (tmp_in_fd < 0)
			FATAL("unable to create temporary file");
		if (unlink(tmp_input_file_name_template) < 0)
			FATAL("failed to unlink temporary file from filesystem");

		char buffer[BUFF_LEN];
		ssize_t bytes_read = 0;
		while ((bytes_read = recoverable_read(STDIN_FILENO, buffer, BUFF_LEN)) > 0)
			if (recoverable_write(tmp_in_fd, buffer, bytes_read) != bytes_read)
				FATAL("failed to write to temporary outfile file");

		if (bytes_read < 0)
			FATAL("unable to read message from stdin");

		if (lseek(tmp_fd, 0, SEEK_SET) < 0)
			FATAL("failed to set the file offset for temporary file");

		embed_data(in_fd, tmp_fd, tmp_in_fd, NULL, result);
		close(tmp_in_fd);
	} else {
		struct strbuf message_buf;
		strbuf_init(&message_buf);

		strbuf_attach_str(&message_buf, message);

		embed_data(in_fd, tmp_fd, -1, &message_buf, result);
		strbuf_release(&message_buf);
	}

	close(in_fd);

	if (lseek(tmp_fd, 0, SEEK_SET) < 0)
		FATAL("failed to set the file offset for temporary file");

	int out_fd = open(output_file, O_WRONLY | O_CREAT | O_TRUNC, st.st_mode);
	if (out_fd < 0)
		DIE(FILE_OPEN_FAILED, output_file);

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
 * Compute the sparcity constant used to determine how sparsely the embedded chunks will be
 * distributed in the file.
 *
 * This constant is a rough estimate; it assumes that each chunk has a length of 8192 bytes,
 * but it's close enough for most situations.
 *
 * A value of 0 indicates that embedded chunks should not be sparse, (i.e. localized).
 * */
static inline unsigned int compute_sparcity(off_t source_file_len, off_t data_len)
{
	double data_length_factor = (unsigned int)((source_file_len / data_len) % UINT_MAX);
	return (unsigned)(data_length_factor / DEFLATE_CHUNK_DATA_LENGTH);
}

static size_t single_pass_deflate(struct z_stream_s *, unsigned char *, int, int);

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
		struct chunk_summary *result)
{
	if (data && data_fd != -1)
		BUG("either data or data_fd must be defined, not both");
	if (!data && data_fd == -1)
		BUG("either data or data_fd must be defined");

	struct chunk_iterator_ctx ctx;
	int status = chunk_iterator_init_ctx(&ctx, in_fd);
	if (status < 0)
		FATAL("failed to read from file descriptor");
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

	int ret, flush = Z_NO_FLUSH;
	ret = deflateInit(&strm, compression_level);
	if (ret != Z_OK)
		FATAL("failed to initialize zlib for DEFLATE: %s", zError(ret));

	struct stat st;
	if (fstat(in_fd, &st) && errno == ENOENT)
		FATAL("failed to stat tmp file with descriptor %s'", in_fd);

	// compute the sparcity
	unsigned int sparcity;
	if (data) {
		sparcity = compute_sparcity(st.st_size, data->len);
	} else {
		struct stat data_st;
		if (fstat(data_fd, &data_st) && errno == ENOENT)
			FATAL("failed to stat tmp file with descriptor %s'", in_fd);

		sparcity = compute_sparcity(st.st_size, data_st.st_size);
	}

	struct timeval time;
	if (gettimeofday(&time, NULL))
		FATAL("unable to seed PRNG; gettimeofday failed unexpectedly");

	srandom((unsigned)time.tv_sec ^ (unsigned)time.tv_usec);

	int has_next_chunk, IEND_found = 0, IHDR_found = 0;
	while ((has_next_chunk = chunk_iterator_has_next(&ctx)) != 0) {
		if (has_next_chunk < 0)
			DIE("unable to parse input file: file does not appear to represent a valid PNG file, or may be corrupted.");

		if (chunk_iterator_next(&ctx) != 0)
			FATAL("unable to advance png chunk iterator: inconsistent state, possibly corrupted file.");

		// if the current chunk is the IEND chunk, then write our chunk
		if (!memcmp(ctx.current_chunk.chunk_type, IEND_CHUNK_TYPE, CHUNK_TYPE_LENGTH))
			IEND_found++;

		// deflate input file/buffer
		strm.avail_out = DEFLATE_STREAM_BUFFER_SIZE;
		strm.next_out = output_buffer;
		while (flush != Z_FINISH) {
			if (!IHDR_found || (!IEND_found && sparcity && (random() % sparcity != 0)))
				break;

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

			// run a single pass of DEFLATE, flushing if necessary
			size_t bytes = single_pass_deflate(&strm, output_buffer, out_fd, flush);

			// multiples of 8192, plus one if reached end of file and last chunk size less than 8192
			unsigned chunks_written = (unsigned)(bytes / DEFLATE_CHUNK_DATA_LENGTH);
			if (bytes % DEFLATE_CHUNK_DATA_LENGTH != 0)
				chunks_written++;

			result->chunks_written += chunks_written;
			result->bytes_out += bytes;
		}

		write_chunk_to_file_from_ctx(out_fd, &ctx);

		if (!memcmp(ctx.current_chunk.chunk_type, IHDR_CHUNK_TYPE, CHUNK_TYPE_LENGTH))
			IHDR_found++;
	}

	(void)deflateEnd(&strm);
	free(input_buffer);
	free(output_buffer);

	result->compression_ratio = result->bytes_out == 0 ? 0.0 : (float)result->bytes_out / (float)result->bytes_in;

	if (IHDR_found != 1)
		DIE("non-compliant input file; IHDR chunk defined %d times (does not conform to RFC 2083)", IHDR_found);
	if (IEND_found != 1)
		DIE("non-compliant input file; IEND chunk defined %d times (does not conform to RFC 2083)", IEND_found);

	chunk_iterator_destroy_ctx(&ctx);
	return 0;
}

/**
 * Run zlib deflate on input data while the output buffer is not full.
 * If the output buffer is full, or we reached the end of the input data,
 * the output buffer is written as stEG chunks to the output file in chunks
 * of 8192 bytes in length.
 *
 * Returns the number of (defalted) bytes written to the file. A return value of
 * zero indicates that the output buffer was not filled, and as a result no data
 * was written to the file.
 * */
static size_t single_pass_deflate(struct z_stream_s *strm, unsigned char *output_buffer,
		int out_fd, int flush)
{
	int ret;
	unsigned pending = 0;

	size_t bytes_out = 0;
	size_t data_to_write, chunk_size;
	do {
		ret = deflate(strm, flush);
		if (ret == Z_STREAM_ERROR)
			FATAL("zlib DEFLATE failed with unexpected error: input file may be corrupted.\n"
				  "zlib: %s", zError(ret));

		// if we filled the output buffer or reached end of input data, write chunks
		if (strm->avail_out == 0 || flush == Z_FINISH) {
			data_to_write = DEFLATE_STREAM_BUFFER_SIZE - strm->avail_out;

			do {
				// chunk size is minimum of DEFLATE_CHUNK_DATA_LENGTH and data_to_write
				chunk_size = data_to_write > DEFLATE_CHUNK_DATA_LENGTH ? DEFLATE_CHUNK_DATA_LENGTH : data_to_write;

				write_steg_chunk_to_file_from_buffer(out_fd, output_buffer, chunk_size);
				bytes_out += chunk_size;

				data_to_write -= chunk_size;

				// shift data in output buffer to beginning of buffer
				memmove(output_buffer, output_buffer + chunk_size, DEFLATE_STREAM_BUFFER_SIZE - chunk_size);

				// update zlib stream state
				strm->avail_out = DEFLATE_STREAM_BUFFER_SIZE - data_to_write;
				strm->next_out = output_buffer + data_to_write;
			} while (data_to_write >= DEFLATE_CHUNK_DATA_LENGTH || (data_to_write > 0 && flush == Z_FINISH));
		}

		/*
		 * Even though we may have fully consumed the output buffer, zlib may still have some pending
		 * data that it is waiting to write to the output buffer. If so (determined by deflatePending),
		 * run deflate again to write it to the output buffer.
		 * */
		ret = deflatePending(strm, &pending, Z_NULL);
		if (ret != Z_OK)
			FATAL("zlib DEFLATE failed with unexpected error: input file may be corrupted.\n"
				  "zlib: %s", zError(ret));
	} while (strm->avail_out == 0 || pending);

	return bytes_out;
}

/**
 * Print a summary of a embedded chunk operation.
 *
 * Prints the input file and output file to stdout in the following format:
 * in  <filename> <file mode> <file length> <md5 hash>
 * out <filename> <file mode> <file length> <md5 hash>
 *
 * summary:
 * compression factor: x.xx (xxxx in, xxxx out)
 * chunks embedded in file: xxx
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
	printf("chunks embedded in file: %u\n",
			result->chunks_written);
}
