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

struct jitc
{
    /* To store the opaque handle */
    void * handle;
};

/**
 * Compiles a C program into a dynamically loadable module.
 *
 * input : the file pathname of the C program
 * output: the file pathname of the dynamically loadable module
 *
 * return: 0 on success, otherwise error
 */
int jitc_compile(const char *input, const char *output){
    pid_t pid;
    int child_status;
    char* execv_args[8];

    if ((pid = fork()) < 0){
        TRACE("Error while creating fork");
    } else if (0 == pid) {
        /* Part Executed By Child */
        /* Arguments for execv() */
        execv_args[0] = "/usr/bin/gcc";
        execv_args[1] = "-shared"; 
        execv_args[2] = "-fPIC";
        execv_args[3] = "-O3";
        execv_args[4] = "-o";
        execv_args[5] = (char*) output;
        execv_args[6] = (char*) input;
        execv_args[7] = NULL;
    
        execv("/usr/bin/gcc", execv_args);
    } else {
        /* Part Executed By Parent */
        if (waitpid(pid, &child_status, 0) == -1)
        {
            TRACE("CHILD ERROR");
            return 1;
        }
        else
        {
            if(WIFEXITED(child_status)){
                printf("Process exited with Exit Status = %d\n",WEXITSTATUS(child_status));
            }
        }
    }
    return 0;
}

/**
 * Loads a dynamically loadable module into the calling process' memory for
 * execution.
 *
 * pathname: the file pathname of the dynamically loadable module
 *
 * return: an opaque handle or NULL on error
 */
struct jitc *jitc_open(const char *pathname){
    struct jitc* jitc = malloc(sizeof(struct jitc));

    if (!jitc) {
        TRACE("Memory Full");
        return NULL;
    }

    jitc->handle = dlopen(pathname, RTLD_LAZY);
    if (jitc->handle == NULL)
    {
        TRACE("Handle Not Found or Null");
    }
    return jitc;
}

/**
 * Unloads a previously loaded dynamically loadable module.
 *
 * jitc: an opaque handle previously obtained by calling jitc_open()
 *
 * Note: jitc may be NULL
 */
void jitc_close(struct jitc *jitc){
    if (NULL != jitc) {
        dlclose(jitc->handle);
    }
    FREE(jitc);
}

/**
 * Searches for a symbol in the dynamically loaded module associated with jitc.
 *
 * jitc: an opaque handle previously obtained by calling jitc_open()
 *
 * return: the memory address of the start of the symbol, or 0 on error
 */
long jitc_lookup(struct jitc *jitc, const char *symbol){
    void * address = dlsym(jitc->handle, symbol);
    if (NULL == address)
    {
        TRACE("Symbol Address Was Not Found");
        return 0;
    }
    return (long) address;
}