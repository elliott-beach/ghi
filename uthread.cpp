// From http://www-users.cselabs.umn.edu/classes/Fall-2017/csci5103/PROJECT/PROJECT1/sigjmp-demo.c.

#include <aio.h>
#include <errno.h>
#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/time.h>
#include <deque>
#include <algorithm>

#include "uthread.h"

#define STACK_SIZE 4096

/**
 * Global thread state.
 */
TCB threads[MAX_THREADS];

/**
 * Jump buff for returning to main environment.
 */
sigjmp_buf main_env;

/**
 * Total number of created threads.
 */
int num_threads = 0;

/**
 * Ready List, waiting, suspended list
 */
std::deque<int> ready_list;
std::deque<int> waiting_list;
std::deque<int> suspended_list;

/**
 * TID of the current executing thread.
 */
int current_thread_id = -1;

/**
 * For signal handler and timer
 */
struct sigaction sa;
struct itimerval timer;

void uthread_yield();

void test_uthread_suspend();

void test_timing();

void finish() {
    siglongjmp(main_env, 1);
}

bool valid_tid(int tid) {
    return 0 <= tid && tid < num_threads;
}

/**
 * Removes all the threads that were waiting on thread tid from
 * the waiting list.
 * @param tid - the tid that threads no longre have to wait for
 */
void free_waiting_threads(int tid) {

    // Search for threads waiting on this thread - set them to ready
    std::deque<int>::iterator it = waiting_list.begin();
    std::deque<int>::iterator end = waiting_list.end();
    while (it != end) {
        int id = *it;
        if (threads[id].waiting_for_tid == tid) {
            threads[id].waiting_for_tid = -1;
            waiting_list.erase(it);
            ready_list.push_back(id);
        }
        ++it;
    }
}


/**
 * Mark a thread as complete and release its resources.
 * Interrupts should be disabled when this function is called.
 * @param tid The tid of the thread.
 */
void set_complete(int tid){
    TCB &tcb = threads[tid];
    tcb.complete = true;
    free(tcb.stack);
    free_waiting_threads(tid);
}

/**
 * Remove an id from a queue of items.
 * @param items - The queue of items.
 * @param id - The id to remove.
 * @return true if the item was removec, else false.
 */
bool remove(std::deque<int> &items, int num) {
    std::deque<int>::iterator it;
    if ((it = find(items.begin(), items.end(), num)) != items.end()) {
        items.erase(it);
        return true;
    }
    return false;
}


/* A translation is required when using an address of a variable.
    Use this as a black box in your code. */
#ifdef __x86_64__

/* 64 bit Intel arch */

typedef unsigned long address_t;
#define JB_SP 6
#define JB_PC 7

address_t translate_address(address_t addr) {
    address_t ret;
    asm volatile("xor    %%fs:0x30,%0\n"
            "rol    $0x11,%0\n"
    : "=g" (ret)
    : "0" (addr));
    return ret;
}

#else

/* 32 bit Intel arch */

typedef unsigned int address_t;
#define JB_BP 3
#define JB_SP 4
#define JB_PC 5

address_t translate_address(address_t addr){
    address_t ret;
    asm volatile("xor    %%gs:0x18,%0\n"
            "rol    $0x9,%0\n"
            : "=g" (ret)
            : "0" (addr));
    return ret;
}

#endif

/**
 * Blocks SIGVTALRM interrupts
 */
void disable_interrupts() {

    // Block interrupts
    sigset_t blockmask;
    if ((sigemptyset(&blockmask) == -1) || (sigaddset(&blockmask, SIGVTALRM) == -1)) {
        perror("Failed to initialize signal set");
        exit(1);
    } else if (sigprocmask(SIG_BLOCK, &blockmask, nullptr) == -1) {
        perror("Failed to block interrupt");
        exit(1);
    }
}

/**
 * Removes the block on SIGVTALRM
 */
void enable_interrupts() {
    sigset_t blockmask;
    if ((sigemptyset(&blockmask) == -1) || (sigaddset(&blockmask, SIGVTALRM) == -1)) {
        perror("Failed to intialize signal set");
        exit(1);
    }

    // Unblock interrupts
    if (sigprocmask(SIG_UNBLOCK, &blockmask, nullptr) == -1) {
        perror("Failed to unblock interrupt");
        exit(1);
    }
}

/**
 * Completes a thread's execution. adds to the ready list.
 */
void thread_complete() {
    printf("setting thread complete\n");

    disable_interrupts();

    int ret_val = sigsetjmp(threads[current_thread_id].env, 1);
    if (ret_val == 1) {
        enable_interrupts();
        return;
    }

    set_complete(current_thread_id);
    printf("set complete\n");

    // If all threads have complete execution
    if (ready_list.empty()) {
        enable_interrupts();
        finish();
    }

    printf("now here\n");

    // Choose the first thread on the ready list
    current_thread_id = ready_list.front();
    ready_list.pop_front();

    enable_interrupts();

    printf("jumping %d\n", current_thread_id);

    siglongjmp(threads[current_thread_id].env, 1);
}


/**
 * Add calling thread to the waiting list to block. Switch to a
 * different thread.
 */
void thread_switch() {

    disable_interrupts();

    printf("switching from %d\n", current_thread_id);
    int ret_val = sigsetjmp(threads[current_thread_id].env, 1);
    if (ret_val == 1) {
        printf("switched to %d\n", current_thread_id);
        enable_interrupts();
        return;
    }

    // Place calling thread on waiting list
    waiting_list.push_back(current_thread_id);

    // Deadlock
    if (ready_list.empty()) {
        enable_interrupts();
        finish();
    }

    // Take the top thread off the ready list
    current_thread_id = ready_list.front();
    ready_list.pop_front();

    enable_interrupts();

    siglongjmp(threads[current_thread_id].env, 1);
}


/**
 * thread_wrapper is the function initially called when a thread starts.
 * Calls the thread function with its argument, marks the thread as complete, and yields.
 * @param arg not used
 */
void thread_wrapper(void *arg) {
    TCB *tcb = &threads[current_thread_id];
    tcb->result = tcb->function(tcb->arg);
    printf("called function\n");
    thread_complete();
    printf("completed thread\n");
}

/*
 * Create a new uthread.
 * @param start_routine - The function that should be executed in the thread.
 * @param arg - An argument to pass to the function.
 * @return The tid of the created thread, or -1 if there were too many threads.
 */
int uthread_create(void *(start_routine)(void *), void *arg) {

    disable_interrupts();

    // We support a limited number of threads.
    if (num_threads >= MAX_THREADS - 1) {
        enable_interrupts();
        return -1;
    }

    int tid = num_threads;
    TCB *tcb = &threads[tid];

    // Store the arg and function in a global variable.
    tcb->function = start_routine;
    tcb->arg = arg;

    // Allocate the stack.
    tcb->stack = (char *) malloc(STACK_SIZE);

    // sp starts out at the top of the stack, pc at the wrapper function.
    auto sp = (address_t) tcb->stack + STACK_SIZE - 10 * sizeof(void *);
    auto pc = (address_t) thread_wrapper;

    // Modify the env_buf with the thread context.
    sigsetjmp(tcb->env, 1);
    (tcb->env->__jmpbuf)[JB_SP] = translate_address(sp);
    (tcb->env->__jmpbuf)[JB_PC] = translate_address(pc);
    sigemptyset(&tcb->env->__saved_mask);

    // Add thread to ready list
    ready_list.push_back(tid);

    // We now have one more thread!
    num_threads++;

    enable_interrupts();

    return tid;
}

void uthread_yield() {

    disable_interrupts();

    // Save execution state.
    int ret_val = sigsetjmp(threads[current_thread_id].env, 1);
    if (ret_val == 1) {
        enable_interrupts();
        return;
    }

    // Choose the next thread to execute.
    ready_list.push_back(current_thread_id);

    current_thread_id = ready_list.front();
    ready_list.pop_front();

    enable_interrupts();

    siglongjmp(threads[current_thread_id].env, 1);
}

int uthread_self() {
    return current_thread_id;
}

/**
 * Join against another thread.
 * @param tid The tid of the thread to join on.
 * @param retval A pointer which will be set to the return value of the thread.
 * @return 0 if join was successful, false if tid did not represent a valid thread.
 */
int uthread_join(int tid, void **retval) {
    if (!valid_tid(tid)) return -1;
    if (tid == current_thread_id || threads[tid].complete) return 0;
    threads[current_thread_id].waiting_for_tid = tid;
    thread_switch();
    printf("setting retval\n");
    *retval = threads[tid].result;
    printf("set retval\n");
    return 0;
}

/**
 * Attmpt to read nbytes bytes from file for file descriptor fildes, into the buffer pointed to by buff.
 * @param fildes The file descriptor to read from.
 * @param buf The buffer to read into.
 * @param nbytes Number of bytes to read.
 */
ssize_t async_read(int fildes, void *buf, size_t nbytes) {
    struct aiocb params{};
    params.aio_fildes = fildes;
    params.aio_buf = buf;
    params.aio_nbytes = nbytes;
    if (aio_read(&params) != 0) return -1;
    while (true) {
        switch (aio_error(&params)) {
            case EINPROGRESS:
                uthread_yield();
                continue;
            case ECANCELED:
                errno = ECANCELED;
                return -1;
            default:
                return aio_return(&params);
        }
    }
}

/**
 * Resume a suspended thread. Returns -1 on if tid is invalid or tid was not suspended
 * @param tid - The tid needed to be resumed
 */
int uthread_resume(int tid) {
    if (!valid_tid(tid)) return -1;
    disable_interrupts();

    // Verify that tid is currently suspended
    if (remove(suspended_list, tid)) {
        if (threads[tid].waiting_for_tid == -1){
            ready_list.push_back(tid);
        } else {
            waiting_list.push_back(tid);
        }
        enable_interrupts();
        return 0;
    }

    enable_interrupts();
    // If it wasn't suspended, return an error.
    return -1;
}

/**
 * Suspend a thread by adding it to the suspended list. Return -1 if tid is invalid or
 * tid is already suspended or complete
 * @param - The tid of the thread needed to be suspended
 * @return 0, if thread was successfully suspend, else -1.
 */
int uthread_suspend(int tid) {
    if (!valid_tid(tid)) return -1;
    disable_interrupts();

    // If tid is complete it can't be suspended
    if (threads[tid].complete) {
        enable_interrupts();
        return -1;
    }

    // If a thread tries to suspend itself
    if (tid == current_thread_id) {
        suspended_list.push_back(current_thread_id);
        thread_switch();
        return 0;
    }

    // Check if the tid trying to be suspended is in the ready list or the waiting list.
    if(remove(ready_list, tid)){
        suspended_list.push_back(tid);
    } else if(remove(waiting_list, tid)){
        suspended_list.push_back(tid);
    } else {
        // tid is already suspended
        enable_interrupts();
        return -1;
    }

    enable_interrupts();
    return 0;
}


/**
 * Terminate a thread by setting it to complete
 * @param tid - the tid of the thread needing to be terminated
 * @return 0 if termination was successful, -1 if tid was not valid.
 */
int uthread_terminate(int tid) {

    if (!valid_tid(tid)) return -1;

    disable_interrupts();

    if (tid == current_thread_id) {
        // We could enable interrupts here, but it is safer to not,
        // and thread_complete will disable interrupts immediatly anyway.
        thread_complete();
        return 0; // Unreachable code.
    }

    remove(ready_list, tid) || remove(waiting_list, tid) || remove(suspended_list, tid);
    set_complete(tid);

    enable_interrupts();
    return 0;
}

/**
 * Sets the time slice for how long each thread runs
 * @param time_slice - the new time slice for each thread in microseconds
 */
int uthread_init(int time_slice) {
    timer.it_interval.tv_usec = time_slice;
    return setitimer(ITIMER_VIRTUAL, &timer, nullptr);
}

/**
 * Calls thread_yield every time the timer expires
 */
void timer_handler(int signum) {
    static int count = 0;
    count++;
    uthread_yield();
}

/**
 * Set up timer
 */
int setupitimer(void) {
    timer.it_interval.tv_sec = 0;
    timer.it_interval.tv_usec = 1000;
    timer.it_value = timer.it_interval;
    return setitimer(ITIMER_VIRTUAL, &timer, nullptr);
}

/**
 * Set up interrupt handler
 */
int setupinterrupt(void) {
    sa.sa_handler = timer_handler;
    sa.sa_flags = 0;
    return (sigemptyset(&sa.sa_mask) || sigaction(SIGVTALRM, &sa, nullptr));
}

/**
 * Start the threading library.
 */
void start() {
    if (sigsetjmp(main_env, 1) != 0) {
        return;
    }

    if (setupinterrupt() == -1) {
        perror("Failed to set up handler");
    }
    if (setupitimer() == -1) {
        perror("Failed to set up timer");
    }

    current_thread_id = ready_list.front();
    ready_list.pop_front();
    siglongjmp(threads[current_thread_id].env, 1);
}

