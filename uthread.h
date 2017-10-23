#include <fcntl.h>
#include <setjmp.h>

typedef void *(*thread_func)(void *);

#define MAX_THREADS 1000

struct TCB {
    sigjmp_buf env;
    thread_func function;
    void *arg;
    void *result;
    int waiting_for_tid = -1;
    bool complete;
    char* stack;
};

extern TCB threads[MAX_THREADS];

int uthread_create(void *(start_routine)(void *), void *arg);
void uthread_yield();
int uthread_self();
int uthread_join(int tid, void **retval);
ssize_t async_read(int fildes, void *buf, size_t nbytes);
int uthread_resume(int tid);
int uthread_suspend(int tid);
int uthread_terminate(int tid);
int uthread_init(int time_slice);
void start();