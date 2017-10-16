
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

void wrapper(void* arg){
    long i = *((long*)(&arg + 5));
    printf("i: %ld\n", i);
    thread_funcs[i](thread_args[i]);
    // TODO: Destroy this thread's resources now that it has completed execution.
}

void uthread_create(void *(start_routine)(void *), void* arg){

    // Store the args in a global variable.
    thread_funcs[num_threads] = start_routine;
    thread_args[num_threads] = arg;

    // Allocate the stack.
    char* stack = (char*)malloc(STACK_SIZE);

    // sp starts out at the top of the stack, pc at the wrapper function.
    address_t sp = (address_t)stack + STACK_SIZE - 10 * sizeof(void*);
    address_t pc = (address_t)wrapper;

    // Push a pointer to the function and arguments on the stack, above sp.
    auto * thread_index_addr = (void*)(sp + sizeof(void*));
    memcpy(thread_index_addr, &num_threads, sizeof(void*));

    // Modify the env_buf with the thread context.
    sigsetjmp(threads[num_threads],1);
    (threads[num_threads]->__jmpbuf)[JB_SP] = translate_address(sp);
    (threads[num_threads]->__jmpbuf)[JB_PC] = translate_address(pc);
    sigemptyset(&threads[num_threads]->__saved_mask);

    // We now have one more thread!
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

//////////////////////////////////// Test Cases

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

void test_uthread_create(){
    printf("creating f\n");
    uthread_create(f, (void*)10l);
    start();
}

int main(){
    test_uthread_create();
    return 0;
}
