#ifndef STEG_PNG_PNG_CHUNK_PROCESSOR_H
#define STEG_PNG_PNG_CHUNK_PROCESSOR_H

#include <stdlib.h>

#include "strbuf.h"

#define SIGNATURE_LENGTH 8
#define CHUNK_TYPE_LENGTH 4

extern unsigned char PNG_SIG[];
extern char IEND_CHUNK_TYPE[];

struct chunk_iterator_ctx {
	int fd;
	size_t pos;
};

/**
 * Initialize a chunk_iterator_ctx. The file descriptor must be a valid open
 * file descriptor.
 *
 * Reads the the first eight bytes from the file and verifies that the file
 * is a valid PNG file.
 *
 * Returns -1 if unable to read from file, returns 1 if the file signature is
 * invalid, and returns 0 if the context was initialized successfully.
 * */
int chunk_iterator_init_ctx(struct chunk_iterator_ctx *ctx, int fd);

/**
 * Read the next chunk into the given buffer.
 *
 * Returns 1 if result has been updated with the contents of the next chunk, or
 * zero if no more chunks exist. Returns -1 if a chunk could not be read successfully,
 * or is corrupt.
 * */
int chunk_iterator_next_chunk(struct chunk_iterator_ctx *ctx, struct strbuf *result);

/**
 * Destroy a chunk_iterator_ctx.
 * */
void chunk_iterator_destroy_ctx(struct chunk_iterator_ctx *ctx);

/**
 * Parse a PNG file chunk for the data length field.
 *
 * Returns zero if the length was parsed successfully, and 1 otherwise.
 * */
int png_parse_chunk_data_length(const struct strbuf *chunk, u_int32_t *len);

/**
 * Parse a PNG file chunk for the chunk type field. The given `type` must have
 * a length of CHUNK_TYPE_LENGTH.
 *
 * Returns zero of the type was parsed successfully, and 1 otherwise.
 * */
int png_parse_chunk_type(const struct strbuf *chunk, char type[]);

/**
 * Parse a PNG file chunk for the CRC field.
 *
 * Returns zero if the CRC was parsed successfully, and 1 otherwise.
 * */
int png_parse_chunk_crc(const struct strbuf *chunk, u_int32_t *crc);

#endif //STEG_PNG_PNG_CHUNK_PROCESSOR_H
