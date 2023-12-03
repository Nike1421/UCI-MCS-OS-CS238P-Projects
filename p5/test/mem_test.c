#include "../system.h"

#define BLOCKS 2048
#define BLOCK_SIZE 4096

void
us_sleep(uint64_t us)
{
	struct timespec in, out;

	in.tv_sec = (time_t)(us / 1000000);
	in.tv_nsec = (long)(us % 1000000) * 1000;
	while (nanosleep(&in, &out)) {
		in = out;
	}
}

int main(int argc, char *argv[])
{
    UNUSED(argc);
    UNUSED(argv);
    int *p;
    long i = 0;
    while (i < 10000000)
    {
        int inc = 1024 * 1024 * sizeof(char);
        p = (int *) calloc(1, inc);
        us_sleep(5000);
        i++;
    }
    FREE(p);
    /* us_sleep(20000000); */
    /* sleep(20); */
    /* free(blocks); */
    return 0;
}