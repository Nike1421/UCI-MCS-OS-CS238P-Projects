#include <stdio.h>
#include <string.h>
#include <signal.h>
#include "system.h"

#define PROC_MEMINFO "/proc/meminfo"
#define PROC_NET "/proc/net/dev"
#define PROC_STAT "/proc/stat"
#define PROC_UPTIME "/proc/uptime"
#define PROC_DEVICE "/proc/diskstats"

static volatile int done;

static void
_signal_(int signum)
{
    assert(SIGINT == signum);
    printf("\n");
    printf("\n");
    printf("\n");
    done = 1;
}

double cpu_util(const char *s)  
{
    static unsigned sum_, vector_[7];
    unsigned sum, vector[7];
    const char *p;
    double util;
    uint64_t i;

    /*
      user
      nice
      system
      idle
      iowait
      irq
      softirq
    */

    if (!(p = strstr(s, " ")) ||
        (7 != sscanf(p,
                     "%u %u %u %u %u %u %u",
                     &vector[0],
                     &vector[1],
                     &vector[2],
                     &vector[3],
                     &vector[4],
                     &vector[5],
                     &vector[6])))
    {
        return 0;
    }
    sum = 0.0;
    for (i = 0; i < ARRAY_SIZE(vector); ++i)
    {
        sum += vector[i];
    }
    util = (1.0 - (vector[3] - vector_[3]) / (double)(sum - sum_)) * 100.0;
    sum_ = sum;
    for (i = 0; i < ARRAY_SIZE(vector); ++i)
    {
        vector_[i] = vector[i];
    }
    return util;
}

double get_cpu_util()
{
    char line[1024];
    FILE *file;
    if (!(file = fopen(PROC_STAT, "r")))
    {
        TRACE("fopen()");
        return -1;
    }
    if (fgets(line, sizeof(line), file))
    {
        return cpu_util(line);
    }
    fclose(file);
    return 0.0;
}

double memory_util()
{
    char line[1024];
    FILE *file;
    unsigned long mem_total = 0, mem_free = 0;
    double mem_usage = 0.0;

    if (!(file = fopen(PROC_MEMINFO, "r")))
    {
        fprintf(stderr, "Error opening %s\n", PROC_MEMINFO);
        return 0.0;
    }

    while (fgets(line, sizeof(line), file))
    {
        unsigned long value;
        if (sscanf(line, "MemTotal: %lu kB", &value) == 1)
        {
            mem_total = value;
        }
        else if (sscanf(line, "MemFree: %lu kB", &value) == 1)
        {
            mem_free = value;
        }
    }

    fclose(file);

    if (mem_total > 0)
    {
        mem_usage = (mem_total - mem_free) / (double)mem_total * 100.0;
    }

    return mem_usage;
}

void network_packet_util()
{
    char line[1024];
    FILE *file;
    const char *interface = "eth0";
    /* unsigned long packets_received, packets_sent; */
    unsigned vector[4];

    if (!(file = fopen(PROC_NET, "r")))
    {
        fprintf(stderr, "Error opening %s\n", PROC_NET);
        return;
    }
    /* Read The File Line By Line */
    while (fgets(line, sizeof(line), file))
    {
        /* Check If The Read Line Contains The Interface Name */
        if (strstr(line, interface))
        {
            /* Extract The Received And Transmitted Packets */
            sscanf(line + strcspn(line, ":") + 1, "%u %u %*u %*u %*u %*u %*u %*u %u %u",
                   &vector[2], &vector[0], &vector[3], &vector[1]);
            printf("---------------Network Stats---------------\n");
            printf("Packets Sent: %u | Packets Received: %u\n", vector[0], vector[1]);
            printf("Bytes Sent: %u | Bytes Received: %u\n", vector[2], vector[3]);
            fflush(stdout);
            fclose(file);
            return;
        }
    }
}

void uptime_util()
{
    char line[1024];
    FILE *file;
    float uptime, idle_time;
    if (!(file = fopen(PROC_UPTIME, "r")))
    {
        fprintf(stderr, "Error opening %s\n", PROC_UPTIME);
        return;
    }
    if (fgets(line, sizeof(line), file))
    {
        sscanf(line, "%f %f", &uptime, &idle_time);
        printf("----------------Uptime Stats---------------\n");
        printf("Uptime: %.2f | Idle Time: %.2f\n", uptime, idle_time);
        fflush(stdout);
        fclose(file);
    }
    
}

void disk_util()
{
    char line[1024];
    FILE *file;
    unsigned int reads, writes;
    char dev_name[20];

    file = fopen(PROC_DEVICE, "r");

    while (fgets(line, sizeof(line), file) != NULL)
    {
        sscanf(line, "%*u %*u %s %*u %*u %u %*u %u %*u %*u %*u",
               dev_name, &reads, &writes);

        /* reads = 512 * reads;
        writes = 512 * writes; */
        if (strcmp(dev_name, "loop0") == 0)
        {
            printf("-----------------Disk Stats----------------\n");
            printf("Reads: %u | Writes: %u\n", reads, writes);
            fflush(stdout);
            fclose(file);
            return;
        }
    }
    printf("Device not found\n");
    
}
/* Add memory utilization calculation to the main loop */
int main(int argc, char *argv[])
{

    /*  */
    UNUSED(argc);
    UNUSED(argv);

    if (SIG_ERR == signal(SIGINT, _signal_))
    {
        TRACE("signal()");
        return -1;
    }
    fflush(stdout);

    while (!done)
    {
        printf("\033[2J\033[H");
        printf("-----------------CPU Stats-----------------\n");
        printf("CPU Usage: %5.1f%%\n", get_cpu_util());
        printf("----------------Memory Stats---------------\n");
        printf("Memory Utilization: %.2f%%\n", memory_util());
        network_packet_util();
        uptime_util();
        disk_util();
        us_sleep(750000);
    }
    return 0;
}