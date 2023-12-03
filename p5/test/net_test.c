#include <sys/wait.h>
#include <unistd.h>
#include <stdio.h>

#define BUFLEN 1024

int main(int argc, char **argv)
{
    pid_t pid;
    int pipe_arr[2];
    char buf[BUFLEN];
    ssize_t bytes_read;

    /* Create Pipe For Communication Between Parent And Child Process */
    pipe(pipe_arr);

    if ((pid = fork()) < 0)
    {
        printf("Error while creating fork");
        return -1;  
    }
    else if (pid == 0) /* Child Process */
    {
        /* Close The Read End Of The Pipe */
        close(pipe_arr[0]);
        /* Duplicate The Write End Of The Pipe To The Standard Output */
        dup2(pipe_arr[1], STDOUT_FILENO);
        /* Execute The Ping Command */
        execl("/bin/ping", "ping", "-s 20", "-c 15", "www.geeksforgeeks.org", (char *)NULL);
    }
    else /* Parent Process */
    {
        /* Close The Write End Of The Pipe */
        close(pipe_arr[1]);
        /* Read The Data From The Read End Of The Pipe */
        while ((bytes_read = read(pipe_arr[0], buf, sizeof(buf))) > 0)
        {
            /* Write The Data To The Standard Output */
            write(STDOUT_FILENO, buf, bytes_read);
        }
        /* Wait For The Child Process To Finish */
        waitpid(pid, NULL, 0);
    }
    /* Close The Pipe */
    close(pipe_arr[0]);
    close(pipe_arr[1]);
    return 0;
}