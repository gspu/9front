#include <u.h>
#include <libc.h>
#include <bio.h>
#include <String.h>
#include <thread.h>
#include "wiki.h"

void
main(int argc, char **argv)
{
	print("%d\n", nametonum(argv[1]));
}
