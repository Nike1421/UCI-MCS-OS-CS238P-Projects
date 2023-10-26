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
    /* The Jump Buffer For Our Thread */
    jmp_buf ctx;

    enum
    {
        STATUS_,
        STATUS_RUNNING,
        STATUS_SLEEPING,
        STATUS_TERMINATED
    } thread_status;

    /* Function we're calling */
    scheduler_fnc_t fnc;

    /* Argument to be passed to the function */
    void *arg;

    /* Stack of the thread */
    struct
    {
        /* All Memory Allocated To The Thread's Stack */
        void *memory_;
        /* Page Aligned Memory */
        void *memory;
    } stack;

    /* Next Thread In Line */
    struct Thread *linked_thread;
};

/* We cheat a little bit here */
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

    /* Allocate 1MB Memory to the thread */
    struct Thread *thread = malloc(1024 * 1024);

    if (!thread)
    {
        TRACE("scheduler_create: Thread : Memory Full");
    }
    

    page_size_v = page_size();

    thread->thread_status = STATUS_;
    thread->fnc = fnc;
    thread->arg = arg;

    /* Allocate Memory To Stack */
    thread->stack.memory_ = malloc(3 * page_size_v);

    if (!thread->stack.memory_)
    {
        TRACE("scheduler_create: Thread Stack :Memory Full");
        FREE(thread);
        thread = NULL;
    }
    

    /* Page Align the memory */
    thread->stack.memory = memory_align(thread->stack.memory_, page_size_v);
    thread->linked_thread = NULL;

    /* Manage Linked List Of Threads */
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
    /* Set Scheduler Jump Buffer */
    setjmp(state.ctx);
    /* Register Signal handler */
    signal(SIGALRM, (void (*)(int))scheduler_yield);   
    /* Call the Signal Handler after 1 second */
    alarm(1);
    /* Schedule Next Thread */
    schedule();
    /* Kill All Threads */
    destroy();
}

/**
 * Returns a candidate thread from the list of threads using the round-robin scheduling algorithm
 */
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
 * Executes the thread
 */
void schedule(void)
{

    /* Get Candidate Thread From The List */
    struct Thread *thread = thread_candidate();

    if (NULL == thread)
    {
        return;
    }

    /* When the thread is newly created */
    if (thread->thread_status == STATUS_)
    {
        /* x86_64 assembly instruction to assign the top of the thread stack to the rsp register (stack pointer). */
        uint64_t rsp = (uint64_t)thread->stack.memory;
        __asm__ volatile("mov %[rs], %%rsp \n"
                         : [rs] "+r"(rsp)::);

        thread->thread_status = STATUS_RUNNING;
        /* Calls the associated function */
        thread->fnc(thread->arg);

        /* The Thread has completed executing */
        thread->thread_status = STATUS_TERMINATED;
        
        /* After Thread has terminated, revert back the scheduler jump buffer */
        longjmp(state.ctx, 0);
    }

    /* Restore Thread Jump Buffer */
    longjmp(state.current_thread->ctx, 0);
}

/**
 * Frees all memory allocated to the threads
*/
void destroy(void)
{
    struct Thread *head_thread = state.head;
    struct Thread *next_thread = head_thread;

    while (next_thread->thread_status == STATUS_TERMINATED)
    {
        /* Make all pointer references NULL IFF we have reached the end of the thread list */
        if (state.head == state.tail)
        {
            state.tail->linked_thread = NULL;
            state.tail = NULL;
            state.head = NULL;
            next_thread = NULL;
        }
        /* Else, update the head of the thread list as we will deallocate the current thread */
        else
        {
            next_thread = next_thread->linked_thread;
            state.head = next_thread;
        }

        state.current_thread = NULL;

        FREE(head_thread->stack.memory_);
        FREE(head_thread);

        head_thread = next_thread;

        if (next_thread == NULL)
        {
            break;
        }
    }
}

/**
 * Called from within a user thread to yield the CPU to another user thread.
 */
void scheduler_yield(void)
{
    /* Reset The Alarm */
    alarm(0);
    /* Set Thread Jump Buffer */
    if (setjmp(state.current_thread->ctx))
    {
        return;
    }
    /* When a thread is supposed to yield to another thread, revert back the scheduler jump buffer */
    else
    {
        longjmp(state.ctx, 1);
    }
}