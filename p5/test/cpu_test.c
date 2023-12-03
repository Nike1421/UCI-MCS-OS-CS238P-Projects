#include "../system.h"

int main(int argc, char *argv[])
{
    time_t secs = 20;
    time_t startTime = time(NULL);
    UNUSED(argc);
    UNUSED(argv);
    while (time(NULL) - startTime < secs)
    {
        /* code */
    }
    return 0;
}