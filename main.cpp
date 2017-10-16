
// From http://www-users.cselabs.umn.edu/classes/Fall-2017/csci5103/PROJECT/PROJECT1/sigjmp-demo.c.

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

sigjmp_buf threads[1000];
void* thread_args[1000];
thread_func thread_funcs[1000];

int num_threads = 0;
char* saved_stack;

void thread_yield();

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

// What if we get arg from a global array (TCB) instead of crazy shiet?
void wrapper(void* arg){
    long i = *((long*)(&arg + 5));
    printf("i: %ld\n", i);
    thread_funcs[i](thread_args[i]);
    // Destroy this thread's resources now that it has completed execution.
}

void uthread_create(void *(start_routine)(void *), void* arg){

    thread_funcs[num_threads] = start_routine;
    thread_args[num_threads] = arg;

    char* stack = (char*)malloc(STACK_SIZE);
    saved_stack = stack;


    address_t sp = (address_t)stack + STACK_SIZE - 10 * sizeof(void*);
    address_t pc = (address_t)wrapper;

    auto * addr2 = (void*)(sp + sizeof(void*));
    memcpy(addr2, &num_threads, sizeof(void*));

    sigsetjmp(threads[num_threads],1);
    (threads[num_threads]->__jmpbuf)[JB_SP] = translate_address(sp);
    (threads[num_threads]->__jmpbuf)[JB_PC] = translate_address(pc);
    sigemptyset(&threads[num_threads]->__saved_mask);

    num_threads++;
}

void thread_yield(){
    static int currentThread = 0;

    int ret_val = sigsetjmp(threads[currentThread],1);
    printf("SWITCH: ret_val=%d\n", ret_val);
    if (ret_val == 1) {
        return;
    }

    currentThread = (currentThread + 1) % num_threads;
    siglongjmp(threads[currentThread],1);
}

void start(){
    siglongjmp(threads[0],1);
}

//////////////////////////////////// Sample Invocation
void* f(void * arg){
    printf("argument: %ld\n", *((long*)(&arg+5)));
    int i=0;
    while(1) {
        ++i;
        printf("in f (%d)\n",i);
        if(i % 5 == 0){
        }
        if (i % 3 == 0) {
            printf("f: switching\n");
            thread_yield();
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
            thread_yield();
        }
        usleep(SECOND);
    }
    return 0;
}

int main(){
        printf("creating f\n");
    uthread_create(f, (void*)10l);
    //    printf("creating g\n");
    //uthread_create(g, (void*)20l);
    start();
    return 0;
}
/////////////////////////////////////





// #include <stdio.h>
// #include "threads.h"
// #include "scheduler.h"

// /*
//  * This file implements the API that is required for the project, starting with uthread_create.
//  * TODO: all other methods.
//  */


// // TODO: prevent the thread from dominating the execution flow, by setting a timer to stop execution.
// int uthread_create(void *(start_routine)(void *), void* arg){
//     start_routine(arg);
// }

// // The way this has got to work is by pushing this threads info to the global TCB,
// // the thread running the next start_routine on the list of functions, and this start_routine returning.
// int uthread_yield(){
//     // Implementation: jump to the scheduler, which then jumps back to us.
//     if(setjmp(theThread.env)){
//         return 0;
//     }
//     printf("theThread.env jump set\n");
//     longjmp(scheduler_stack, 1);

//     // Unreachable code.
//     return 0;
// }

// int initalize_scheduler(){
//     if(setjmp(scheduler_stack)){
//         printf("Jumped back to the scheduler!\n");
//         for(int i=0;i<1000000;i++)
//             ;
//         return 0;
//     }
//     return 0;
// }
