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
#include <signal.h>

/**
 * Needs:
 *   setjmp()
 *   longjmp()
 */

/* research the above Needed API and design accordingly */

struct Thread
{
    jmp_buf ctx;

    enum
    {
        STATUS_,
        STATUS_RUNNING,
        STATUS_SLEEPING,
        STATUS_TERMINATED
    } thread_status;

    int id;

    /* Maybe Remove Later */
    scheduler_fnc_t fnc;
    void *arg;

    struct
    {
        void *memory_;
        void *memory;
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
int scheduler_create(scheduler_fnc_t fnc, void *arg)
{
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

    if (state.head == NULL)
    {
        state.head = thread;
        state.tail = state.head;
    }
    else
    {
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

void scheduler_execute(void)
{

    int temp = setjmp(state.ctx);
    switch (temp)
    {
    case 0:
    case 1:
        settimer();
        schedule();
        break;
    case 2:
        cleartimer();
        destroy();
        settimer();
        schedule();
        break;
    default:
        break;
    }

    /* setjmp(state.ctx);
    schedule();
    printf("%dA\n", state.current_thread->id);
    destroy(); */

    /* schedule() */
    /* destroy() */
}

struct Thread *thread_candidate(void)
{

    struct Thread *head_thread = state.head;
    struct Thread *next_thread;

    /* Enters the if block twice */
    /* Once when all threads have status STATUS_ */
    /* Second time when all threads are terminated and we free their memory */
    if (state.current_thread == NULL || head_thread->thread_status == STATUS_)
    {
        
        /* Handle the situation when all threads except the last have been freed */
        if (state.head == state.tail)
        {
            
            state.current_thread = state.head;
            return state.head;
        }
        
        state.current_thread = head_thread;
        return head_thread;
    }
    else
    {
        /* Set Next Thread */
        next_thread = state.current_thread->linked_thread;

        /* If the next thread is not terminated, return it */
        /* Else loop until we get the first thread which is not terminated and return it */
        /* Or else return null if all threads have been terminated */
        while (next_thread != state.current_thread)
        {
            if (next_thread->thread_status != STATUS_TERMINATED)
            {
                state.current_thread = next_thread;
                return next_thread;
            }
            else
            {
                next_thread = next_thread->linked_thread;
            }
        }
        
        return NULL;
    }
}

/**
 * runs the thread returned by thread_candidate function
 */
void schedule(void)
{

    /* Get Candidate Thread From The List */
    struct Thread *thread = thread_candidate();
    
    if (NULL == thread)
    {
        return;
    }

    /* Update the current thread */
    state.current_thread = thread;
    
    /* First Run */
    if (thread->thread_status == STATUS_)
    {
        uint64_t rsp = (uint64_t)thread->stack.memory; /* initialize this variable to the memory location (top of it) */
        __asm__ volatile("mov %[rs], %%rsp \n"
                         : [rs] "+r"(rsp)::);
        thread->thread_status = STATUS_RUNNING;
        
        thread->fnc(thread->arg);

        /* The Thread has completed executing */
        thread->thread_status = STATUS_TERMINATED;
        /* Long Jump to scheduler_execute() */
        longjmp(state.ctx, 2);
    }
    else
    {
        /* Long Jump to scheduler_yield()*/
        longjmp(thread->ctx, 1);
    }
}

void destroy(void)
{

    struct Thread *current_thread = state.current_thread;

    /* Make all pointer references NULL IFF we have reached the end of the thread list */
    if (state.head == state.tail)
    {
        state.tail->linked_thread = NULL;
        state.tail = NULL;
        state.head = NULL;
    }
    /* Else, update the head of the thread list as we will deallocate the current thread */
    else
    {
        state.head = current_thread->linked_thread;
    }
    state.current_thread = NULL;
    FREE(current_thread->stack.memory_);
    FREE(current_thread);
    /* Long Jump to scheduler_execute() */
    longjmp(state.ctx, 1);
}

void destroy2(void){
    
}

/**
 * Called from within a user thread to yield the CPU to another user thread.
 */

void scheduler_yield(void)
{
    if (setjmp(state.current_thread->ctx))
    {
        return;
    }
    else
    {
        longjmp(state.ctx, 1);
    }
}

void handler(int signum)
{
    signum++;
    scheduler_yield();
}

void settimer(void)
{
    signal(SIGALRM, handler);
    alarm(1);
}

void cleartimer(void)
{
    alarm(0);
    signal(SIGALRM, SIG_DFL);
}
