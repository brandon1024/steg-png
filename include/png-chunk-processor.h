#ifndef STEG_PNG_PNG_CHUNK_PROCESSOR_H
#define STEG_PNG_PNG_CHUNK_PROCESSOR_H

#include <stdlib.h>
#include <sys/types.h>

/**
 * png-chunk-processor usage:
 *
 * The png-chunk-processor API can be used to process a PNG image, allowing the
 * caller to iterate over all the chunks in a PNG file and incrementally
 * stream the individual file chunk data portions.
 *
 * Example Usage:
 * void example() {
 * 		int fd = open(file_path, O_RDONLY);
 * 		struct chunk_iterator_ctx ctx;
 * 		if (chunk_iterator_init_ctx(&ctx, fd) != 0)
 * 			DIE("could not initialize chunk iterator context");
 *
 * 		do {
 * 			int has_next = chunk_iterator_has_next(&ctx);
 * 			if (has_next < 0)
 * 				DIE("corrupted file or context");
 * 			if (!has_next)
 * 				break;
 *
 * 			if (chunk_iterator_next(&ctx) != 0)
 * 				DIE("corrupted file or context");
 *
 * 			unsigned char data[1024];
 * 			while (1) {
 * 				ssize_t bytes_read = chunk_iterator_read_data(&ctx, data, 1024);
 *				if (bytes_read < 0)
 *					break;
 *
 *				// process data
 * 			}
 * 		} while (1);
 *
 * 		chunk_iterator_destroy_ctx(&ctx);
 * 		close(fd);
 * }
 *
 * */

#define SIGNATURE_LENGTH 8
#define CHUNK_TYPE_LENGTH 4

extern unsigned char PNG_SIG[];

extern const char IHDR_CHUNK_TYPE[];
extern const char PLTE_CHUNK_TYPE[];
extern const char IDAT_CHUNK_TYPE[];
extern const char IEND_CHUNK_TYPE[];

struct png_chunk_detail {
	char chunk_type[CHUNK_TYPE_LENGTH];
	u_int32_t data_length;
	u_int32_t chunk_crc;
};

struct chunk_iterator_ctx {
	int fd;
	unsigned int initialized: 1;
	unsigned int read_full: 1;
	off_t chunk_file_offset;
	struct png_chunk_detail current_chunk;
};

/**
 * Initialize a chunk_iterator_ctx. The file descriptor must be a valid open
 * file descriptor, and cannot be a socket, pipe or FIFO.
 *
 * Reads the the first eight bytes from the file and verifies that the file
 * is a valid PNG file.
 *
 * Returns -1 if unable to read from file, returns 1 if the file signature is
 * invalid, and returns 0 if the context was initialized successfully.
 * */
int chunk_iterator_init_ctx(struct chunk_iterator_ctx *ctx, int fd);

/**
 * Determine whether there are any file chunks left to be processed by this chunk
 * iterator, without advancing the iterator.
 *
 * Returns 0 if there are no more chunks to be processed, and 1 if more chunks
 * remain. If an unexpected error occurs, -1 is returned and this chunk iterator
 * is no longer reliable.
 * */
int chunk_iterator_has_next(struct chunk_iterator_ctx *ctx);

/**
 * Advance the chunk iterator to the next chunk. Once invoked, the internal
 * context state is updated with the details of the new chunk.
 *
 * Note that the caller is responsible for ensuring that the chunk is not
 * corrupted (valid CRC).
 *
 * Returns 0 if the internal state was updated successfully. Returns -1 if the
 * state could not be updated for any reason (possibly corrupted file).
 * */
int chunk_iterator_next(struct chunk_iterator_ctx *ctx);

/**
 * Read data from the current chunk into the given buffer.
 *
 * This function has two modes of operation. If `read_full` is enabled on the
 * context, reads will include header/control bytes and data. If disabled,
 * only data from the chunks data segment are read.
 *
 * At most 'length' bytes are read from the chunk. If all bytes has been read
 * for the current chunk, returns zero and the buffer is left unchanged.
 * Otherwise returns the number of bytes read into the buffer. This function
 * may also return -1 if the context has an inconsistent state.
 * */
ssize_t chunk_iterator_read_data(struct chunk_iterator_ctx *ctx, unsigned char *buffer, size_t length);

/**
 * Get the length of the data for the current chunk, in host byte order.
 * The length is written to 'len'.
 *
 * Returns zero if successful, and non-zero otherwise.
 * */
int chunk_iterator_get_chunk_data_length(struct chunk_iterator_ctx *ctx, u_int32_t *len);

/**
 * Get the type of the current chunk. The given `type` must have a length of
 * CHUNK_TYPE_LENGTH.
 *
 * Returns zero if successful, and non-zero otherwise.
 * */
int chunk_iterator_get_chunk_type(struct chunk_iterator_ctx *ctx, char type[]);

/**
 * Get the CRC for the current chunk, in host byte order.
 *
 * Returns zero if successful, and non-zero otherwise.
 * */
int chunk_iterator_get_chunk_crc(struct chunk_iterator_ctx *ctx, u_int32_t *crc);

/**
 * Return 1 if the current chunk is a 'critical' chunk, and zero if the chunk
 * is ancillary.
 *
 * Returns -1 if chunk_iterator_next() has not yet been called, or the iterator has
 * reached the end of the file.
 * */
int chunk_iterator_is_critical(struct chunk_iterator_ctx *ctx);

/**
 * Return 1 if the current chunk is a 'ancillary' chunk, and zero if the chunk
 * is critical.
 *
 * Returns -1 if chunk_iterator_next() has not yet been called, or the iterator has
 * reached the end of the file.
 * */
int chunk_iterator_is_ancillary(struct chunk_iterator_ctx *ctx);

/**
 * Destroy a chunk_iterator_ctx.
 * */
void chunk_iterator_destroy_ctx(struct chunk_iterator_ctx *ctx);

#endif //STEG_PNG_PNG_CHUNK_PROCESSOR_H
