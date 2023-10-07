/**
 * Tony Givargis
 * Copyright (C), 2023
 * University of California, Irvine
 *
 * CS 238P - Operating Systems
 * jitc.c
 */

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <dlfcn.h>
#include "system.h"
#include "jitc.h"

/**
 * Needs:
 *   fork()
 *   execv()
 *   waitpid()
 *   WIFEXITED()
 *   WEXITSTATUS()
 *   dlopen()
 *   dlclose()
 *   dlsym()
 */

/* research the above Needed API and design accordingly */
int jitc_compile(const char *input, const char *output){
    pid_t pid, w_pid;
    int status;
    char* execv_args[8];
    if ((pid = fork()) < 0){
        TRACE("FORK ERROR");
        /* code */
    } else if (0 == pid) {
        /* Part Executed By Child */
        execv_args[0] = "/usr/bin/gcc";
        execv_args[1] = "-shared"; 
        execv_args[2] = "-o";
        execv_args[3] = (char*) output;
        execv_args[4] = "-fPIC";
        execv_args[5] = "-O3";
        execv_args[6] = (char*) input;
        execv_args[7] = NULL;
    
        printf("EXECV PART\n");
        execv("/usr/bin/gcc", execv_args);
    } else {
        if ((w_pid = waitpid(pid, &status, 0)) == -1)
        {
            TRACE("CHILD ERROR");
            return 1;
        }
        else
        {
            if(WIFEXITED(status)){
                printf("Child exited with RC=%d\n",WEXITSTATUS(status));
            }
        }
    }
    
    return 0;
}

/*
struct jitc *jitc_open(const char *pathname){

}

void jitc_close(struct jitc *jitc){

}

long jitc_lookup(struct jitc *jitc, const char *symbol){

}*/