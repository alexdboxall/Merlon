
#include <heap.h>
#include <stdlib.h>
#include <vfs.h>
#include <log.h>
#include <assert.h>
#include <irql.h>
#include <errno.h>
#include <string.h>
#include <transfer.h>
#include <sys/stat.h>
#include <dirent.h>
#include <panic.h>
#include <thread.h>
#include <termios.h>
#include <mailbox.h>
#include <virtual.h>

#define INTERNAL_BUFFER_SIZE 256        // used to communicate with master and sub. can have any length - but lower means both input AND **PRINTING** will incur more semaphore trashing
#define LINE_BUFFER_SIZE 300            // maximum length of a typed line
#define FLUSHED_BUFFER_SIZE 500         // used to store any leftover after pressing '\n' that the program has yet to read

struct master_data {
    struct vnode* subordinate;
    struct mailbox* display_buffer;
    struct mailbox* keybrd_buffer;
    struct mailbox* flushed_buffer;
    struct thread* line_processing_thread;
};

struct sub_data {
    struct vnode* master;
    struct termios termios;
    char line_buffer[LINE_BUFFER_SIZE];
    uint8_t line_buffer_char_width[LINE_BUFFER_SIZE];
    int line_buffer_pos;
};

// "THE SCREEN"
static int MasterRead(struct vnode* node, struct transfer* tr) {  
    struct master_data* internal = node->data;
    while (tr->length_remaining > 0) {
        uint8_t c;
        MailboxGet(internal->display_buffer, -1, &c);
        PerformTransfer(&c, tr, 1);
    }

    return 0;
}

// "THE KEYBOARD"
static int MasterWrite(struct vnode* node, struct transfer* tr) {
    struct master_data* internal = node->data;

    while (tr->length_remaining > 0) {
        char c;
        PerformTransfer(&c, tr, 1);
        MailboxAdd(internal->keybrd_buffer, -1, c);
    }

    return 0;
}

static void FlushSubordinateLineBuffer(struct vnode* node) {
    struct sub_data* internal = node->data;
    struct master_data* master_internal = internal->master->data;

    for (int i = 0; i < internal->line_buffer_pos; ++i) {
        MailboxAdd(master_internal->flushed_buffer, -1, internal->line_buffer[i]);
    }

    internal->line_buffer_pos = 0;
}

static void RemoveFromSubordinateLineBuffer(struct vnode* node) {
    struct sub_data* internal = node->data;

    if (internal->line_buffer_pos == 0) {
        return;
    }

    internal->line_buffer[--internal->line_buffer_pos] = 0;
}

static void AddToSubordinateLineBuffer(struct vnode* node, char c, int width) {
    struct sub_data* internal = node->data;

    if (internal->line_buffer_pos == LINE_BUFFER_SIZE) {
        return;
    }

    internal->line_buffer[internal->line_buffer_pos] = c;
    internal->line_buffer_char_width[internal->line_buffer_pos] = width;
    internal->line_buffer_pos++;
}

static void LineProcessor(void* sub_) {
    SetThreadPriority(GetThread(), SCHEDULE_POLICY_FIXED, FIXED_PRIORITY_KERNEL_HIGH);

    struct vnode* node = (struct vnode*) sub_;
    struct sub_data* internal = node->data;
    struct master_data* master_internal = internal->master->data;

    while (true) {
        bool echo = internal->termios.c_lflag & ECHO;
        bool canon = internal->termios.c_lflag & ICANON;

        uint8_t c;
        MailboxGet(master_internal->keybrd_buffer, -1, &c);

        /*
         * Must happen before we modify the line buffer (i.e. to add / backspace 
         * a character), as the backspace code needs to check for non-empty 
         * lines (so this must be done before we make the line empty).
         */
        if (echo) {
            if (c == '\b' && canon) {
                if (internal->line_buffer_pos > 0) {
                    MailboxAdd(master_internal->display_buffer, -1, '\b');
                    MailboxAdd(master_internal->display_buffer, -1, ' ');
                    MailboxAdd(master_internal->display_buffer, -1, '\b');
                }
            } else {
                MailboxAdd(master_internal->display_buffer, -1, c);
            }
        }

        if (c == '\b' && canon) {
            RemoveFromSubordinateLineBuffer(node);

        } else {
            AddToSubordinateLineBuffer(node, c, 1);
        }

        if (c == '\n' || c == 3 || !canon) {
            FlushSubordinateLineBuffer(node);
        }
    }
}

// "THE STDIN LINE BUFFER"
static int SubordinateRead(struct vnode* node, struct transfer* tr) {        
    struct sub_data* internal = (struct sub_data*) node->data;
    struct master_data* master_internal = (struct master_data*) internal->master->data;

    if (tr->length_remaining == 0) {
        return 0;
    }

    uint8_t c;
    MailboxGet(master_internal->flushed_buffer, -1, &c);
    PerformTransfer(&c, tr, 1);

    while (tr->length_remaining > 0 && MailboxGet(master_internal->flushed_buffer, 0, (uint8_t*) &c) == 0) {
        PerformTransfer(&c, tr, 1);
    }

    return 0;
}

// "WRITING TO STDOUT"
static int SubordinateWrite(struct vnode* node, struct transfer* tr) {
    struct sub_data* internal = (struct sub_data*) node->data;
    struct master_data* master_internal = (struct master_data*) internal->master->data;

    while (tr->length_remaining > 0) {
        char c;
        int err = PerformTransfer(&c, tr, 1);
        if (err) {
            return err;
        }

        MailboxAdd(master_internal->display_buffer, -1, c);
    }
    
    return 0;
}

static const struct vnode_operations master_operations = {
    .read           = MasterRead,
    .write          = MasterWrite,
};

static const struct vnode_operations subordinate_operations = {
    .read           = SubordinateRead,
    .write          = SubordinateWrite,
};

void CreatePseudoTerminal(struct vnode** master, struct vnode** subordinate) {
    struct stat st = (struct stat) {
        .st_mode =  S_IFCHR | S_IRWXU | S_IRWXG | S_IRWXO,
        .st_nlink = 1,
    };
    struct vnode* m = CreateVnode(master_operations, st);
    struct vnode* s = CreateVnode(subordinate_operations, st);
    
    struct master_data* m_data = AllocHeap(sizeof(struct master_data));
    struct sub_data* s_data = AllocHeap(sizeof(struct sub_data));

    m_data->subordinate = s;
    m_data->display_buffer = MailboxCreate(INTERNAL_BUFFER_SIZE);
    m_data->keybrd_buffer = MailboxCreate(INTERNAL_BUFFER_SIZE);
    m_data->flushed_buffer = MailboxCreate(FLUSHED_BUFFER_SIZE);
    m_data->line_processing_thread = CreateThread(LineProcessor, (void*) s, GetVas(), "line processor");

    s_data->master = m;
    s_data->termios.c_lflag = ICANON | ECHO;

    m->data = m_data;
    s->data = s_data;
    *master = m;
    *subordinate = s;
}
