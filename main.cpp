#include "uthread.h"
#include <assert.h>
#include <stdio.h>

////////////////////////////////////
/*  Unit Tests and Fixtures       */
///////////////////////////////////

// Fixtures

void *uthread_argument_fixture(void *arg) {
    assert((long) arg == 10);
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
    uthread_terminate(uthread_self());
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

// Test that we can top a thread's execution by terminating it.
// Also test that terminating the current thread stops execution.
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
    test_join_invalid_tid();
    test_uthread_suspend();
    test_async();
    test_timing();
    test_terminate();
    printf("All tests passed.\n");
    return 0;
}

