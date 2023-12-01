
#include <arch.h>
#include <irql.h>
#include <thread.h>
#include <assert.h>
#include <virtual.h>
#include <sys/types.h>
#include <avl.h>
#include <semaphore.h>
#include <spinlock.h>
#include <heap.h>
#include <process.h>
#include <log.h>
#include <linkedlist.h>

struct process {
    pid_t pid;
    struct vas* vas;
    pid_t parent;
    struct avl_tree* children;
    struct avl_tree* dead_children;
    struct avl_tree* threads;
    struct semaphore* lock;
    struct semaphore* killed_children_semaphore;
    int retv;
    bool terminated;
};

struct process_table_node {
    pid_t pid;
    struct process* process;
};

static struct spinlock pid_lock;
static pid_t next_pid = 1;
static struct avl_tree* process_table;
static struct semaphore* process_table_mutex;

static int ProcessTableComparator(void* a_, void* b_) {
    struct process_table_node* a = a_;
    struct process_table_node* b = b_;

    if (a->pid == b->pid) {
        return 0;
    }
    return a->pid < b->pid ? -1 : 1;
}

static int InsertIntoProcessTable(struct process* prcss) {
    AcquireSpinlockIrql(&pid_lock);
    pid_t pid = next_pid++;
    ReleaseSpinlockIrql(&pid_lock);

    AcquireMutex(process_table_mutex, -1);

    struct process_table_node* node = AllocHeap(sizeof(struct process_table_node));
    node->pid = pid;
    node->process = prcss;
    AvlTreeInsert(process_table, (void*) node);

    ReleaseMutex(process_table_mutex);

    return pid;
}

static void RemoveFromProcessTable(pid_t pid) {
    AcquireMutex(process_table_mutex, -1);

    struct process_table_node dummy;
    dummy.pid = pid;
    struct process_table_node* actual = AvlTreeGet(process_table, (void*) &dummy);
    AvlTreeDelete(process_table, (void*) actual);
    FreeHeap(actual);       // this was allocated on 'InsertIntoProcessTable'

    ReleaseMutex(process_table_mutex);
}

struct process* GetProcessFromPid(pid_t pid) {
    MAX_IRQL(IRQL_STANDARD);

    AcquireMutex(process_table_mutex, -1);

    struct process_table_node dummy;
    dummy.pid = pid;
    struct process_table_node* node = AvlTreeGet(process_table, (void*) &dummy);

    ReleaseMutex(process_table_mutex);

    return node->process;
}

void LockProcess(struct process* prcss) {
    LogWriteSerial("LOCKING: 0x%X 0x%X\n", prcss, prcss->lock);
    AcquireMutex(prcss->lock, -1);
}

void UnlockProcess(struct process* prcss) {
    LogWriteSerial("UNLOCKING: 0x%X 0x%X\n", prcss, prcss->lock);
    ReleaseMutex(prcss->lock);
}

void InitProcess(void) {
    InitSpinlock(&pid_lock, "pid", IRQL_SCHEDULER);
    process_table_mutex = CreateMutex();
    process_table = AvlTreeCreate();
    AvlTreeSetComparator(process_table, ProcessTableComparator);
}

struct process* CreateProcess(pid_t parent_pid) {
    MAX_IRQL(IRQL_STANDARD);

    struct process* prcss = AllocHeap(sizeof(struct process));

    prcss->lock = CreateMutex();
    prcss->vas = CreateVas();
    prcss->parent = parent_pid;
    prcss->children = AvlTreeCreate();
    prcss->threads = AvlTreeCreate();
    prcss->killed_children_semaphore = CreateSemaphore(1000, 1000);
    prcss->retv = 0;
    prcss->terminated = false;
    prcss->pid = InsertIntoProcessTable(prcss);

    if (parent_pid != 0) {
        struct process* parent = GetProcessFromPid(parent_pid);
        LockProcess(parent);
        AvlTreeInsert(parent->children, (void*) prcss);
        UnlockProcess(parent);
    }

    return prcss;
}

void AddThreadToProcess(struct process* prcss, struct thread* thr) {
    LockProcess(prcss);
    AvlTreeInsert(prcss->threads, (void*) thr);
    thr->process = prcss;
    UnlockProcess(prcss);
}

struct process* ForkProcess(void) {
    MAX_IRQL(IRQL_STANDARD);

    return NULL;
}

static void ReapProcess(struct process* prcss) {
    // TOOD: there's more cleanup to be done here..., e.g. VAS()

    // TODO: need to destroy semaphore, but of course it started at some huge number (e.g. 1 << 30), 
    //       so destroy is going to shit itself. probably need to pass flags to CreateSemaphore to have
    //       an 'inverse' mode - or check that on deletion it equals what it started on, or just an IGNORE_HELD_ON_DELETE?

    RemoveFromProcessTable(prcss->pid);
    FreeHeap(prcss);
}

static pid_t TryReapProcessAux(struct process* parent, struct avl_node* node, pid_t target, int* status) {
    if (node == NULL) {
        return 0;
    }

    LogWriteSerial("   : 0x%X, %d\n", node, target);

    struct process* child = (struct process*) AvlTreeGetData(node);
    LogWriteSerial("   about to try locking the child... (plz no deadlock..?)\n");
    LockProcess(child);
    LogWriteSerial("    > terminated %d, pid %d\n", child->terminated, child->pid);

    if (child->terminated && (child->pid == target || target == (pid_t) -1)) {
        *status = child->retv;
        pid_t retv = child->pid;
        LogWriteSerial("Actually reaping!!!\n");
        UnlockProcess(child);       // needed in case someone is waiting on us, before our death
        ReapProcess(child);
        return retv;
    }
    UnlockProcess(child);

    pid_t left_retv = TryReapProcessAux(parent, AvlTreeGetLeft(node), target, status);
    if (left_retv != 0) {
        return left_retv;
    }

    return TryReapProcessAux(parent, AvlTreeGetRight(node), target, status);
}

static pid_t TryReapProcess(struct process* parent, pid_t target, int* status) {
    LogWriteSerial("TryReapProcess, parent->children = 0x%X, rootNode(*) = 0x%X\n", parent->children, AvlTreeGetRootNode(parent->children));
    return TryReapProcessAux(parent, AvlTreeGetRootNode(parent->children), target, status);
} 

pid_t WaitProcess(pid_t pid, int* status, int flags) {
    MAX_IRQL(IRQL_STANDARD);

    (void) flags;

    struct process* prcss = GetProcess();
    
    LogWriteSerial("     -> START OF WAIT: the semaphore has count: %d\n", GetSemaphoreCount(prcss->killed_children_semaphore));

    pid_t result = 0;

    int failed_reaps = 0;
    while (result == 0) {
        LogWriteSerial("...A\n");
        AcquireSemaphore(prcss->killed_children_semaphore, -1);
        LogWriteSerial("...B\n");
        LockProcess(prcss);
        LogWriteSerial("About to start reaping on %d\n", pid);
        result = TryReapProcess(prcss, pid, status);

        UnlockProcess(prcss);

        if (result == 0 && pid != (pid_t) -1) {
            LogWriteSerial("got a failed reap! (%d)\n", pid);
            failed_reaps++;
        }
    }

    LogWriteSerial("WaitProcess done for pid %d, with %d fails.\n", pid, failed_reaps);

    /*
     * Ensure that the next time we call WaitProcess(), we can immediately retry the reaps that
     * we increased the semaphore for, but didn't actually reap on.
     */
    while (failed_reaps--) {
        ReleaseSemaphore(prcss->killed_children_semaphore);
    }

    LogWriteSerial("     -> END OF WAIT: the semaphore has count: %d\n", GetSemaphoreCount(prcss->killed_children_semaphore));

    return result;
}

static void AdoptOrphan(struct process* adopter, struct process* ophan) {
    LockProcess(adopter);

    ophan->parent = adopter->pid;
    AvlTreeInsert(adopter->children, (void*) ophan);
    ReleaseSemaphore(adopter->killed_children_semaphore);

    UnlockProcess(adopter);
}

static void OrphanChildProcesses(struct avl_node* node) {
    if (node == NULL) {
        return;
    }

    OrphanChildProcesses(AvlTreeGetLeft(node));
    OrphanChildProcesses(AvlTreeGetRight(node));

    AdoptOrphan(GetProcessFromPid(1), AvlTreeGetData(node));
}

/*
static void KillRemainingThreads(struct avl_node* node) {
    if (node == NULL) {
        return;
    }

    KillRemainingThreads(AvlTreeGetLeft(node));
    KillRemainingThreads(AvlTreeGetRight(node));

    struct thread* victim = (struct thread*) AvlTreeGetData(node);
    TerminateThread(victim);
}*/

void KillProcess(int retv) {
    MAX_IRQL(IRQL_STANDARD);

    struct process* prcss = GetProcess();
    prcss->terminated = true;
    prcss->retv = retv;

    /*
     * TODO: this needs to be deferred. (we can't destroy the thread we're on!!!)
     */
    //KillRemainingThreads(AvlTreeGetRootNode(prcss->threads));
    //AvlTreeDestroy(prcss->threads);

    OrphanChildProcesses(AvlTreeGetRootNode(prcss->children));
    AvlTreeDestroy(prcss->children);

    // TODO: delete the VAS when safe to do so

    if (prcss->parent == 0) {
        ReapProcess(prcss);

    } else {
        struct process* parent = GetProcessFromPid(prcss->parent);
        ReleaseSemaphore(parent->killed_children_semaphore);
    }
}

struct process* GetProcess(void) {
    struct thread* thr = GetThread();
    if (thr == NULL) {
        return NULL;
    }
    return thr->process;
}

struct process* CreateProcessWithEntryPoint(pid_t parent, void(*entry_point)(void*), void* args) {
    struct process* prcss = CreateProcess(parent);
    struct thread* thr = CreateThread(entry_point, args, prcss->vas, "init");
    AddThreadToProcess(prcss, thr);
    return prcss;
}

pid_t GetPid(struct process* prcss) {
    return prcss->pid;
}