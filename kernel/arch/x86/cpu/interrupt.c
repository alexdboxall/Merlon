
#include <machine/regs.h>
#include <machine/interrupt.h>
#include <machine/pic.h>
#include <log.h>
#include <irq.h>
#include <irql.h>
#include <virtual.h>

static bool ready_for_irqs = false;

static int GetRequiredIrql(int irq_num) {
    if (irq_num == PIC_IRQ_BASE + 0) {
        return IRQL_TIMER;
    } else {
        return IRQL_DRIVER + irq_num - PIC_IRQ_BASE;
    }
}

void x86HandleInterrupt(struct x86_regs* r) {
    int num = r->int_no;

    if (num >= PIC_IRQ_BASE && num < PIC_IRQ_BASE + 16) {
        RespondToIrq(num, GetRequiredIrql(num), r);

    } else if (num == 14) {
        extern size_t x86GetCr2();

        int type = 0;
        if (r->err_code & 1) {
            type |= VM_READ;
        }
        if (r->err_code & 2) {
            type |= VM_WRITE;
        }
        if (r->err_code & 4) {
            type |= VM_USER;
        }
        if (r->err_code & 16) {
            type |= VM_EXEC;
        }

        LogWriteSerial("Page fault: cr2 0x%X, eip 0x%X, nos-err 0x%X\n", x86GetCr2(), r->eip, type);

        HandleVirtFault(x86GetCr2(), type);
    }

    /*
    if (protection fault, etc. || page fault unhandled) {
        RespondToUnhandledFault
    } else if (syscall) {
        RespondToSyscall(...)
    }

    */
}

void ArchSendEoi(int irq_num) {
    SendPicEoi(irq_num);
}

void ArchSetIrql(int irql) {
    if (irql == IRQL_HIGH || irql == IRQL_TIMER || !x86IsReadyForIrqs()) {
        /*
         * Interrupts stay off.
         */
        return;
    }
    
    if (irql >= IRQL_DRIVER) {
        // TODO: mask things.
    }

    ArchEnableInterrupts();
}

bool x86IsReadyForIrqs(void) {
    return ready_for_irqs;
}

void x86MakeReadyForIrqs(void) {
    ready_for_irqs = true;
    RaiseIrql(GetIrql());
}