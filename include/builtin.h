#ifndef STEG_PNG_BUILTIN_H
#define STEG_PNG_BUILTIN_H

struct steg_png_builtin {
	const char *cmd;
	int (*fn)(int, char **);
};

extern int cmd_embed(int argc, char *argv[]);
extern int cmd_extract(int argc, char *argv[]);
extern int cmd_inspect(int argc, char *argv[]);

/* embed strategies */
extern int cmd_embed_zlib_strategy(int argc, char *argv[]);
extern int cmd_embed_lsb_strategy(int argc, char *argv[]);

#endif //STEG_PNG_BUILTIN_H
