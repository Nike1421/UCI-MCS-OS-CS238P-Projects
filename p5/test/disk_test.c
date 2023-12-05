#include "system.h"

size_t
safe_strlen(const char *s)
{
    return s ? strlen(s) : 0;
}

void us_sleep(uint64_t us)
{
    struct timespec in, out;

    in.tv_sec = (time_t)(us / 1000000);
    in.tv_nsec = (long)(us % 1000000) * 1000;
    while (nanosleep(&in, &out))
    {
        in = out;
    }
}

int main(int argc, char *argv[])
{
    int i;
    FILE *file;
    char line[4096];
    UNUSED(argc);
    UNUSED(argv);
    for (i = 0; i < 500; i++)
    {
        if (!(file = fopen("/mnt/test/file.txt", "a+")))
        {
            TRACE("fopen()");
            return -1;
        }
        fprintf(file, "Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod tempor incididunt ut labore et dolore magna aliqua. Massa enim nec dui nunc mattis enim ut tellus elementum. Ornare aenean euismod elementum nisi quis. Neque aliquam vestibulum morbi blandit cursus. Sagittis aliquam malesuada bibendum arcu vitae. Adipiscing diam donec adipiscing tristique risus nec feugiat in. Eu lobortis elementum nibh tellus molestie nunc. Scelerisque viverra mauris in aliquam sem fringilla ut morbi tincidunt. Vulputate ut pharetra sit amet. Non pulvinar neque laoreet suspendisse interdum consectetur libero id. Venenatis a condimentum vitae sapien.\n");
        fclose(file);
        us_sleep(10000);
        printf("File write %d\n", i);
    }
    for (i = 0; i < 500; i++)
    {
        if (!(file = fopen("/mnt/test/file.txt", "r")))
        {
            TRACE("fopen()");
            return -1;
        }
        fgets(line, sizeof(line), file);
        fclose(file);
        us_sleep(10000);
        printf("File read %d\n", i);
    }
    if (!(file = fopen("/mnt/test/file.txt", "w")))
    {
        TRACE("fopen()");
        return -1;
    }
    fclose(file);

    return 0;
}