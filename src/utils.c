#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <ctype.h>

#include "utils.h"
#include "md5.h"

#define BUFF_LEN 1024

static void print_message(FILE *output_stream, const char *prefix,
		const char *fmt, va_list varargs);
static NORETURN void default_exit_routine(int status);
static NORETURN void (*exit_routine)(int) = default_exit_routine;

NORETURN void BUG(const char *fmt, ...)
{
	va_list varargs;

	va_start(varargs, fmt);
	print_message(stderr, "BUG: ", fmt, varargs);
	va_end(varargs);

	exit_routine(EXIT_FAILURE);
}

NORETURN void FATAL(const char *fmt, ...)
{
	va_list varargs;

	va_start(varargs, fmt);
	print_message(stderr, "fatal: ", fmt, varargs);
	va_end(varargs);

	exit_routine(EXIT_FAILURE);
}

NORETURN void DIE(const char *fmt, ...)
{
	va_list varargs;

	va_start(varargs, fmt);
	print_message(stderr, "", fmt, varargs);
	va_end(varargs);

	exit_routine(EXIT_FAILURE);
}

void WARN(const char *fmt, ...)
{
	va_list varargs;

	va_start(varargs, fmt);
	print_message(stderr, "warn: ", fmt, varargs);
	va_end(varargs);
}

static void print_message(FILE *output_stream, const char *prefix,
		const char *fmt, va_list varargs)
{
	fprintf(output_stream, "%s", prefix);
	vfprintf(output_stream, fmt, varargs);
	fprintf(output_stream, "\n");

	if (errno > 0)
		fprintf(stderr, "%s\n", strerror(errno));
}

void set_exit_routine(NORETURN void (*new_exit_routine)(int))
{
	exit_routine = new_exit_routine;
}

static NORETURN void default_exit_routine(int status)
{
	exit(status);
}

ssize_t recoverable_read(int fd, void *buf, size_t len)
{
	int errsv = errno;

	ssize_t bytes_read = 0;
	while(1) {
		bytes_read = read(fd, buf, len);
		if ((bytes_read < 0) && (errno == EAGAIN || errno == EINTR)) {
			errno = errsv;
			continue;
		}

		break;
	}

	return bytes_read;
}

ssize_t recoverable_write(int fd, const void *buf, size_t len)
{
	int errsv = errno;

	ssize_t bytes_written = 0;
	while(1) {
		bytes_written = write(fd, buf, len);
		if ((bytes_written < 0) && (errno == EAGAIN || errno == EINTR)) {
			errno = errsv;
			continue;
		}

		break;
	}

	return bytes_written;
}

ssize_t copy_file(const char *dest, const char *src, int mode)
{
	int in_fd, out_fd;
	if (!(in_fd = open(src, O_RDONLY)))
		return -1;
	if (!(out_fd = open(dest, O_WRONLY | O_CREAT | O_EXCL, mode)))
		return -1;

	ssize_t bytes_written = copy_file_fd(out_fd, in_fd);
	close(in_fd);
	close(out_fd);

	return bytes_written;
}

ssize_t copy_file_fd(int dest_fd, int src_fd)
{
	char buffer[BUFF_LEN];
	ssize_t bytes_written = 0;

	ssize_t bytes_read;
	while ((bytes_read = recoverable_read(src_fd, buffer, BUFF_LEN)) > 0) {
		// if write failed, return bytes_written
		if (recoverable_write(dest_fd, buffer, bytes_read) != bytes_read)
			return bytes_written;

		bytes_written += bytes_read;
	}

	return bytes_written;
}

void hex_dump(FILE *output_stream, off_t offset, unsigned char *buffer, size_t len)
{
	for (size_t i = 0; i < len; i += 16) {
		fprintf(output_stream, "%08llx  ", i + offset);

		for (size_t j = i; (j < i + 16) && (j < len); j++)
			fprintf(output_stream, "%02hhx ", buffer[j]);

		fprintf(output_stream, "%*s", (int)(i + 16 > len ? 3 * (i + 16 - len) + 1 : 1), " ");

		fprintf(output_stream, "|");
		for (size_t j = i; (j < i + 16) && (j < len); j++)
			fprintf(output_stream, "%c", isprint(buffer[j]) ? buffer[j] : '.');
		fprintf(output_stream, "|\n");
	}
}

int compute_md5_sum(int fd, unsigned char md5_hash[])
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

void print_file_summary(const char *file_path, int filename_table_len)
{
	unsigned char md5_hash[MD5_DIGEST_SIZE];
	struct stat st;
	int fd;

	// print input file summary
	if (lstat(file_path, &st) && errno == ENOENT)
		FATAL("failed to stat %s'", file_path);

	fd = open(file_path, O_RDONLY);
	if (fd < 0)
		DIE(FILE_OPEN_FAILED, file_path);

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
