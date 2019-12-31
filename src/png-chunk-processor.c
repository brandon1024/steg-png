#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <arpa/inet.h>

#include "png-chunk-processor.h"
#include "utils.h"

unsigned char PNG_SIG[] = {
		0x89, 0x50, 0x4e, 0x47,
		0x0d, 0x0a, 0x1a, 0x0a
};

const char IHDR_CHUNK_TYPE[] = {'I', 'H', 'D', 'R'};
const char PLTE_CHUNK_TYPE[] = {'P', 'L', 'T', 'E'};
const char IDAT_CHUNK_TYPE[] = {'I', 'D', 'A', 'T'};
const char IEND_CHUNK_TYPE[] = {'I', 'E', 'N', 'D'};

static int construct_png_chunk_detail(int, struct png_chunk_detail *);

int chunk_iterator_init_ctx(struct chunk_iterator_ctx *ctx, int fd)
{
	off_t offset = lseek(fd, 0, SEEK_SET);
	if (offset < 0)
		return -1;

	unsigned char signature[SIGNATURE_LENGTH];
	if (recoverable_read(fd, signature, SIGNATURE_LENGTH) != SIGNATURE_LENGTH)
		return -1;

	if (memcmp(PNG_SIG, signature, SIGNATURE_LENGTH * sizeof(unsigned char)) != 0)
		return 1;

	ctx->fd = fd;
	ctx->initialized = 0;
	ctx->current_chunk = (struct png_chunk_detail) {
		.chunk_type = {0, 0, 0, 0},
		.data_length = 0,
		.chunk_crc = 0
	};

	return 0;
}

int chunk_iterator_has_next(struct chunk_iterator_ctx *ctx)
{
	// get the current file offset
	off_t file_offset = lseek(ctx->fd, 0, SEEK_CUR);
	if (file_offset < 0)
		return -1;
	if (file_offset < SIGNATURE_LENGTH)
		return -1;

	// move the file offset to the beginning of the next chunk
	struct png_chunk_detail current_chunk = ctx->current_chunk;
	if (ctx->initialized) {
		off_t next_chunk_offset = ctx->chunk_file_offset;
		next_chunk_offset += sizeof(u_int32_t);
		next_chunk_offset += sizeof(char) * CHUNK_TYPE_LENGTH;
		next_chunk_offset += sizeof(unsigned char) * current_chunk.data_length;
		next_chunk_offset += sizeof(u_int32_t);

		if (lseek(ctx->fd, next_chunk_offset, SEEK_SET) < 0)
			return -1;
	}

	// try to construct the png_chunk_detail, but simply throw away the result
	struct png_chunk_detail detail;
	int ret = construct_png_chunk_detail(ctx->fd, &detail);
	if (ret == -1)
		return -1;
	if (ret == 1)
		return 0;

	// reset fd offset to where it was initially
	if (lseek(ctx->fd, file_offset, SEEK_SET) < 0)
		return -1;

	return 1;
}

int chunk_iterator_next(struct chunk_iterator_ctx *ctx)
{
	// move the file offset to the beginning of the next chunk
	struct png_chunk_detail current_chunk = ctx->current_chunk;
	if (ctx->initialized) {
		off_t next_chunk_offset = ctx->chunk_file_offset;
		next_chunk_offset += sizeof(u_int32_t);
		next_chunk_offset += sizeof(char) * CHUNK_TYPE_LENGTH;
		next_chunk_offset += sizeof(unsigned char) * current_chunk.data_length;
		next_chunk_offset += sizeof(u_int32_t);

		off_t offset = lseek(ctx->fd, next_chunk_offset, SEEK_SET);
		if (offset < 0)
			return -1;
	}

	// read the current file offset from the file descriptor
	off_t file_offset = lseek(ctx->fd, 0, SEEK_CUR);
	if (file_offset < 0)
		return -1;
	if (file_offset < SIGNATURE_LENGTH)
		return -1;

	// try to construct the png_chunk_detail
	ctx->initialized = 1;
	ctx->chunk_file_offset = lseek(ctx->fd, 0, SEEK_CUR);
	if (ctx->chunk_file_offset < 0)
		return -1;

	int ret = construct_png_chunk_detail(ctx->fd, &ctx->current_chunk);
	if (ret != 0)
		return ret;

	// seek fd file offset to beginning of data portion
	off_t data_offset = file_offset + sizeof(u_int32_t) + (sizeof(char) * CHUNK_TYPE_LENGTH);
	if (lseek(ctx->fd, data_offset, SEEK_SET) < 0)
		return -1;

	return 0;
}

ssize_t chunk_iterator_read_data(struct chunk_iterator_ctx *ctx, unsigned char* buffer, size_t length)
{
	if (!ctx->initialized)
		return -1;

	off_t file_offset = lseek(ctx->fd, 0, SEEK_CUR);
	if (file_offset < 0)
		return -1;
	if (file_offset < SIGNATURE_LENGTH)
		return -1;

	off_t data_segment_start = ctx->chunk_file_offset
			+ sizeof(u_int32_t) + (sizeof(char) * CHUNK_TYPE_LENGTH);
	if (file_offset < data_segment_start)
		return -1;
	if (file_offset >= (data_segment_start + ctx->current_chunk.data_length))
		return 0;

	size_t bytes_left_to_read = data_segment_start + ctx->current_chunk.data_length
			- file_offset;
	bytes_left_to_read = bytes_left_to_read > length ? length : bytes_left_to_read;
	if (recoverable_read(ctx->fd, buffer, bytes_left_to_read) != bytes_left_to_read)
		return -1;

	return bytes_left_to_read;
}

int chunk_iterator_get_chunk_data_length(struct chunk_iterator_ctx *ctx, u_int32_t *len)
{
	if (!ctx->initialized)
		return 1;

	*len = ctx->current_chunk.data_length;
	return 0;
}

int chunk_iterator_get_chunk_type(struct chunk_iterator_ctx *ctx, char type[])
{
	if (!ctx->initialized)
		return 1;

	memcpy(type, ctx->current_chunk.chunk_type, CHUNK_TYPE_LENGTH);
	return 0;
}

int chunk_iterator_get_chunk_crc(struct chunk_iterator_ctx *ctx, u_int32_t *crc)
{
	if (!ctx->initialized)
		return 1;

	*crc = ctx->current_chunk.chunk_crc;
	return 0;
}

int chunk_iterator_is_critical(struct chunk_iterator_ctx *ctx)
{
	if (!ctx->initialized)
		return -1;

	if (!memcmp(IHDR_CHUNK_TYPE, ctx->current_chunk.chunk_type, CHUNK_TYPE_LENGTH))
		return 1;
	if (!memcmp(PLTE_CHUNK_TYPE, ctx->current_chunk.chunk_type, CHUNK_TYPE_LENGTH))
		return 1;
	if (!memcmp(IDAT_CHUNK_TYPE, ctx->current_chunk.chunk_type, CHUNK_TYPE_LENGTH))
		return 1;
	if (!memcmp(IEND_CHUNK_TYPE, ctx->current_chunk.chunk_type, CHUNK_TYPE_LENGTH))
		return 1;

	return 0;
}

int chunk_iterator_is_ancillary(struct chunk_iterator_ctx *ctx)
{
	if (!ctx->initialized)
		return -1;

	return !chunk_iterator_is_critical(ctx);
}

void chunk_iterator_destroy_ctx(struct chunk_iterator_ctx *ctx)
{
	ctx->fd = -1;
	ctx->current_chunk = (struct png_chunk_detail) {
			.chunk_type = {0, 0, 0, 0},
			.data_length = 0,
			.chunk_crc = 0
	};
}

/**
 * Attempt to construct a struct png_chunk_detail from a given position in a file.
 *
 * The given file descriptor fd must be open and seekable, and must be positioned
 * at the first byte of the png file chunk. The file offset is mutated after a
 * call to this function, so the caller must reposition the file offset with
 * lseek(), as needed.
 *
 * Returns zero if the chunk was processed successfully. Otherwise, if the given
 * file descriptor does not appear to represent a valid png chunk, returns 1. If
 * an unexpected error occurs, returns -1.
 * */
static int construct_png_chunk_detail(int fd, struct png_chunk_detail *new_chunk)
{
	// read and set the chunk's file offset
	off_t file_offset = lseek(fd, 0, SEEK_CUR);
	if (file_offset < 0)
		return -1;

	// read chunk data length and convert from network byte order to host byte order
	if (recoverable_read(fd, &new_chunk->data_length, sizeof(u_int32_t)) != sizeof(u_int32_t))
		return 1;
	new_chunk->data_length = ntohl(new_chunk->data_length);

	// read chunk type and ensure valid asccii characters
	if (recoverable_read(fd, new_chunk->chunk_type, CHUNK_TYPE_LENGTH) != CHUNK_TYPE_LENGTH)
		return 1;
	for (size_t i = 0; i < CHUNK_TYPE_LENGTH; i++) {
		if (!isascii(new_chunk->chunk_type[i]))
			return 1;
	}

	// seek to and read CRC and convert from network byte order to host byte order
	off_t crc_offset = file_offset;
	crc_offset += sizeof(u_int32_t);
	crc_offset += sizeof(char) * CHUNK_TYPE_LENGTH;
	crc_offset += sizeof(unsigned char) * new_chunk->data_length;

	if (lseek(fd, crc_offset, SEEK_SET) < 0)
		return -1;
	if (recoverable_read(fd, &new_chunk->chunk_crc, sizeof(u_int32_t)) != sizeof(u_int32_t))
		return 1;

	new_chunk->chunk_crc = ntohl(new_chunk->chunk_crc);

	return 0;
}
