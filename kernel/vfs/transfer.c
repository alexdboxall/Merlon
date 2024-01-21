
#include <transfer.h>
#include <assert.h>
#include <string.h>
#include <errno.h>
#include <virtual.h>
#include <arch.h>
#include <log.h>

static int ValidateCopy(const void* user_addr, size_t size, bool write) {
    size_t initial_address = (size_t) user_addr;
    size_t final_address = initial_address + size;

    if (initial_address < ARCH_USER_AREA_BASE || initial_address >= ARCH_USER_AREA_LIMIT) {
        return EINVAL;
    }
    if (final_address < initial_address) {      /* Checks for overflow */
        return EINVAL;
    }
    if (final_address < ARCH_USER_AREA_BASE || final_address >= ARCH_USER_AREA_LIMIT) {
        return EINVAL;
    }

    /*
     * We must now check if the USER (and possibly WRITE) bits are set on the memory pages
     * being accessed.
     */
    size_t initial_page = initial_address / ARCH_PAGE_SIZE;
    size_t pages = BytesToPages(size);

    for (size_t i = 0; i < pages; ++i) {
        size_t page = initial_page + i;
        size_t permissions = GetVirtPermissions(page * ARCH_PAGE_SIZE);

        if (permissions == 0) {
            return EINVAL;
        }
        if (!(permissions & VM_READ)) {
            return EINVAL;
        }
        if (!(permissions & VM_USER)) {
            return EINVAL;
        }
        if (write && !(permissions & VM_WRITE)) {
            return EINVAL;
        }
        if (write && (permissions & VM_EXEC)) {
            return EINVAL;
        }
    }
    
    return 0;
}

static int CopyIntoKernel(void* kernel_addr, const void* user_addr, size_t size) {
    int status = ValidateCopy(user_addr, size, false);
    if (status != 0) {
        return status;
    }

    inline_memcpy(kernel_addr, user_addr, size);
    return 0;
}

static int CopyOutOfKernel(const void* kernel_addr, void* user_addr, size_t size) {
    int status = ValidateCopy(user_addr, size, true);
    if (status != 0) {
        return status;
    }

    inline_memcpy(user_addr, kernel_addr, size);
    return 0;
}

int PerformTransfer(void* trusted_buffer, struct transfer* untrusted_buffer, uint64_t len) { 
    assert(trusted_buffer != NULL);
    assert(untrusted_buffer != NULL && untrusted_buffer->address != NULL);
    assert(untrusted_buffer->direction == TRANSFER_READ || untrusted_buffer->direction == TRANSFER_WRITE);

    size_t amount_to_copy = MIN(len, untrusted_buffer->length_remaining);
    if (amount_to_copy == 0) {
        return 0;
    }

    if (untrusted_buffer->type == TRANSFER_INTRA_KERNEL) {
        if (untrusted_buffer->direction == TRANSFER_READ) {
            memmove(untrusted_buffer->address, trusted_buffer, amount_to_copy);
        
        } else {
            memmove(trusted_buffer, untrusted_buffer->address, amount_to_copy);
        } 

    } else {
        int result;

        /*
        * This is from the kernel's perspective of the operations.
        */
        if (untrusted_buffer->direction == TRANSFER_READ) {
            result = CopyOutOfKernel((const void*) trusted_buffer, untrusted_buffer->address, amount_to_copy);
            
        } else {
            result = CopyIntoKernel(trusted_buffer, (const void*) untrusted_buffer->address, amount_to_copy);
        }

        if (result != 0) {
            return result;
        }
    }

    untrusted_buffer->length_remaining -= amount_to_copy;
    untrusted_buffer->offset += amount_to_copy;
    untrusted_buffer->address = ((uint8_t*) untrusted_buffer->address) + amount_to_copy;

    return 0;
}  

int WriteStringToUsermode(const char* trusted_string, char* untrusted_buffer, uint64_t max_length) {
    struct transfer tr = CreateTransferWritingToUser(untrusted_buffer, max_length, 0);
    int result;

    /*
     * Limit the size of the string by the maximimum length. We use <, and a -1 in the other case,
     * as we need to ensure the null terminator fits.
     */
    uint64_t size = strlen(trusted_string) < max_length ? strlen(trusted_string) : max_length - 1;
    result = PerformTransfer((void*) trusted_string, &tr, size);

    if (result != 0) {
        return result;
    }
    
    uint8_t zero = 0;
    return PerformTransfer(&zero, &tr, 1);
}

int ReadStringFromUsermode(char* trusted_buffer, const char* untrusted_string, uint64_t max_length) {
    struct transfer tr = CreateTransferReadingFromUser(untrusted_string, max_length, 0);
    size_t i = 0;

    while (max_length-- > 1) {
        char c = 0;
        int result = PerformTransfer(&c, &tr, 1);
        if (result != 0) {
            return result;
        }
        trusted_buffer[i++] = c;
        if (c == 0) {
            break;
        }
    }
    
    trusted_buffer[i] = 0;
    return 0;
}

int WriteWordToUsermode(size_t* location, size_t value) {
    struct transfer io = CreateTransferWritingToUser(location, sizeof(size_t), 0);
    int res = PerformTransfer(&value, &io, sizeof(size_t));
    if (io.length_remaining != 0) {
        return EINVAL;
    }
    return res;
}

int ReadWordFromUsermode(size_t* location, size_t* output) {
    struct transfer io = CreateTransferReadingFromUser(location, sizeof(size_t), 0);
    int res = PerformTransfer(output, &io, sizeof(size_t));
    if (io.length_remaining != 0) {
        return EINVAL;
    }
    return res;
}

static struct transfer CreateTransfer(void* addr, uint64_t length, uint64_t offset, int direction, int type) {
    return (struct transfer) {
        .address = addr, .direction = direction, .length_remaining = length,
        .offset = offset, .type = type, .blockable = true
    };
}

struct transfer CreateKernelTransfer(void* addr, uint64_t length, uint64_t offset, int direction) {
    return CreateTransfer(addr, length, offset, direction, TRANSFER_INTRA_KERNEL);
}

/*
 * When we "write to the user", we are doing so because the user is trying 
 * to *read* from the kernel. i.e. someone is doing an "untrusted read" of 
 * kernel data (i.e. a TRANSFER_READ). Likewise with the vice verse once.
 */
struct transfer CreateTransferWritingToUser(void* addr, uint64_t length, uint64_t offset) {
    return CreateTransfer(
        addr, length, offset, TRANSFER_READ, TRANSFER_USERMODE
    );
}

struct transfer CreateTransferReadingFromUser(const void* addr, uint64_t length, uint64_t offset) {
    return CreateTransfer(
        (void*) addr, length, offset, TRANSFER_WRITE, TRANSFER_USERMODE
    );
}
