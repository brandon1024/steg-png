#ifndef STEG_PNG_UTILS_H
#define STEG_PNG_UTILS_H

#include <stdio.h>
#include <sys/types.h>

#define NORETURN __attribute__((noreturn))

/**
 * Frequently used error/information messages should be defined here.
 * */
#define MEM_ALLOC_FAILED "unable to allocate memory"
#define FILE_OPEN_FAILED "failed to open file '%s'"

/**
 * Simple assertion function. Invoking this function will print a message to
 * stderr, and exit with status EXIT_FAILURE.
 *
 * This function is primarily used to catch fail fast when an undefined state
 * is encountered.
 *
 * If errno is set, an appropriate message is also printed to stderr.
 * */
NORETURN void BUG(const char *fmt, ...);

/**
 * Function used to terminate the application if an unexpected error occurs.
 * This function should be used in situations where the application encountered
 * an error that it cannot recover from, such as a NULL pointer returned from
 * a malloc() call.
 *
 * If errno is set, an appropriate message is also printed to stderr.
 *
 * The message is printed to stderr, and the application exits with status
 * EXIT_FAILURE.
 * */
NORETURN void FATAL(const char *fmt, ...);

/**
 * Function used to terminate the application if the user attempted to do
 * something that was unexpected.
 *
 * The message is printed to stderr, and the application exits with status
 * EXIT_FAILURE.
 *
 * If errno is set, an appropriate message is also printed to stderr.
 * */
NORETURN void DIE(const char *fmt, ...);

/**
 * Function used to warn the user of exceptional situations, but not fatal to
 * the application.
 *
 * The message is printed to stderr.
 * */
void WARN(const char *fmt, ...);

/**
 * Configure which routine should be invoked to exit the current process.
 *
 * By default, exit(int) is used as the exit routine. However, this cannot be
 * used in certain situations, such as within a fork()'ed process. This
 * provides an interface to modify this behavior.
 * */
void set_exit_routine(NORETURN void (*new_exit_routine)(int status));

/**
 * A self-recovering wrapper for read(). If EINTR or EAGAIN is encountered,
 * retries read().
 * */
ssize_t recoverable_read(int fd, void *buf, size_t len);

/**
 * A self-recovering wrapper for write(). If EINTR or EAGAIN is encountered,
 * retries write().
 * */
ssize_t recoverable_write(int fd, const void *buf, size_t len);

/**
 * Copy a file from the src location to the dest location. `dest` and `src` must
 * be null-terminated strings.
 *
 * The new file will assume the given mode.
 *
 * If the src file cannot be opened for reading, -1 is returned.
 * If the destination file cannot be opened for writing, -1 is returned.
 *
 * If reading/writing could not be completed due to an unexpected error, returns
 * the total number of bytes written so far.
 *
 * If successful, returns the total number of bytes written.
 * */
ssize_t copy_file(const char *dest, const char *src, int mode);

/**
 * Copy a file from the src location to the dest location. `dest_fd` and `src_fd`
 * must be open file descriptors.
 *
 * If the src file cannot be opened for reading, -1 is returned.
 * If the destination file cannot be opened for writing, -1 is returned.
 *
 * If reading/writing could not be completed due to an unexpected error, returns
 * the total number of bytes written so far.
 *
 * If successful, returns the total number of bytes written.
 * */
ssize_t copy_file_fd(int dest_fd, int src_fd);

/**
 * Print canonical hexdump of a given data buffer. The offset argument specifies
 * the offset of the chunk of data; useful for printing the hexdump of a file or
 * larger stream (requiring multiple calls to this function).
 *
 * Example:
 * 00000000  23 20 4a 65 74 42 72 61  69 6e 73 0a 2e 69 64 65  |# JetBrains..ide|
 * 00000010  61 2f 0a 0a 23 20 6d 61  63 4f 53 0a 2e 44 53 5f  |a/..# macOS..DS_|
 * 00000020  70 65 63 69 66 69 63 0a  65 78 74 65 72 6e 2f     |pecific.extern/|
 * */
void hex_dump(FILE *output_stream, off_t offset, unsigned char *buffer, size_t len);

/**
 * Compute the md5 sum of an open file. The md5_hash argument must be an array of
 * length MD5_DIGEST_SIZE. The file offset is assumed to be positioned at
 * byte zero.
 * */
int compute_md5_sum(int fd, unsigned char md5_hash[]);

/**
 * Print a summary of a file, with table formatting capability.
 *
 * Prints the file summary in the following format:
 * <filename> <file mode> <file length> <md5 hash>
 *
 * The filename may be padded with whitespace using the filename_table_len
 * argument.
 * */
void print_file_summary(const char *file_path, int filename_table_len);

#endif //STEG_PNG_UTILS_H
