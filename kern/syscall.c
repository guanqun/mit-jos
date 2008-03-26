/* See COPYRIGHT for copyright information. */

#include <inc/x86.h>
#include <inc/error.h>
#include <inc/string.h>
#include <inc/assert.h>

#include <kern/env.h>
#include <kern/pmap.h>
#include <kern/trap.h>
#include <kern/syscall.h>
#include <kern/console.h>
#include <kern/sched.h>

// Print a string to the system console.
// The string is exactly 'len' characters long.
// Destroys the environment on memory errors.
static void
sys_cputs(const char *s, size_t len)
{
	// Check that the user has permission to read memory [s, s+len).
	// Destroy the environment if not.
	// LAB 3: Your code here.
	user_mem_assert(curenv, (void *)s, len, 0);

	// Print the string supplied by the user.
	cprintf("%.*s", len, s);
}

// Read a character from the system console.
// Returns the character.
static int
sys_cgetc(void)
{
	int c;

	// The cons_getc() primitive doesn't wait for a character,
	// but the sys_cgetc() system call does.
	while ((c = cons_getc()) == 0)
		/* do nothing */;

	return c;
}

// Returns the current environment's envid.
static envid_t
sys_getenvid(void)
{
	return curenv->env_id;
}

// Destroy a given environment (possibly the currently running environment).
//
// Returns 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
static int
sys_env_destroy(envid_t envid)
{
	int r;
	struct Env *e;

	if ((r = envid2env(envid, &e, 1)) < 0)
		return r;
	env_destroy(e);
	return 0;
}

// Deschedule current environment and pick a different one to run.
static void
sys_yield(void)
{
	sched_yield();
}

// Allocate a new environment.
// Returns envid of new environment, or < 0 on error.  Errors are:
//	-E_NO_FREE_ENV if no free environment is available.
static envid_t
sys_exofork(void)
{
	// Create the new environment with env_alloc(), from kern/env.c.
	// It should be left as env_alloc created it, except that
	// status is set to ENV_NOT_RUNNABLE, and the register set is copied
	// from the current environment -- but tweaked so sys_exofork
	// will appear to return 0.
	// LAB 4: Your code here.
	struct Env *child;

	if (env_alloc(&child, curenv->env_id) < 0)
		return -E_NO_FREE_ENV;

	child->env_status = ENV_NOT_RUNNABLE;
	child->env_tf = curenv->env_tf;
	// install the pgfault upcall to the child
	child->env_pgfault_upcall = curenv->env_pgfault_upcall;
	// tweak the register eax of the child,
	// thus, the child will look like the return value
	// of the the system call is zero.
	child->env_tf.tf_regs.reg_eax = 0;
	// but notice that the return value of the parent
	// is the env id of the child
	return child->env_id;
}

// Set envid's env_status to status, which must be ENV_RUNNABLE
// or ENV_NOT_RUNNABLE.
//
// Returns 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
//	-E_INVAL if status is not a valid status for an environment.
static int
sys_env_set_status(envid_t envid, int status)
{
  	// Hint: Use the 'envid2env' function from kern/env.c to translate an
  	// envid to a struct Env.
	// You should set envid2env's third argument to 1, which will
	// check whether the current environment has permission to set
	// envid's status.
	// LAB 4: Your code here.
	int r;
	struct Env *task;

	if ((r = envid2env(envid, &task, 1)) < 0)
		return -E_BAD_ENV;

	if (status != ENV_FREE &&
		status != ENV_RUNNABLE &&
		status != ENV_NOT_RUNNABLE)
		return -E_INVAL;

	task->env_status = status;

	return 0;
}

// Set envid's trap frame to 'tf'.
// tf is modified to make sure that user environments always run at code
// protection level 3 (CPL 3) with interrupts enabled.
//
// Returns 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
static int
sys_env_set_trapframe(envid_t envid, struct Trapframe *tf)
{
	// LAB 4: Your code here.
	// Remember to check whether the user has supplied us with a good
	// address!
	panic("sys_set_trapframe not implemented");
}

// Set the page fault upcall for 'envid' by modifying the corresponding struct
// Env's 'env_pgfault_upcall' field.  When 'envid' causes a page fault, the
// kernel will push a fault record onto the exception stack, then branch to
// 'func'.
//
// Returns 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
static int
sys_env_set_pgfault_upcall(envid_t envid, void *func)
{
	// LAB 4: Your code here.
	struct Env *task;

	if (envid2env(envid, &task, 1) < 0)
		return -E_BAD_ENV;

	task->env_pgfault_upcall = func;

	return 0;
}

// Allocate a page of memory and map it at 'va' with permission
// 'perm' in the address space of 'envid'.
// The page's contents are set to 0.
// If a page is already mapped at 'va', that page is unmapped as a
// side effect.
//
// perm -- PTE_U | PTE_P must be set, PTE_AVAIL | PTE_W may or may not be set,
//         but no other bits may be set.
//
// Return 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
//	-E_INVAL if va >= UTOP, or va is not page-aligned.
//	-E_INVAL if perm is inappropriate (see above).
//	-E_NO_MEM if there's no memory to allocate the new page,
//		or to allocate any necessary page tables.
static int
sys_page_alloc(envid_t envid, void *va, int perm)
{
	// Hint: This function is a wrapper around page_alloc() and
	//   page_insert() from kern/pmap.c.
	//   Most of the new code you write should be to check the
	//   parameters for correctness.
	//   If page_insert() fails, remember to free the page you
	//   allocated!
	// LAB 4: Your code here.
	struct Env *task;
	struct Page *page;

	//cprintf("sys_page_alloc: [%08x] .\n", envid);
	if (envid2env(envid, &task, 1) < 0)
		return -E_BAD_ENV;

	if (page_alloc(&page) < 0)
		return -E_NO_MEM;

	if ((unsigned int)va >= UTOP || va != ROUNDDOWN(va, PGSIZE))
		return -E_INVAL;

	// PTE_U and PTE_P must be set
	if (!(perm & PTE_U) || !(perm & PTE_P))
		return -E_INVAL;
	// other bits than PTE_{U,P,W,AVAIL} are set
	if (perm & ((~(PTE_U | PTE_P | PTE_W | PTE_AVAIL)) & 0xfff))
		return -E_INVAL;

	memset(page2kva(page), 0, PGSIZE);
	if (page_insert(task->env_pgdir, page, va, perm) < 0) {
		page_free(page);
		return -E_NO_MEM;
	}

	//cprintf("allocated page: [%08x].\n", page2pa(page));
	return 0;
}

// Map the page of memory at 'srcva' in srcenvid's address space
// at 'dstva' in dstenvid's address space with permission 'perm'.
// Perm has the same restrictions as in sys_page_alloc, except
// that it also must not grant write access to a read-only
// page.
//
// Return 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if srcenvid and/or dstenvid doesn't currently exist,
//		or the caller doesn't have permission to change one of them.
//	-E_INVAL if srcva >= UTOP or srcva is not page-aligned,
//		or dstva >= UTOP or dstva is not page-aligned.
//	-E_INVAL is srcva is not mapped in srcenvid's address space.
//	-E_INVAL if perm is inappropriate (see sys_page_alloc).
//	-E_INVAL if (perm & PTE_W), but srcva is read-only in srcenvid's
//		address space.
//	-E_NO_MEM if there's no memory to allocate the new page,
//		or to allocate any necessary page tables.
static int
sys_page_map(envid_t srcenvid, void *srcva,
	     envid_t dstenvid, void *dstva, int perm)
{
	// Hint: This function is a wrapper around page_lookup() and
	//   page_insert() from kern/pmap.c.
	//   Again, most of the new code you write should be to check the
	//   parameters for correctness.
	//   Use the third argument to page_lookup() to
	//   check the current permissions on the page.
	// LAB 4: Your code here.
	struct Env *srcenv, *dstenv;
	struct Page *page;
	pte_t *srcpte, *dstpte;

	if (envid2env(srcenvid, &srcenv, 1) < 0 ||
		envid2env(dstenvid, &dstenv, 1) < 0)
		return -E_BAD_ENV;

	if ((unsigned int)srcva >= UTOP || srcva != ROUNDDOWN(srcva, PGSIZE) ||
		(unsigned int)dstva >= UTOP || dstva != ROUNDDOWN(dstva, PGSIZE))
		return -E_INVAL;

	if ((page = page_lookup(srcenv->env_pgdir, srcva, &srcpte)) == NULL)
		return -E_INVAL;

	// PTE_U and PTE_P must be set
	if (!(perm & PTE_U) || !(perm & PTE_P))
		return -E_INVAL;
	// other bits than PTE_{U,P,W,AVAIL} are set
	if (perm & ((~(PTE_U | PTE_P | PTE_W | PTE_AVAIL)) & 0xfff))
		return -E_INVAL;
	// perm has PTE_W, but scrpte is read-only.
	if ((perm & PTE_W) && !(*srcpte & PTE_W))
		return -E_INVAL;

	if (page_insert(dstenv->env_pgdir, page, dstva, perm) < 0)
		return -E_NO_MEM;
	/*cprintf("map [%08x] %08x(%08x) -> [%08x] %08x(%08x) perm: %x\n",
			srcenv->env_id, srcva, *srcpte,
			dstenv->env_id, dstva, *dstpte, perm);*/

	return 0;
}

// Unmap the page of memory at 'va' in the address space of 'envid'.
// If no page is mapped, the function silently succeeds.
//
// Return 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
//	-E_INVAL if va >= UTOP, or va is not page-aligned.
static int
sys_page_unmap(envid_t envid, void *va)
{
	// Hint: This function is a wrapper around page_remove().
	// LAB 4: Your code here.
	struct Env *task;

	if (envid2env(envid, &task, 1) < 0)
		return -E_BAD_ENV;

	if ((unsigned int)va >= UTOP || va != ROUNDDOWN(va, PGSIZE))
		return -E_INVAL;

	page_remove(task->env_pgdir, va);

	return 0;
}

// Try to send 'value' to the target env 'envid'.
// If va != 0, then also send page currently mapped at 'va',
// so that receiver gets a duplicate mapping of the same page.
//
// The send fails with a return value of -E_IPC_NOT_RECV if the
// target has not requested IPC with sys_ipc_recv.
//
// Otherwise, the send succeeds, and the target's ipc fields are
// updated as follows:
//    env_ipc_recving is set to 0 to block future sends;
//    env_ipc_from is set to the sending envid;
//    env_ipc_value is set to the 'value' parameter;
//    env_ipc_perm is set to 'perm' if a page was transferred, 0 otherwise.
// The target environment is marked runnable again, returning 0
// from the paused ipc_recv system call.
//
// If the sender sends a page but the receiver isn't asking for one,
// then no page mapping is transferred, but no error occurs.
// The ipc doesn't happen unless no errors occur.
//
// Returns 0 on success where no page mapping occurs,
// 1 on success where a page mapping occurs, and < 0 on error.
// Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist.
//		(No need to check permissions.)
//	-E_IPC_NOT_RECV if envid is not currently blocked in sys_ipc_recv,
//		or another environment managed to send first.
//	-E_INVAL if srcva < UTOP but srcva is not page-aligned.
//	-E_INVAL if srcva < UTOP and perm is inappropriate
//		(see sys_page_alloc).
//	-E_INVAL if srcva < UTOP but srcva is not mapped in the caller's
//		address space.
//	-E_NO_MEM if there's not enough memory to map srcva in envid's
//		address space.
static int
sys_ipc_try_send(envid_t envid, uint32_t value, void *srcva, unsigned perm)
{
	// LAB 4: Your code here.
	struct Env *target;
	struct Page *page;
	pte_t *pte;
	int r, ret = 0;

	if ((r = envid2env(envid, &target, 0)) < 0)
		return -E_BAD_ENV;

	if (!target->env_ipc_recving)
		return -E_IPC_NOT_RECV;

	// srcva is not null, then
	// we need to map it, thus sharing the map
	if (srcva) {
		if ((unsigned int)srcva >= UTOP)
			return -E_INVAL;

		if (srcva != ROUNDDOWN(srcva, PGSIZE))
			return -E_INVAL;

		if ((page = page_lookup(curenv->env_pgdir, srcva, &pte)) == NULL)
			return -E_INVAL;

		// PTE_U and PTE_P must be set
		if (!(perm & PTE_U) || !(perm & PTE_P))
			return -E_INVAL;
		// other bits than PTE_{U,P,W,AVAIL} are set
		if (perm & ((~(PTE_U | PTE_P | PTE_W | PTE_AVAIL)) & 0xfff))
			return -E_INVAL;
		// perm has PTE_W, but scrpte is read-only.
		if ((perm & PTE_W) && !(*pte & PTE_W))
			return -E_INVAL;

		if (target->env_ipc_dstva == srcva &&
			page_insert(target->env_pgdir, page, srcva, perm) < 0)
			return -E_NO_MEM;

		ret = 1;
	}

	target->env_ipc_recving = 0;
	target->env_ipc_value = value;
	target->env_ipc_from = curenv->env_id;
	if (ret)
		target->env_ipc_perm = perm;
	else
		target->env_ipc_perm = 0;
	target->env_status = ENV_RUNNABLE;

	return ret;
}

// Block until a value is ready.  Record that you want to receive
// using the env_ipc_recving and env_ipc_dstva fields of struct Env,
// mark yourself not runnable, and then give up the CPU.
//
// If 'dstva' is < UTOP, then you are willing to receive a page of data.
// 'dstva' is the virtual address at which the sent page should be mapped.
//
// This function only returns on error, but the system call will eventually
// return 0 on success.
// Return < 0 on error.  Errors are:
//	-E_INVAL if dstva < UTOP but dstva is not page-aligned.
static int
sys_ipc_recv(void *dstva)
{
	// LAB 4: Your code here.
	if ((unsigned int)dstva >= UTOP || dstva != ROUNDDOWN(dstva, PGSIZE))
		return -E_INVAL;

	curenv->env_ipc_dstva = dstva;
	curenv->env_ipc_recving = 1;
	curenv->env_status = ENV_NOT_RUNNABLE;
	// set the return value to be zero,
	// it is necessary, because the 'return' statement
	// after 'sched_yield' will never be executed,
	// actually it is skipped.
	curenv->env_tf.tf_regs.reg_eax = 0;
	// give up the CPU
	sched_yield();
	return 0;
}

static int
sys_phy_page(envid_t envid, void *va)
{
	struct Env *task;
	struct Page *page;
	pte_t *pte;

	if (envid2env(envid, &task, 1) < 0)
		return -E_BAD_ENV;

	page = page_lookup(task->env_pgdir, va, &pte);
	if (page == 0)
		return 0;

	return *pte;
}

// Dispatches to the correct kernel function, passing the arguments.
int32_t
syscall(uint32_t syscallno, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5)
{
	// Call the function corresponding to the 'syscallno' parameter.
	// Return any appropriate return value.
	// LAB 3: Your code here.
	int ret = 0;

	switch (syscallno) {
	case SYS_cputs:
		sys_cputs((const char *)a1, (size_t)a2);
		break;
	case SYS_cgetc:
		ret = sys_cgetc();
		break;
	case SYS_getenvid:
		ret = sys_getenvid();
		break;
	case SYS_env_destroy:
		ret = sys_env_destroy((envid_t)a1);
		break;
	case SYS_page_alloc:
		ret = sys_page_alloc((envid_t)a1, (void *)a2, (int)a3);
		break;
	case SYS_page_map:
		ret = sys_page_map((envid_t)a1, (void *)a2,
						   (envid_t)a3, (void *)a4, (int)a5);
		break;
	case SYS_page_unmap:
		ret = sys_page_unmap((envid_t)a1, (void *)a2);
		break;
	case SYS_exofork:
		ret = sys_exofork();
		break;
	case SYS_env_set_status:
		ret = sys_env_set_status((envid_t)a1, (int)a2);
		break;
	case SYS_env_set_pgfault_upcall:
		ret = sys_env_set_pgfault_upcall((envid_t)a1, (void *)a2);
		break;
	case SYS_yield:
		sys_yield();
		break;
	case SYS_phy_page:
		ret = sys_phy_page((envid_t)a1, (void *)a2);
		break;
	case SYS_ipc_try_send:
		ret = sys_ipc_try_send((envid_t)a1, (uint32_t)a2,
							   (void *)a3, (int)a4);
		break;
	case SYS_ipc_recv:
		ret = sys_ipc_recv((void *)a1);
		break;
	default:
		// NSYSCALLS
		ret = -E_INVAL;
		break;
	}

	return ret;
}

