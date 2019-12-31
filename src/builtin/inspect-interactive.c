#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

#include "parse-options.h"
#include "png-chunk-processor.h"
#include "utils.h"

// TODO interactive mode
// TODO byte chunk type
// TODO show signature
// TODO validate signature
// TODO show summary (signature, chunk types, chunk lengths, chunk CRCs)

int cmd_inspect_interactive(int argc, char *argv[])
{
	int interactive = 0;

	return interactive;
}
