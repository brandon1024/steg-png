#include <string.h>
#include <ctype.h>
#include <arpa/inet.h>

#include "png-chunk-processor.h"
#include "crc.h"
#include "utils.h"

unsigned char PNG_SIG[] = {
		0x89, 0x50, 0x4e, 0x47,
		0x0d, 0x0a, 0x1a, 0x0a
};

char IEND_CHUNK_TYPE[] = {'I', 'E', 'N', 'D'};

int chunk_iterator_init_ctx(struct chunk_iterator_ctx *ctx, int fd)
{
	unsigned char signature[SIGNATURE_LENGTH];
	if (recoverable_read(fd, signature, SIGNATURE_LENGTH) != SIGNATURE_LENGTH)
		return -1;

	if (memcmp(PNG_SIG, signature, SIGNATURE_LENGTH) != 0)
		return 1;

	ctx->fd = fd;
	ctx->pos = SIGNATURE_LENGTH;
	return 0;
}

int chunk_iterator_next_chunk(struct chunk_iterator_ctx *ctx, struct strbuf *result)
{
	// read the chunk length
	u_int32_t len = 0;
	if (recoverable_read(ctx->fd, &len, sizeof(u_int32_t)) != sizeof(u_int32_t))
		return 0;
	ctx->pos += sizeof(u_int32_t);

	strbuf_attach_bytes(result, &len, sizeof(u_int32_t));

	len = CHUNK_TYPE_LENGTH + ntohl(len) + sizeof(u_int32_t);
	strbuf_grow(result, result->len + len);

	// read the chunk full chunk
	if (recoverable_read(ctx->fd, result->buff + result->len, len) != len)
		return -1;
	ctx->pos += len;
	result->len += len;

	// verify crc
	u_int32_t crc = 0;
	if (png_parse_chunk_crc(result, &crc))
		return -1;

	u_int32_t computed_crc = crc32(result->buff + sizeof(u_int32_t), result->len - sizeof(u_int32_t) - sizeof(u_int32_t));
	if (crc != computed_crc)
		return -1;

	return 1;
}

void chunk_iterator_destroy_ctx(struct chunk_iterator_ctx *ctx)
{
	ctx->fd = -1;
	ctx->pos = 0;
}

int png_parse_chunk_data_length(struct strbuf *chunk, u_int32_t *len)
{
	if (chunk->len <= sizeof(u_int32_t))
		return 1;

	u_int32_t tmp = *((u_int32_t *)chunk->buff);
	*len = ntohl(tmp);
	return 0;
}

int png_parse_chunk_type(struct strbuf *chunk, char type[])
{
	for (size_t i = 0; i < CHUNK_TYPE_LENGTH; i++) {
		if (i >= chunk->len)
			return 1;

		type[i] = chunk->buff[sizeof(u_int32_t) + i];
		if (!isascii(type[i]))
			return 1;
	}

	return 0;
}

int png_parse_chunk_crc(struct strbuf *chunk, u_int32_t *crc)
{
	u_int32_t data_len = 0;
	if (png_parse_chunk_data_length(chunk, &data_len))
		return 1;

	size_t crc_field_index = sizeof(u_int32_t) + CHUNK_TYPE_LENGTH + data_len;
	if (chunk->len != crc_field_index + sizeof(u_int32_t))
		return 1;

	u_int32_t tmp = *((u_int32_t *)(chunk->buff + crc_field_index));
	*crc = ntohl(tmp);
	return 0;
}
