// implement fork from user space

#include <inc/string.h>
#include <inc/lib.h>

// PTE_COW marks copy-on-write page table entries.
// It is one of the bits explicitly allocated to user processes (PTE_AVAIL).
#define PTE_COW		0x800

extern void _pgfault_upcall(void);

//
// Custom page fault handler - if faulting page is copy-on-write,
// map in our own private writable copy.
//
static void
pgfault(struct UTrapframe *utf)
{
	void *addr = (void *) utf->utf_fault_va;
	uint32_t err = utf->utf_err;
	int r;

	// Check that the faulting access was (1) a write, and (2) to a
	// copy-on-write page.  If not, panic.
	// Hint:
	//   Use the read-only page table mappings at vpt
	//   (see <inc/memlayout.h>).
	// LAB 4: Your code here.
	if (!((err & FEC_WR) && (vpt[VPN((unsigned int)addr)] & PTE_COW)))
		panic("not a write and not to a COW page, addr: %x, err: %x", addr, err & 7);

	// Allocate a new page, map it at a temporary location (PFTEMP),
	// copy the data from the old page to the new page, then move the new
	// page to the old page's address.
	// Hint:
	//   You should make three system calls.
	//   No need to explicitly delete the old page's mapping.
	// LAB 4: Your code here.
	if ((r = sys_page_alloc(0, (void *)PFTEMP, PTE_U | PTE_W | PTE_P)) < 0)
		panic("sys_page_alloc error: %e", r);

	// copy the actual content
	memmove((void *)PFTEMP, ROUNDDOWN(addr, PGSIZE), PGSIZE);
	if ((r = sys_page_map(0, (void *)PFTEMP, 0, ROUNDDOWN(addr, PGSIZE),
						  PTE_U | PTE_W | PTE_P)) < 0)
		panic("sys_page_map error: %e", r);

	if ((r = sys_page_unmap(0, (void *)PFTEMP)) < 0)
		panic("sys_page_unmap error: %e", r);
}

//
// Map our virtual page pn (address pn*PGSIZE) into the target envid
// at the same virtual address.  If the page is writable or copy-on-write,
// the new mapping must be created copy-on-write, and then our mapping must be
// marked copy-on-write as well.  (Exercise: Why mark ours copy-on-write again
// if it was already copy-on-write?)
//
// Returns: 0 on success, < 0 on error.
// It is also OK to panic on error.
// 
static int
duppage(envid_t envid, unsigned pn)
{
	int r;
	void *addr;
	pte_t pte;
	// LAB 4: Your code here.

	pte = vpt[pn];
	if ((pte & PTE_W) || (pte & PTE_COW)) {
		if ((r = sys_page_map(0, (void *)(pn*PGSIZE),
							  envid, (void *)(pn*PGSIZE),
							  PTE_U | PTE_P | PTE_COW)) < 0)
			panic("sys_page_map error: %e", r);

		if ((r = sys_page_map(envid, (void *)(pn*PGSIZE),
							  0, (void *)(pn*PGSIZE),
							  PTE_U | PTE_P | PTE_COW)) < 0)
			panic("sys_page_map error: %e", r);
	} else {
		if ((r = sys_page_map(0, (void *)(pn*PGSIZE),
							  envid, (void *)(pn*PGSIZE),
							  PTE_W | PTE_P | PTE_U)) < 0)
			panic("sys_page_map error: %e", r);
	}

	return 0;
}

//
// User-level fork with copy-on-write.
// Set up our page fault handler appropriately.
// Create a child.
// Copy our address space and page fault handler setup to the child.
// Then mark the child as runnable and return.
//
// Returns: child's envid to the parent, 0 to the child, < 0 on error.
// It is also OK to panic on error.
//
// Hint:
//   Use vpd, vpt, and duppage.
//   Remember to fix "env" and the user exception stack in the child process.
//   Neither user exception stack should ever be marked copy-on-write,
//   so you must allocate a new page for the child's user exception stack.
//
envid_t
fork(void)
{
	// LAB 4: Your code here.
	envid_t envid;
	int r;
	int pn, i;

	// install the page fault handler
	set_pgfault_handler(pgfault);

	envid = sys_exofork();
	if (envid < 0)
		panic("fork error");

	if (envid == 0) {
		// we are the child
		env = &envs[ENVX(sys_getenvid())];
		return 0;
	}

	// we are the parent
	pn = UTOP / PGSIZE - 1;
	while (--pn >= 0)
		if ((vpd[pn >> 10] & PTE_P) && (vpt[pn] & PTE_P))
			duppage(envid, pn);

	// allocate a new page for child - user exception stack
	if ((r = sys_page_alloc(envid,
							(void *)(UXSTACKTOP-PGSIZE),
							PTE_W |PTE_U |PTE_P)) < 0)
		panic("sys_page_alloc error: %e", r);

	// setup the child's page fault entry point
	if ((r = sys_env_set_pgfault_upcall(envid, _pgfault_upcall)) < 0)
		panic("set_pgfault_handler: set pgfault upcall error: %e", r);

	// fire the engine
	if ((r = sys_env_set_status(envid, ENV_RUNNABLE)) < 0)
		panic("sys_env_set_status: %e", r);

	return envid;
}

// Challenge!
int
sfork(void)
{
	panic("sfork not implemented");
	return -E_INVAL;
}
