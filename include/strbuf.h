#ifndef STEG_PNG_STRBUF_H
#define STEG_PNG_STRBUF_H

#include <stdlib.h>

/**
 * strbuf api
 *
 * The strbuf api is used as an alternative to dynamically allocating character
 * and byte arrays. strbuf's are linearly resized when needed.
 *
 * Conventional string buffers can be easily attached (appended) to a strbuf, and
 * accessed using the strbuf->buff property. The internal buffer is guaranteed
 * to be null-terminated.
 * */

struct strbuf {
	char *buff;
	size_t len;
	size_t alloc;
};

/**
 * Initialize the strbuf. Enough space is allocated for an empty string, so that
 * calls to detach will always return a valid null-terminated string.
 * */
void strbuf_init(struct strbuf *buff);

/**
 * Release any resources under the strbuf. The strbuf MUST be reinitialized for
 * reuse.
 * */
void strbuf_release(struct strbuf *buff);

/**
 * Reallocate the strbuf to the given size. If the buffer is already larger than
 * size, the buffer is left as is.
 * */
void strbuf_grow(struct strbuf *buff, size_t size);

/**
 * Attach a string to the strbuf, up to buffer_len characters or until null byte
 * is encountered.
 * */
void strbuf_attach(struct strbuf *buff, const char *str, size_t buffer_len);

/**
 * Attach a null-terminated string to the strbuf. Similar to strbuf_attach(), except
 * uses strlen() to determine the buffer_len.
 * */
void strbuf_attach_str(struct strbuf *buff, const char *str);

/**
 * Attach a single character to the strbuf.
 * */
void strbuf_attach_chr(struct strbuf *buff, char ch);

/**
 * Attach a formatted string to the strbuf.
 *
 * The formatted string is constructed using snprintf(), potentially requiring
 * everal passes to allocate a buffer sufficiently large to hold the formatting
 * string.
 * */
void strbuf_attach_fmt(struct strbuf *buff, const char *fmt, ...);

/**
 * Attach arbitrary byte data to the strbuf.
 *
 * Attaching arbitrary byte data to the strbuf is discouraged unless using the
 * strbuf as a dynamically-allocated memory buffer. Using string-related functions
 * with this strbuf after attaching arbitrary byte data is undefined.
 * */
void strbuf_attach_bytes(struct strbuf *buff, const void *mem, size_t buffer_len);

/**
 * Trim leading and trailing whitespace from an strbuf, returning the number of
 * characters removed from the buffer.
 *
 * The underlying buffer is not resized, rather characters are just shifted such
 * that no leading or trailing whitespace exists. Note that this may not be
 * efficient with very large buffers.
 * */
int strbuf_trim(struct strbuf *buff);

/**
 * Detach the string from the strbuf. The strbuf is released and must be
 * reinitialized for reuse.
 * */
char *strbuf_detach(struct strbuf *buff);

/**
 * Clear all content stored under the given strbuf. The strbuf is not resized,
 * and can be reused for other purposes.
 * */
void strbuf_clear(struct strbuf *buff);

#endif //STEG_PNG_STRBUF_H
