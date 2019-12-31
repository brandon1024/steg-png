#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <string.h>
#include <ctype.h>

#include "strbuf.h"
#include "parse-options.h"
#include "png-chunk-processor.h"
#include "utils.h"
#include "zlib.h"

#define DEFLATE_STREAM_BUFFER_SIZE 16384

static int extract(const char *, const char *, int);
static void print_hex_dump(int fd);

int cmd_extract(int argc, char *argv[])
{
	const char *output_file = NULL;
	int hexdump = 0;
	int help = 0;

	const struct usage_string extract_cmd_usage[] = {
			USAGE("steg-png extract [-o | --output <file>] <file>"),
			USAGE("steg-png extract [--hexdump] <file>"),
			USAGE("steg-png extract (-h | --help)"),
			USAGE_END()
	};

	const struct command_option extract_cmd_options[] = {
			OPT_STRING('o', "output", "file", "alternate output file path", &output_file),
			OPT_LONG_BOOL("hexdump", "print a canonical hex+ASCII of the embedded data", &hexdump),
			OPT_BOOL('h', "help", "show help and exit", &help),
			OPT_END()
	};

	argc = parse_options(argc, argv, extract_cmd_options, 0, 1);
	if (help) {
		show_usage_with_options(extract_cmd_usage, extract_cmd_options, 0, NULL);
		return 0;
	}

	if (argc > 1) {
		show_usage_with_options(extract_cmd_usage, extract_cmd_options, 1, "unknown option '%s'", argv[0]);
		return 1;
	}

	if (argc < 1) {
		show_usage_with_options(extract_cmd_usage, extract_cmd_options, 1, "nothing to do");
		return 1;
	}

	return extract(argv[0], output_file, hexdump);
}

static int extract(const char *input_file, const char *output_file, int show_hexdump)
{
	struct strbuf output_file_path;
	strbuf_init(&output_file_path);
	if (output_file)
		strbuf_attach_str(&output_file_path, output_file);
	else
		strbuf_attach_fmt(&output_file_path, "%s.out", input_file);

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

	struct chunk_iterator_ctx ctx;
	int status = chunk_iterator_init_ctx(&ctx, in_fd);
	if (status < 0)
		FATAL("failed to read from file descriptor");
	else if(status > 0)
		DIE("input file is not a PNG (does not conform to RFC 2083)");

	// allocate buffers for inflate input/output
	unsigned char *input_buffer = (unsigned char *) malloc(sizeof(unsigned char) * DEFLATE_STREAM_BUFFER_SIZE);
	if (!input_buffer)
		FATAL(MEM_ALLOC_FAILED);

	unsigned char *output_buffer = (unsigned char *) malloc(sizeof(unsigned char) * DEFLATE_STREAM_BUFFER_SIZE);
	if (!output_buffer)
		FATAL(MEM_ALLOC_FAILED);

	// set up zlib for deflate
	struct z_stream_s strm;
	strm.zalloc = Z_NULL;
	strm.zfree = Z_NULL;
	strm.opaque = Z_NULL;
	strm.avail_in = 0;
	strm.next_in = Z_NULL;

	int ret = inflateInit(&strm);
	if (ret != Z_OK)
		FATAL("failed to initialize zlib for DEFLATE: %s", zError(ret));

	const char steg_chunk_type[] = {'s', 't', 'E', 'G' };
	int has_next_chunk, IEND_found = 0;
	while ((has_next_chunk = chunk_iterator_has_next(&ctx)) != 0) {
		if (has_next_chunk < 0)
			FATAL("unexpected error while parsing input file");

		if (chunk_iterator_next(&ctx) != 0)
			FATAL("unexpected error while parsing input file");

		if (IEND_found)
			DIE("non-compliant input file with IEND chunk defined twice (does not conform to RFC 2083)");

		/*
		 * If this chunk is our stEG chunk, inflate the data and write to the
		 * temporary file.
		 * */
		if (!memcmp(ctx.current_chunk.chunk_type, steg_chunk_type, CHUNK_TYPE_LENGTH)) {
			ssize_t bytes_read = 0;

			while ((bytes_read = chunk_iterator_read_data(&ctx, input_buffer, DEFLATE_STREAM_BUFFER_SIZE)) > 0) {
				strm.avail_in = bytes_read;
				strm.next_in = input_buffer;

				// fully consume the input buffer data
				do {
					strm.avail_out = DEFLATE_STREAM_BUFFER_SIZE;
					strm.next_out = output_buffer;

					ret = inflate(&strm, Z_NO_FLUSH);
					switch (ret) {
						case Z_STREAM_ERROR:
						case Z_NEED_DICT:
						case Z_DATA_ERROR:
						case Z_MEM_ERROR:
							FATAL("zlib INFLATE failed with unexpected error: %s", zError(ret));
						default:
							break;
					}

					size_t data_to_write = DEFLATE_STREAM_BUFFER_SIZE - strm.avail_out;
					if (recoverable_write(tmp_fd, output_buffer, data_to_write) != data_to_write)
						FATAL("failed to write inflated data to temporary file.");
				} while (strm.avail_out == 0);
			}
		}

		if (!memcmp(ctx.current_chunk.chunk_type, IEND_CHUNK_TYPE, CHUNK_TYPE_LENGTH))
			IEND_found = 1;
	}

	(void)inflateEnd(&strm);
	free(input_buffer);
	free(output_buffer);
	chunk_iterator_destroy_ctx(&ctx);

	if (!IEND_found)
		DIE("non-compliant input file with no IEND chunk defined (does not conform to RFC 2083)");

	// check that the input file actually contained embedded stEG chunks
	struct stat tmp_file_st;
	if (fstat(tmp_fd, &tmp_file_st) && errno == ENOENT)
		FATAL("failed to stat tmp file with descriptor %s'", tmp_fd);
	if (!tmp_file_st.st_size)
		DIE("input file is clean; embedded data could not be found.");

	if (show_hexdump) {
		off_t offset = lseek(tmp_fd, 0, SEEK_SET);
		if (offset < 0)
			return -1;

		print_hex_dump(tmp_fd);
	}

	// write file to final destination
	if (!show_hexdump || output_file) {
		off_t offset = lseek(tmp_fd, 0, SEEK_SET);
		if (offset < 0)
			return -1;

		struct stat input_file_st;
		if (lstat(input_file, &input_file_st) && errno == ENOENT)
			FATAL("failed to stat %s'", input_file);

		int out_fd = open(output_file_path.buff,  O_WRONLY | O_CREAT | O_TRUNC,
				input_file_st.st_mode & (S_IRWXU | S_IRWXG | S_IRWXO));
		if (out_fd < 0)
			DIE(FILE_OPEN_FAILED, output_file);

		if (copy_file_fd(out_fd, tmp_fd) != tmp_file_st.st_size)
			FATAL("Failed to write to file %s", output_file_path.buff);
	}

	close(in_fd);
	close(tmp_fd);
	strbuf_release(&output_file_path);

	return 0;
}

/**
 * Print a hexdump of a given open file. The hexdump is formatted similar to
 * the hexdump tool.
 * */
static void print_hex_dump(int fd)
{
	unsigned char buffer[4096];

	off_t file_offset = 0;
	ssize_t bytes_read = 0;
	while ((bytes_read = recoverable_read(fd, buffer, 4096)) > 0) {
		hex_dump(stdout, file_offset, buffer, bytes_read);
		file_offset += bytes_read;
	}
}
