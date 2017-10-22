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
    void* arg;
    void* result;
    int waiting_for_tid = -1;
    bool complete;
};

/**
 * Global thread state.
 */
TCB threads[1000];

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

void uthread_yield();

/* A translation is required when using an address of a variable.
    Use this as a black box in your code. */
#ifdef __x86_64__

    /* 64 bit Intel arch */

    typedef unsigned long address_t;
    #define JB_SP 6
    #define JB_PC 7

    address_t translate_address(address_t addr)
    {
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
 * Removes all the threads that were waiting on thread tid from
 * the waiting list.
 * @param tid - the tid that threads no longre have to wait for
 */
void free_waiting_threads(int tid) {

    // Search for threads waiting on this thread - set them to ready
    std::deque<int>::iterator it = waiting_list.begin();
    std::deque<int>::iterator end = waiting_list.end();
    while(it != end) {
	int id = *it;
	if(threads[id].waiting_for_tid = tid) {
	    threads[id].waiting_for_tid == -1;
	    waiting_list.erase(it);
	    ready_list.push_back(id);
	}
	++it;
    }

}

/**
 * Completes a thread's execution. adds to the ready list.
 */
void thread_complete(){
    int ret_val = sigsetjmp(threads[current_thread_id].env,1);
    if (ret_val == 1) return;

    // After this is set, thread will never execute again.
    TCB* tcb = &threads[current_thread_id];
    tcb->complete = true;

    free_waiting_threads(current_thread_id);

    // If all threads have complete execution
    if(ready_list.empty()) {
	exit(0);
    }

    // Choose the first thread on the ready list
    current_thread_id = ready_list.front();
    ready_list.pop_front();

    siglongjmp(threads[current_thread_id].env,1);
}

/**
 * thread_wrapper is the function initially called when a thread starts.
 * Calls the thread function with its argument, marks the thread as complete, and yields.
 * @param arg not used
 */
void thread_wrapper(void *arg){
    TCB* tcb = &threads[current_thread_id];
    tcb->result = tcb->function(tcb->arg);

    thread_complete();
}

/*
 * Create a new uthread.
 * @param start_routine - The function that should be executed in the thread.
 * @param arg - An argument to pass to the function.
 * @return The tid of the created thread.
 */
int uthread_create(void *(start_routine)(void *), void* arg){

    TCB* tcb = &threads[num_threads];

    // Store the arg and function in a global variable.
    tcb->function = start_routine;
    tcb->arg = arg;

    // Allocate the stack.
    auto* stack = (char*)malloc(STACK_SIZE);

    // sp starts out at the top of the stack, pc at the wrapper function.
    auto sp = (address_t)stack + STACK_SIZE - 10 * sizeof(void*);
    auto pc = (address_t)thread_wrapper;

    // Modify the env_buf with the thread context.
    sigsetjmp(tcb->env,1);
    (tcb->env->__jmpbuf)[JB_SP] = translate_address(sp);
    (tcb->env->__jmpbuf)[JB_PC] = translate_address(pc);
    sigemptyset(&tcb->env->__saved_mask);

    // Add thread to ready list
    ready_list.push_back(num_threads);

    // We now have one more thread!
    return num_threads++;
}

/*
* Returns true if thread with given tid is ready, else false
* @param tid - The tid of the thread.
*/
bool is_thread_ready(int tid){
    TCB &tcb = threads[tid];
    int w_tid = tcb.waiting_for_tid;
    return !tcb.complete && (w_tid == -1 || threads[w_tid].complete);
}

void uthread_yield(){

    // Save execution state.
    int ret_val = sigsetjmp(threads[current_thread_id].env,1);
    if (ret_val == 1) return;

    // Choose the next thread to execute.
    ready_list.push_back(current_thread_id);

    current_thread_id = ready_list.front();
    ready_list.pop_front();

    siglongjmp(threads[current_thread_id].env,1);
}

/**
 * Add calling thread to the waiting list to block. Switch to a
 * different thread.
 */
void thread_switch(){

    int ret_val = sigsetjmp(threads[current_thread_id].env,1);
    if (ret_val == 1) return;

    // Place calling thread on waiting list
    waiting_list.push_back(current_thread_id);

    // Deadlock
    if(ready_list.empty()) {
	exit(0);
    }

    // Take the top thread off the ready list
    current_thread_id = ready_list.front();
    ready_list.pop_front();

    siglongjmp(threads[current_thread_id].env,1);
}

int uthread_self(){
    return current_thread_id;
}

int uthread_join(int tid, void **retval){
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
ssize_t async_read(int fildes, void *buf, size_t nbytes){
    struct aiocb params{};
    params.aio_fildes = fildes;
    params.aio_buf = buf;
    params.aio_nbytes = nbytes;
    if(aio_read(&params) != 0) return -1;
    while(true){
        switch(aio_error(&params)){
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
    // Verify that tid is valid
    if(tid >= num_threads || tid < 0)
	return -1;

    std::deque<int>::iterator it;

    // Verify that tid is currently suspended
    if((it = find(suspended_list.begin(), suspended_list.end(), tid)) != suspended_list.end()) {
	suspended_list.erase(it);
	if(threads[tid].waiting_for_tid == -1)
	    ready_list.push_back(tid);
	else
	    waiting_list.push_back(tid);
	return 0;
    }

    // If it wasn't suspended return an error
    return -1;
}

/**
 * Suspend a thread by adding it to the suspended list. Return -1 if tid is invalid or
 * tid is already suspended or complete
 * @param - The tid of the thread needed to be suspended
 */
int uthread_suspend(int tid) {
    // Verify that tid is valid
    if(tid >= num_threads || tid < 0)
	return -1;

    // If tid is complete it can't be suspended
    if(threads[tid].complete) {
	return -1;
    }

    // If a thread tries to suspend itself
    if(tid == current_thread_id) {
	suspended_list.push_back(current_thread_id);
	thread_switch();
	return 0;
    }

    std::deque<int>::iterator it;

    // Check if the tid trying to be suspended is in the ready list or the waiting list
    if((it = find(ready_list.begin(), ready_list.end(), tid)) != ready_list.end()) {
	ready_list.erase(it);
	suspended_list.push_back(tid);
    } else if((it = find(waiting_list.begin(), waiting_list.end(), tid)) != waiting_list.end()) {
	waiting_list.erase(it);
	suspended_list.push_back(tid);
    } else {
	return -1;  // Handles case if tid is already suspended
    }

    return 0;
}

/**
 * Terminate a thread by setting it to complete
 * @param tid - the tid of the thread needing to be terminated
 */
int uthread_terminate(int tid) {
    // Verify that tid is valid
    if(tid >= num_threads || tid < 0)
	return -1;

    threads[tid].complete = true;

    std::deque<int>::iterator it;
    if((it = find(ready_list.begin(), ready_list.end(), tid)) != ready_list.end()) {
	ready_list.erase(it);
    } else if((it = find(waiting_list.begin(), waiting_list.end(), tid)) != waiting_list.end()) {
	waiting_list.erase(it);
    } else {
	return -1;
    }

    free_waiting_threads(tid);
}
/**
 * Start the threading library.
 */
void start(){
    current_thread_id = ready_list.front();
    ready_list.pop_front();
    siglongjmp(threads[current_thread_id].env,1);
}

////////////////////////////////////
/*  Unit Tests and Fixtures       */
///////////////////////////////////

void* uthread_test_function(void* arg){
    assert((long)arg == 10);
    for(int i=0;i<100;i++){
        printf("%d\n", i);
    }
    return 0;
}
void* uthread_yield_test_function(void* arg){
    int limit = 100;
    for(int i=0;i<2*limit;i++){
        if(i % limit == 0){
            uthread_yield();
        }
        printf("%d\n", i);
    }
    return 0;
}

void* uthread_self_test_function(void* arg){
    static int id = 0;
    assert(uthread_self()==id);
    id++;
    return nullptr;
}

void* return_10_fixture(void* arg){
   return (void*)10l;
}

void* uthread_join_test(void* arg){
    uthread_create(return_10_fixture, nullptr);
}

void* f(void * arg){
    assert((long)arg == 10);
    printf("argument: %ld\n", (long)arg);
    int i=0;
    while(1) {
        ++i;
        printf("in f (%d)\n",i);
        if(i % 5 == 0){
        }
        if (i % 3 == 0) {
            printf("f: switching\n");
            uthread_yield();
        }
        usleep(SECOND);
    }
    return 0;
}

void* g(void * arg){
    printf("&arg: %p\n", &arg);
    printf("arg: 0x%lx\n", (long)arg);
    int i=0;
    while(1){
        ++i;
        printf("in g (%d)\n",i);
        if (i % 5 == 0) {
            printf("g: switching\n");
            uthread_yield();
        }
        usleep(SECOND);
    }
    return 0;
}

void test_uthread_create(){
    uthread_create(uthread_test_function, (void*)10l);
}

// Create 10 threads, and check that they alternate between eachother
void test_thread_yield(){
    uthread_create(uthread_yield_test_function, nullptr);
    uthread_create(uthread_yield_test_function, nullptr);
    uthread_create(uthread_yield_test_function, nullptr);
    uthread_create(uthread_yield_test_function, nullptr);
    uthread_create(uthread_yield_test_function, nullptr);
}

void test_thread_self(){
    uthread_create(uthread_self_test_function, nullptr);
}

void* uthread_join_test_function(void* arg){
    void* retval;
    uthread_join(uthread_create(return_10_fixture, nullptr), &retval);
    assert((long)retval == 10l);
    return nullptr;
}

void test_uthread_join(){
    uthread_create(uthread_join_test_function, nullptr);
}

void* yield_wrapper(void* arg){
    test_thread_yield();
}

void* do_something(void* arg){
    printf("FRONT\n");
    uthread_yield();
    printf("END\n");  // This should print last
}

void* uthread_suspend_test_function(void* arg) {
    int tid = uthread_create(do_something, nullptr);
    uthread_yield();
    uthread_suspend(tid);
    uthread_yield();
    printf("MIDDLE\n");
    uthread_resume(tid);
}

void test_thread_suspend_resume() {
    uthread_create(uthread_suspend_test_function, nullptr);
}

void* async_test(void* arg){
    char buf [1024];
    uthread_create(uthread_yield_test_function, nullptr);
    uthread_create(uthread_yield_test_function, nullptr);
    uthread_create(uthread_yield_test_function, nullptr);
    async_read(open("/etc/passwd", O_RDONLY), &buf, 100);
}

int main(){
    test_thread_self();
    test_uthread_create();
    test_uthread_join();
    uthread_create(yield_wrapper, nullptr);
    test_thread_suspend_resume();
    uthread_create(async_test, nullptr);
    start();
    return 0;
}
