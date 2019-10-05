#include <string.h>

#include "str-array.h"
#include "utils.h"

#define BUFF_SLOP 8

static void **str_array_detach_internal(struct str_array *str_a, size_t *len, int data);


void str_array_init(struct str_array *str_a)
{
	*str_a = (struct str_array){ NULL, 0, 0, 0 };
}

void str_array_grow(struct str_array *str_a, size_t size)
{
	if ((size - 1) < str_a->len)
		return;

	str_a->alloc = size;
	str_a->entries = (struct str_array_entry *)realloc(str_a->entries,
			str_a->alloc * sizeof(struct str_array_entry));
	if (!str_a->entries)
		FATAL(MEM_ALLOC_FAILED);
}

void str_array_release(struct str_array *str_a)
{
	for (size_t i = 0; i < str_a->len; i++) {
		free(str_a->entries[i].string);
		if (str_a->free_data)
			free(str_a->entries[i].data);
	}

	free(str_a->entries);
	str_array_init(str_a);
}

char *str_array_get(struct str_array *str_a, size_t pos)
{
	if (pos >= str_a->len)
		return NULL;

	return str_a->entries[pos].string;
}

struct str_array_entry *str_array_get_entry(struct str_array *str_a, size_t pos)
{
	if (pos >= str_a->len)
		return NULL;

	return &str_a->entries[pos];
}

int str_array_set(struct str_array *str_a, const char *str, size_t pos)
{
	if (pos >= str_a->len)
		return 1;

	char *duplicated_str = strdup(str);
	if (!duplicated_str)
		FATAL(MEM_ALLOC_FAILED);

	return str_array_set_nodup(str_a, duplicated_str, pos);
}

int str_array_set_nodup(struct str_array *str_a, char *str, size_t pos)
{
	if (pos >= str_a->len)
		return 1;

	free(str_a->entries[pos].string);
	if (str_a->free_data)
		free(str_a->entries[pos].data);

	str_a->entries[pos] = (struct str_array_entry) { .string = str, .data = NULL };

	return 0;
}

int str_array_push(struct str_array *str_a, ...)
{
	va_list ap;
	va_start(ap, str_a);
	int new_args = str_array_vpush(str_a, ap);
	va_end(ap);

	return new_args;
}

int str_array_vpush(struct str_array *str_a, va_list args)
{
	int new_strings = 0;

	char *arg;
	while ((arg = va_arg(args, char *))) {
		if ((str_a->len + 2) >= str_a->alloc)
			str_array_grow(str_a, str_a->alloc + BUFF_SLOP);

		char *str = strdup(arg);
		if (!str)
			FATAL(MEM_ALLOC_FAILED);

		str_a->entries[str_a->len++] = (struct str_array_entry) { .string = str, .data = NULL };
		str_a->entries[str_a->len] = (struct str_array_entry) { .string = NULL, .data = NULL };
		new_strings++;
	}

	return new_strings;
}

struct str_array_entry *str_array_insert(struct str_array *str_a, const char *str, size_t pos)
{
	char *duplicated_str = strdup(str);
	if (!duplicated_str)
		FATAL(MEM_ALLOC_FAILED);

	return str_array_insert_nodup(str_a, duplicated_str, pos);
}

struct str_array_entry *str_array_insert_nodup(struct str_array *str_a, char *str, size_t pos)
{
	if ((str_a->len + 2) >= str_a->alloc)
		str_array_grow(str_a, str_a->alloc + BUFF_SLOP);

	str_a->entries[str_a->len + 1] = (struct str_array_entry){ .string = NULL, .data = NULL };

	if (pos < str_a->len) {
		for (size_t i = str_a->len; i > pos; i--)
			str_a->entries[i] = str_a->entries[i - 1];
	} else {
		pos = str_a->len;
	}

	str_a->entries[pos] = (struct str_array_entry){ .string = str, .data = NULL };
	str_a->len++;

	return &str_a->entries[pos];
}

static int entry_comparator(const void *a, const void *b)
{
	const char *str_a = ((struct str_array_entry *)a)->string;
	const char *str_b = ((struct str_array_entry *)b)->string;

	return strcmp(str_a, str_b);
}

void str_array_sort(struct str_array *str_a)
{
	qsort(str_a->entries, str_a->len, sizeof(struct str_array_entry),
		  entry_comparator);
}

char *str_array_remove(struct str_array *str_a, size_t pos)
{
	if (pos >= str_a->len)
		return NULL;

	char *removed_str = str_a->entries[pos].string;
	if (str_a->free_data)
		free(str_a->entries[pos].data);

	for (size_t i = pos; i < str_a->len - 1; i++)
		str_a->entries[i] = str_a->entries[i+1];

	str_a->len--;
	str_a->entries[str_a->len] = (struct str_array_entry) { .string = NULL, .data = NULL };

	return removed_str;
}

void str_array_clear(struct str_array *str_a)
{
	for (size_t i = 0; i < str_a->len; i++) {
		free(str_a->entries[i].string);
		if (str_a->free_data)
			free(str_a->entries[i].data);

		str_a->entries[i] = (struct str_array_entry) { .string = NULL, .data = NULL };
	}

	str_a->len = 0;
}

char **str_array_detach(struct str_array *str_a, size_t *len)
{
	return (char **) str_array_detach_internal(str_a, len, 0);
}

void **str_array_detach_data(struct str_array *str_a, size_t *len)
{
	return str_array_detach_internal(str_a, len, 1);
}

static void **str_array_detach_internal(struct str_array *str_a, size_t *len, int data)
{
	void **arr;

	if (data)
		arr = (void **)malloc(sizeof(void *) * (str_a->len + 1));
	else
		arr = (void **)malloc(sizeof(char *) * (str_a->len + 1));

	if (!arr)
		FATAL(MEM_ALLOC_FAILED);

	for (size_t i = 0; i < str_a->len; i++) {
		if (data) {
			arr[i] = str_a->entries[i].data;
			free(str_a->entries[i].string);
		} else {
			arr[i] = str_a->entries[i].string;
			if (str_a->free_data)
				free(str_a->entries[i].data);
		}

		str_a->entries[i] = (struct str_array_entry) { .string = NULL, .data = NULL };
	}

	if (len != NULL)
		*len = str_a->len;

	arr[str_a->len] = NULL;
	free(str_a->entries);
	str_array_init(str_a);

	return arr;
}
