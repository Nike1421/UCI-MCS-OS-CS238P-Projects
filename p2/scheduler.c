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
    struct Thread *tail;
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

    if (state.head == NULL) {
        state.head = thread;
        state.tail = state.head;
    } else {
        state.tail->linked_thread = thread;
        state.tail = thread;
    }
    state.tail->linked_thread = state.head;

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
    
    int temp = setjmp(state.ctx);
    switch (temp)
    {
    case 0:
    case 1:
        schedule();
        break;
    case 2:
        destroy();
        schedule();
        break;
    default:
        break;
    }
    
    /* schedule() */
    /* destroy() */
}

struct Thread* thread_candidate(void){
    
    struct Thread* head_thread = state.head;
    struct Thread* next_thread;

    if (state.current_thread == NULL || head_thread->thread_status == STATUS_) {
        if (state.head == NULL)
        {
            return NULL;
        } else if (state.head == state.tail)
        {
            return state.head;
        }
        state.current_thread = head_thread;
        return head_thread;
    } else {

        next_thread = state.current_thread->linked_thread;

        while (next_thread != state.current_thread) {
            if (next_thread->thread_status != STATUS_TERMINATED)
            {
                return next_thread;
            } else {
                next_thread = next_thread->linked_thread;
            }
        }
        return NULL;
    }
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
        thread->thread_status = STATUS_RUNNING;
        
        thread->fnc(thread->arg);
        thread->thread_status = STATUS_TERMINATED;
        
        longjmp(state.ctx, 2);
    }
    else{
        
        longjmp(thread->ctx, 1);    
    }
}

void destroy(void){
    
    struct Thread *current_thread = state.current_thread;

    if (state.current_thread == state.tail)
    {
        state.tail->linked_thread = NULL;
        state.tail = NULL;
        state.head = NULL;
    }
     
    else
    {
        state.head = current_thread->linked_thread;
    }
    

    state.current_thread = NULL;
    FREE(current_thread->stack.memory_);
    FREE(current_thread);
    
/*     longjmp(state.ctx, 1); */
}

/**
 * Called from within a user thread to yield the CPU to another user thread.
 */

void scheduler_yield(void){
    if(setjmp(state.current_thread->ctx)){
        return;
    }
    else{
        longjmp(state.ctx, 1);
    }
}


