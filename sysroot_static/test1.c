
#include <thread.h>
#include <virtual.h>
#include <heap.h>
#include <message.h>
#include <log.h>

static struct msgbox* cleaner_mbox;

static void CleanerThread(void*) {
    struct thread* thr;
    while (true) {
        ReceiveMessage(cleaner_mbox, &thr);
        UnmapVirt(thr->kernel_stack_top - thr->kernel_stack_size, thr->kernel_stack_size);
        FreeHeap(thr->name);
        FreeHeap(thr);
    }
}

void TerminateThread(struct thread* thr) {
    LockScheduler();
    if (thr == GetThread()) {
        SendMessage(cleaner_mbox, &thr);
        BlockThread(THREAD_STATE_TERMINATED);
    } else {
        thr->needs_termination = true;
    }
    UnlockScheduler();
}

void InitCleaner(void) {
    cleaner_mbox = CreateMessageBox("cleaner", sizeof(struct thread*));
    CreateThread(CleanerThread, NULL, GetVas(), "cleaner");
}