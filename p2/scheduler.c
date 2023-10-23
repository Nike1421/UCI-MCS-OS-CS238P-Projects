/**
 * Tony Givargis
 * Copyright (C), 2023
 * University of California, Irvine
 *
 * CS 238P - Operating Systems
 * scheduler.c
 */

#undef _FORTIFY_SOURCE

#include <unistd.h>
#include <signal.h>
#include <setjmp.h>
#include "system.h"
#include "scheduler.h"

/**
 * Needs:
 *   setjmp()
 *   longjmp()
 */

/* research the above Needed API and design accordingly */


struct Thread {
    jmp_buf ctx;
    
    enum {
        STATUS_,
        STATUS_RUNNING,
        STATUS_SLEEPING,
        STATUS_TERMINATED
    } thread_status;

    int id;
    
    /* Maybe Remove Later */
    scheduler_fnc_t fnc;
    void *arg;

    struct {
        void * memory_;
        void * memory;
    } stack;

    struct Thread *linked_thread;
};

static struct
{
    struct Thread *head;
    struct Thread *current_thread;
    int number_of_threads;
    jmp_buf ctx;
} state;


/**
 * Creates a new user thread.
 *
 * fnc: the start function of the user thread (see scheduler_fnc_t)
 * arg: a pass-through pointer defining the context of the user thread
 *
 * return: 0 on success, otherwise error
 */
int scheduler_create(scheduler_fnc_t fnc, void *arg){
    size_t page_size_v;
    static int id = 1;
    struct Thread *thread = malloc(1024 * 1024);
    
    page_size_v = page_size();

    thread->thread_status = STATUS_;
    
    thread->fnc = fnc;
    thread->arg = arg;
    thread->id = id++;

    thread->stack.memory_ = malloc(3 * page_size_v);
    thread->stack.memory = memory_align(thread->stack.memory_, page_size_v);
    thread->linked_thread = NULL;

    /* Link */
    if (thread->id != 1)
    {
        struct Thread *iterator_thread = thread;
        while (NULL != iterator_thread->linked_thread)
        {
            iterator_thread = iterator_thread->linked_thread;
        }
        iterator_thread->linked_thread = thread;
        iterator_thread = state.head;
    } else {
        state.head = thread;
        state.number_of_threads++;
    }
    return 0;
}

/**
 * Called to execute the user threads previously created by calling
 * scheduler_create().
 *
 * Notes:
 *   * This function should be called after a sequence of 0 or more
 *     scheduler_create() calls.
 *   * This function returns after all user threads (previously created)
 *     have terminated.
 *   * This function is not re-enterant.
 */

void scheduler_execute(void){
    /* int temp = setjmp(state.ctx); */
    /* schedule() */
    /* destroy() */
}

struct Thread* thread_candidate(void){
    struct Thread* head_thread = state.head;
    struct Thread* next_thread_in_line;
    int i = 0;

    if(state.current_thread->linked_thread){
        next_thread_in_line =  state.current_thread->linked_thread;
    } else
    {
        next_thread_in_line = head_thread;
    }
    

    if (head_thread->thread_status == STATUS_)
    {
        return head_thread;
    }

    if (next_thread_in_line->thread_status == STATUS_ || next_thread_in_line->thread_status == STATUS_SLEEPING)
    {
        return next_thread_in_line;
    }
    
    while (next_thread_in_line->thread_status == STATUS_TERMINATED)
    {
        if (i == state.number_of_threads)
        {
            return NULL;
        } 
        i++;    
    }
    
    return next_thread_in_line;   
}

/**
 * runs the thread returned by thread_candidate function
*/
void schedule(void){
    struct Thread *thread = thread_candidate();
    if(NULL == thread){
        return;
    }
    state.current_thread = thread;
    if(thread->thread_status == STATUS_){
        uint64_t rsp = (uint64_t) thread->stack.memory; /* initialize this variable to the memory location (top of it) */
        __asm__ volatile ("mov %[rs], %%rsp \n" : [rs] "+r" (rsp) ::);
    }
    thread->thread_status = STATUS_RUNNING;
    longjmp(thread->ctx, 1);
}

/**
 * Called from within a user thread to yield the CPU to another user thread.
 */

void scheduler_yield(void){

}


