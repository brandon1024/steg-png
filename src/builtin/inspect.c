#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <arpa/inet.h>

#include "parse-options.h"
#include "str-array.h"
#include "png-chunk-processor.h"
#include "utils.h"
#include "builtin.h"

static int print_png_summary(const char *, struct str_array *, int, int, int);

int cmd_inspect(int argc, char *argv[])
{
	int interactive = 0;
	int hexdump = 0;
	int ancillary = 0, critical = 0;
	int help = 0;

	struct str_array filter_list;
	str_array_init(&filter_list);

	const struct usage_string inspect_cmd_usage[] = {
			USAGE("steg-png inspect [(--filter <chunk type>)...] [--critical] [--ancillary] [--hexdump] <file>"),
			USAGE("steg-png inspect (-i | --interactive) <file>"),
			USAGE("steg-png inspect (-h | --help)"),
			USAGE_END()
	};

	const struct command_option inspect_cmd_options[] = {
			OPT_BOOL('i', "interactive", "display each chunk, interactively", &interactive),
			OPT_LONG_BOOL("hexdump", "print a canonical hex+ASCII hexdump of the embedded data", &hexdump),
			OPT_LONG_STRING_LIST("filter", "chunk type", "show chunks with specific type", &filter_list),
			OPT_LONG_BOOL("critical", "show critical chunks", &critical),
			OPT_LONG_BOOL("ancillary", "show ancillary chunks", &ancillary),
			OPT_BOOL('h', "help", "show help and exit", &help),
			OPT_END()
	};

	argc = parse_options(argc, argv, inspect_cmd_options, 0, 1);
	if (help) {
		show_usage_with_options(inspect_cmd_usage, inspect_cmd_options, 0, NULL);
		str_array_release(&filter_list);
		return 0;
	}

	if (argc > 1) {
		show_usage_with_options(inspect_cmd_usage, inspect_cmd_options, 1, "unknown option '%s'", argv[0]);
		str_array_release(&filter_list);
		return 1;
	}

	if (argc < 1) {
		show_usage_with_options(inspect_cmd_usage, inspect_cmd_options, 1, "nothing to do");
		str_array_release(&filter_list);
		return 1;
	}

	if (interactive) {
		str_array_release(&filter_list);
		return cmd_inspect_interactive(argc, argv);
	}

	int ret = print_png_summary(argv[0], &filter_list, hexdump, critical, ancillary);
	str_array_release(&filter_list);

	return ret;
}

static void get_chunk_types(int, struct str_array *);
static void print_filter_summary(struct str_array *, int, int);

/**
 * png file summary:
 * <filename> <file mode> <file length> <md5 hash>
 * chunks: <>, ...
 *
 * showing chunks that [have the type (...)], and [are critical] [or ancillary]
 *
 * chunk type: <string> [(steg-png recognized)]
 * chunk offset: <number>
 * chunk data length: <number>
 * chunk cyclic redundancy check: <number> (valid)
 * [optional] data:
 * <hexdump>
 *
 * ...
 * */
static int print_png_summary(const char *file_path, struct str_array *types,
		int hexdump, int show_critical, int show_ancillary)
{
	fprintf(stdout, "png file summary:\n");
	print_file_summary(file_path, 0);

	int fd = open(file_path, O_RDONLY);
	if (fd < 0)
		DIE(FILE_OPEN_FAILED, file_path);

	struct str_array chunks;
	str_array_init(&chunks);
	chunks.free_data = 1;

	get_chunk_types(fd, &chunks);

	fprintf(stdout, "chunks: ");
	for (size_t i = 0; i < chunks.len; i++) {
		struct str_array_entry *entry = str_array_get_entry(&chunks, i);
		int *data = (int *)entry->data;

		fprintf(stdout, "%4s (%d)", entry->string, *data);

		if (i != chunks.len - 1)
			fprintf(stdout, ", ");
		else
			fprintf(stdout, "\n");
		if (i && i % 8 == 0)
			fprintf(stdout, "\n");
	}

	fprintf(stdout, "\n");
	print_filter_summary(types, show_critical, show_ancillary);
	fprintf(stdout, "\n");

	if (lseek(fd, 0, SEEK_SET) < 0)
		FATAL("failed to set the file offset for temporary file");

	struct chunk_iterator_ctx ctx;
	int ret = chunk_iterator_init_ctx(&ctx, fd);
	if (ret < 0)
		FATAL("failed to read from file descriptor");
	else if(ret > 0)
		DIE("input file is not a PNG (does not conform to RFC 2083)");

	int has_next_chunk;
	while ((has_next_chunk = chunk_iterator_has_next(&ctx)) != 0) {
		if (has_next_chunk < 0)
			DIE("unable to parse input file: file does not appear to represent a valid PNG file, or may be corrupted.");

		if (chunk_iterator_next(&ctx) != 0)
			FATAL("unable to advance png chunk iterator: inconsistent state, possibly corrupted file.");

		char type[CHUNK_TYPE_LENGTH + 1] = {0};
		chunk_iterator_get_chunk_type(&ctx, type);
		type[CHUNK_TYPE_LENGTH] = 0;

		int filtered = types->len > 0 ? 1 : 0;
		for (size_t i = 0; i < types->len; i++) {
			if (!strcmp(type, str_array_get(types, i)))
				filtered = 0;
		}

		if (show_ancillary || show_critical) {
			if (!show_ancillary && chunk_iterator_is_ancillary(&ctx))
				filtered = 1;
			if (!show_critical && chunk_iterator_is_critical(&ctx))
				filtered = 1;
		}

		if (filtered)
			continue;

		u_int32_t len = 0;
		chunk_iterator_get_chunk_data_length(&ctx, &len);

		u_int32_t crc = 0;
		chunk_iterator_get_chunk_crc(&ctx, &crc);

		fprintf(stdout, "chunk type: %4s\n", type);
		fprintf(stdout, "file offset: %lld\n", (long long int)ctx.chunk_file_offset);
		fprintf(stdout, "data length: %u\n", len);
		fprintf(stdout, "cyclic redundancy check: %u (network byte order %#x)\n", crc, htonl(crc));

		if (hexdump) {
			fprintf(stdout, "data:\n");

			unsigned char buffer[4096];
			ssize_t bytes_read = 0;
			off_t offset = 0;
			while ((bytes_read = chunk_iterator_read_data(&ctx, buffer, 4096)) > 0) {
				hex_dump(stdout, offset, buffer, bytes_read);
				offset += bytes_read;
			}
		}

		fprintf(stdout, "\n");
	}

	chunk_iterator_destroy_ctx(&ctx);

	close(fd);

	str_array_release(&chunks);
	return 0;
}

static void get_chunk_types(int fd, struct str_array *types)
{
	struct chunk_iterator_ctx ctx;
	int ret = chunk_iterator_init_ctx(&ctx, fd);
	if (ret < 0)
		FATAL("failed to read from file descriptor");
	else if(ret > 0)
		DIE("input file is not a PNG (does not conform to RFC 2083)");

	char type[CHUNK_TYPE_LENGTH + 1] = {0};
	int has_next_chunk;
	while ((has_next_chunk = chunk_iterator_has_next(&ctx)) != 0) {
		if (has_next_chunk < 0)
			DIE("unable to parse input file: file does not appear to represent a valid PNG file, or may be corrupted.");

		if (chunk_iterator_next(&ctx) != 0)
			FATAL("unable to advance png chunk iterator: inconsistent state, possibly corrupted file.");

		chunk_iterator_get_chunk_type(&ctx, type);
		type[CHUNK_TYPE_LENGTH] = 0;

		int found = 0;
		for (size_t i = 0; i < types->len; i++) {
			struct str_array_entry *entry = str_array_get_entry(types, i);
			int *data = (int *)entry->data;

			if (!strcmp(type, entry->string)) {
				*data = *data + 1;
				found++;
				break;
			}
		}

		if (!found) {
			struct str_array_entry *entry = str_array_insert(types, type, types->len);
			entry->data = malloc(sizeof(int));
			if (!entry->data)
				FATAL(MEM_ALLOC_FAILED);

			*((int *)entry->data) = 1;
		}
	}

	chunk_iterator_destroy_ctx(&ctx);
}

static void print_filter_summary(struct str_array *types, int show_critical, int show_ancillary)
{
	fprintf(stdout, "Showing all chunks");
	if (types->len) {
		fprintf(stdout, " that have the type (");
		for (size_t i = 0; i < types->len; i++)
			fprintf(stdout, "%4s%s", str_array_get(types, i), (i != types->len - 1) ? ", " : "");

		fprintf(stdout, ")");
	}

	if (show_critical && show_ancillary)
		fprintf(stdout, " that are critical or ancillary");
	else if (show_critical)
		fprintf(stdout, " that are critical");
	else if (show_ancillary)
		fprintf(stdout, " that are ancillary");

	fprintf(stdout, ":\n");
}
