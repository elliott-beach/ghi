// From http://www-users.cselabs.umn.edu/classes/Fall-2017/csci5103/PROJECT/PROJECT1/sigjmp-demo.c.

#include <assert.h>
#include <stdio.h>
#include <setjmp.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/time.h>

#define SECOND 1000000
#define STACK_SIZE 4096

typedef void *(*thread_func)(void *);

struct TCB {
    sigjmp_buf env;
    thread_func function;
    void* arg;
    bool complete;
};

TCB threads[1000];

/**
 * Total number of created threads.
 */
int num_threads = 0;


/**
 * TID of the current executing thread.
 */
int current_thread_id = -1;

void uthread_yield();

#ifdef __x86_64__

    /* 64 bit Intel arch */

    typedef unsigned long address_t;
    #define JB_SP 6
    #define JB_PC 7

    /* A translation is required when using an address of a variable.
       Use this as a black box in your code. */
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

    /* A translation is required when using an address of a variable.
       Use this as a black box in your code. */
    address_t translate_address(address_t addr){
        address_t ret;
        asm volatile("xor    %%gs:0x18,%0\n"
                "rol    $0x9,%0\n"
                : "=g" (ret)
                : "0" (addr));
        return ret;
    }

#endif

void wrapper(void* arg){
    TCB* tcb = &threads[current_thread_id];
    tcb->function(tcb->arg);

    // After this is set, thread will never execute again.
    tcb->complete = true;
    uthread_yield();
}

void uthread_create(void *(start_routine)(void *), void* arg){

    TCB* tcb = &threads[num_threads];

    // Store the arg and function in a global variable.
    tcb->function = start_routine;
    tcb->arg = arg;

    // Allocate the stack.
    auto* stack = (char*)malloc(STACK_SIZE);

    // sp starts out at the top of the stack, pc at the wrapper function.
    auto sp = (address_t)stack + STACK_SIZE - 10 * sizeof(void*);
    auto pc = (address_t)wrapper;

    // Modify the env_buf with the thread context.
    sigsetjmp(tcb->env,1);
    (tcb->env->__jmpbuf)[JB_SP] = translate_address(sp);
    (tcb->env->__jmpbuf)[JB_PC] = translate_address(pc);
    sigemptyset(&tcb->env->__saved_mask);

    // We now have one more thread!
    num_threads++;
}

void uthread_yield(){

    // Save execution state.
    int ret_val = sigsetjmp(threads[current_thread_id].env,1);
    if (ret_val == 1) return;

    // Choose the next thread to execute.
    int id = current_thread_id;
    do {
        id = (id + 1) % num_threads;
        if(id == current_thread_id){
            printf("infinite loop!\n");
        }
    } while (threads[id].complete);

    // Execute that thread.
    current_thread_id = id;
    siglongjmp(threads[current_thread_id].env,1);
}

int uthread_self(){
    return current_thread_id;
}

void start(){
    current_thread_id = 0;
    siglongjmp(threads[current_thread_id].env,1);
}

////////////////////////////////////
//////////////////////////////////// Test Cases
////////////////////////////////////

void* uthread_test_function(void* arg){
    assert((long)arg == 10);
    for(int i=0;i<100;i++){
        printf("%d\n", i);
    }
    return 0;
}
void* uthread_yield_test_function(void* arg){
    for(int i=0;true;i++){
        if(i % 500000 == 0){
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

void* yield_wrapper(void* arg){
    test_thread_yield();
}

int main(){
    test_thread_self();
    test_uthread_create();
    uthread_create(yield_wrapper, nullptr);
    start();
    return 0;
}
