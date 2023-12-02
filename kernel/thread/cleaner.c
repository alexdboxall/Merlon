
#include <thread.h>
#include <virtual.h>
#include <threadlist.h>
#include <irql.h>
#include <heap.h>
#include <semaphore.h>
#include <log.h>
#include <panic.h>
#include <physical.h>
#include <assert.h>

static struct thread_list terminated_list;
static struct semaphore* cleaner_semaphore;

static void CleanerDestroyThread(struct thread* thr) {
    (void) thr;

    // TODO: clean up user stacks if needed...?

    /*
     * TODO: @@@
     * The issue here: threads come from all different VASes. We need to call UnmapVirt in the vas it was
     * mapped into, THE PROBLEM BEING that it was actually mapped in using a completely different vas, but
     * it still works because it's *global* as it's kernel memory. The problem is though that the vas we have
     * doesn't have those globally mapped pages.
     * 
     * Probably need to have vas->global_mappings, or GetCpu()->global_mappings, and make GetVirtEntry check that
     * if it fails to find it locally. Would have to have a flag in MapPageEx for VM_GLOBAL, which sticks it in
     * the global one. Every function in virtual.c that looks at the mappings would need to look at both.
     * I see this as the only proper way of doing it.
     * 
     * Probably have AddMapping() and RemoveMapping() helper functions in virtual.c to assist with this.
     */

    SetVas(thr->vas);
    UnmapVirt(thr->kernel_stack_top - thr->kernel_stack_size, thr->kernel_stack_size);
    FreeHeap(thr->name);
    FreeHeap(thr);
}

static void CleanerThread(void* ignored) {
    (void) ignored;

    while (true) {
        /*
         * Block until there is a thread that needs terminating.
         */
        AcquireSemaphore(cleaner_semaphore, -1);

        LockScheduler();
        struct thread* thr = terminated_list.head;
        assert(thr != NULL);
        ThreadListDeleteTop(&terminated_list);
        UnlockScheduler();

        CleanerDestroyThread(thr);
    }
}

void InitCleaner(void) {
    ThreadListInit(&terminated_list, NEXT_INDEX_TERMINATED);

    /*
     * The actual number here is meaningless, we just need it so that we can release
     * semaphores every time we delete a task, and the cleaner will block until then.
     * This number should be big though, as it is techincally a limit on the maximum number
     * of terminated (but uncleaned) tasks.
     */
    cleaner_semaphore = CreateSemaphore(1 << 30, 1 << 30);

    CreateThread(CleanerThread, NULL, GetVas(), "cleaner");
}

static void NotifyCleaner(void* ignored) {
    (void) ignored;

    /*
     * Unblock the cleaner so it can delete our task.
     */
    ReleaseSemaphore(cleaner_semaphore);    
}

void TerminateThread(struct thread* thr) {
    if (thr == GetThread()) {
        LogWriteSerial("Properly terminating a thread, with stack at 0x%X\n", thr->kernel_stack_top - thr->kernel_stack_size);
        LockScheduler();
        ThreadListInsert(&terminated_list, thr);
        BlockThread(THREAD_STATE_TERMINATED);
        DeferUntilIrql(IRQL_STANDARD, NotifyCleaner, NULL);
        UnlockScheduler();
        Panic(PANIC_IMPOSSIBLE_RETURN);

    } else {
        /**
         * We can't terminate it directly, as it may be on any queue somewhere else,
         * and it'd be very messy to write special cases for all of them to be terminated
         * while in a queue. It's much easier to just signal that it needs to be terminated
         * if it is scheduled to run again.
         */
        LockScheduler();
        LogWriteSerial("Thread is scheduled to die... with stack at 0x%X\n", thr->kernel_stack_top - thr->kernel_stack_size);
        thr->death_sentence = true;
        UnlockScheduler();
    }
}