#include <syscall.h>
#include <log.h>
#include <errno.h>
#include <_syscallnum.h>

typedef int (*system_call_t)(size_t, size_t, size_t, size_t, size_t);

const char* syscall_names[_SYSCALL_NUM_ENTRIES] = {
	[SYSCALL_YIELD] = "yield",
	[SYSCALL_TERMINATE] = "terminate",
	[SYSCALL_MAPVIRT] = "map_virt",
	[SYSCALL_UNMAPVIRT] = "unmap_virt",
	[SYSCALL_OPEN] = "open",
	[SYSCALL_READ] = "read",
	[SYSCALL_WRITE] = "write",
	[SYSCALL_CLOSE] = "close",
	[SYSCALL_SEEK] = "seek",
	[SYSCALL_DUP] = "dup",
	[SYSCALL_TELL] = "tell",
};

static const system_call_t system_call_table[_SYSCALL_NUM_ENTRIES] = {
	[SYSCALL_YIELD] = SysYield,
	[SYSCALL_TERMINATE] = SysTerminate,
	[SYSCALL_MAPVIRT] = SysMapVirt,
	[SYSCALL_UNMAPVIRT] = SysUnmapVirt,
	[SYSCALL_OPEN] = SysOpen,
	[SYSCALL_READ] = SysRead,
	[SYSCALL_WRITE] = SysWrite,
	[SYSCALL_CLOSE] = SysClose,
	[SYSCALL_SEEK] = SysSeek,
	[SYSCALL_DUP] = SysDup,
	[SYSCALL_TELL] = SysTell,
};

int HandleSystemCall(int call, size_t a, size_t b, size_t c, size_t d, size_t e) {
	if (call >= _SYSCALL_NUM_ENTRIES) {
		return ENOSYS;
	}

	if (system_call_table[call] == NULL) {
		return ENOSYS;
	}

	return system_call_table[call](a, b, c, d, e);
}