// From http://www-users.cselabs.umn.edu/classes/Fall-2017/csci5103/PROJECT/PROJECT1/sigjmp-demo.c.

#include <aio.h>
#include <errno.h>
#include <fcntl.h>

#include <assert.h>
#include <stdio.h>
#include <setjmp.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/time.h>
#include <deque>
#include <algorithm>

#define SECOND 1000000
#define STACK_SIZE 4096

typedef void *(*thread_func)(void *);

struct TCB {
    sigjmp_buf env;
    thread_func function;
    void *arg;
    void *result;
    int waiting_for_tid = -1;
    bool complete;
    char* stack;
};

/**
 * Global thread state.
 */
TCB threads[1000];


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

    disable_interrupts();

    int ret_val = sigsetjmp(threads[current_thread_id].env, 1);
    if (ret_val == 1) {
        setitimer(ITIMER_VIRTUAL, &timer, nullptr);
        enable_interrupts();
        return;
    }

    set_complete(current_thread_id);

    // If all threads have complete execution
    if (ready_list.empty()) {
        enable_interrupts();
        finish();
    }

    // Choose the first thread on the ready list
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
    thread_complete();
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
    if (num_threads >= 999) {
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
        // Reset the quantum
        setitimer(ITIMER_VIRTUAL, &timer, nullptr);
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

/**
 * Add calling thread to the waiting list to block. Switch to a
 * different thread.
 */
void thread_switch() {

    disable_interrupts();

    int ret_val = sigsetjmp(threads[current_thread_id].env, 1);
    if (ret_val == 1) {
	// Reset the quantum
	setitimer(ITIMER_VIRTUAL, &timer, nullptr);
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
    *retval = threads[tid].result;
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
    if(time_slice <= 0) 
	return -1;
    
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

////////////////////////////////////
/*  Unit Tests and Fixtures       */
///////////////////////////////////

// Fixtures

void *uthread_argument_fixture(void *arg) {
    assert((long) arg == 10);
    return 0;
}

void *uthread_yield_fixture(void *arg) {
    int limit = 5;
    for (int i = 0; i < 2 * limit; i++) {
        if (i % limit == 0) {
            uthread_yield();
        }
        printf("%d\n", i);
    }
    return 0;
}

void *yield_fixture(void *arg) {
    uthread_yield();
    return 0;
}

void *uthread_self_fixture(void *arg) {
    static int id = 0;
    assert(uthread_self() == id);
    id++;
    return nullptr;
}

void *return_10_fixture(void *arg) {
    return (void *) 10l;
}

void *test_timing_fixture(void *arg) {
    int limit = 5;
    static int i = 0;
    i++;
    if (i < limit) {
        uthread_create(test_timing_fixture, nullptr);
        // If we can get through this while loop, timing is working.
        while (i < limit);
    }
    return 0;
}

void *uthread_join_fixture(void *arg) {
    void *retval;
    int tid = uthread_create(return_10_fixture, nullptr);
    uthread_join(tid, &retval);
    assert((long) retval == 10l);
    return 0;
}

void *nop_fixture(void *arg) {}

void *uthread_suspend_fixture(void *arg) {
    int tid = uthread_create(nop_fixture, nullptr);
    uthread_suspend(tid);
    uthread_yield();
    assert(!threads[tid].complete);
    uthread_resume(tid);
    uthread_yield();
    assert(threads[tid].complete);
}

void *async_read_fixture(void *arg) {
    char buf[1024];
    int tid1 = uthread_create(yield_fixture, nullptr);
    int tid2 = uthread_create(yield_fixture, nullptr);
    ssize_t result = async_read(open("/etc/passwd", O_RDONLY), &buf, 100);
    assert(threads[tid1].complete);
    assert(threads[tid2].complete);
    return reinterpret_cast<void *>(result);
}

// Test that we cannot join on an invalid tid.
void *uthread_join_invalid_tid_fixture(void *arg) {
    void *retval;
    int ret = uthread_join(-1, &retval);
    assert(ret != 0);
}

void *test_uthread_yield_fixture(void *arg) {
    int tid = uthread_create(uthread_self_fixture, nullptr);
    assert(!threads[tid].complete);
    uthread_yield();
    assert(threads[tid].complete);
}

void* uthread_terminate_fixture(void* arg){
    int tid = uthread_create(uthread_self_fixture, nullptr);
    assert(!threads[tid].complete);
    uthread_terminate(tid);
    assert(threads[tid].complete);
}

void* uthread_terminate_self_fixture(void* arg){
    uthread_terminate(current_thread_id);
    assert(false);
}

// Tests

// Test that creating a uthread can start a thread, passing
// an argument.
void test_uthread_create() {
    uthread_create(uthread_argument_fixture, (void *) 10l);
    start();
}

// Test that creating a thread and calling
// `uthread_start` gives the expected result.
void test_thread_self() {
    uthread_create(uthread_self_fixture, nullptr);
    start();
}

// Test that creating a thread and joining captures
// the return value of the thread.
void test_uthread_join() {
    uthread_create(uthread_join_fixture, nullptr);
    start();
}

// Test that yielding causes another thread to run before the next
// statement.
void test_uthread_yield() {
    uthread_create(test_uthread_yield_fixture, nullptr);
    start();
}

// Test that joining a thread on an invalid tid does not cause
// the joining thread to block forever.
void *test_join_invalid_tid() {
    int id = uthread_create(uthread_join_invalid_tid_fixture, nullptr);
    start();
    assert(threads[id].complete);
}

// Test that suspending a thread causes it to leave
// the ready list and not execute while other threads run.
void test_uthread_suspend() {
    uthread_create(uthread_suspend_fixture, nullptr);
    start();
}

// Test that reading asynchronously succeeds but completes
// after letting other threads finish.
void *test_async() {
    int tid = uthread_create(async_read_fixture, nullptr);
    start();
    assert(reinterpret_cast<long>(threads[tid].result) == 100);
}

// Test that flow control will move between blocking threads via the timer.
void test_timing() {
    uthread_create(test_timing_fixture, nullptr);
    start();
}

void test_terminate(){
    uthread_create(uthread_terminate_fixture, nullptr);
    start();
    uthread_create(uthread_terminate_self_fixture, nullptr);
    start();
}

int main() {
    test_thread_self();
    test_uthread_create();
    test_uthread_join();
    test_uthread_suspend();
    test_join_invalid_tid();
    test_async();
    test_timing();
    test_terminate();
    printf("All tests passed.\n");
    return 0;
}

