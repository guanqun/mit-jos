#include <inc/lib.h>
#include <inc/elf.h>

#define UTEMP2USTACK(addr)	((void*) (addr) + (USTACKTOP - PGSIZE) - UTEMP)
#define UTEMP2			(UTEMP + PGSIZE)
#define UTEMP3			(UTEMP2 + PGSIZE)

// Helper functions for spawn.
static int init_stack(envid_t child, const char **argv, uintptr_t *init_esp);
static int copy_shared_pages(envid_t child);
static int load_elf_to_child(int fd, struct Proghdr *ph, envid_t child);

// Spawn a child process from a program image loaded from the file system.
// prog: the pathname of the program to run.
// argv: pointer to null-terminated array of pointers to strings,
// 	 which will be passed to the child as its command-line arguments.
// Returns child envid on success, < 0 on failure.
int
spawn(const char *prog, const char **argv)
{
	struct Trapframe child_tf;
	envid_t child;
	struct Elf elf;
	struct Proghdr ph;
	int r, i, pn;
	int fd;
	uintptr_t esp;

	// Insert your code, following approximately this procedure:
	//
	//   - Open the program file.
	if ((fd = open(prog, O_RDONLY)) < 0)
		return fd;
	//
	//   - Read the ELF header, as you have before, and sanity check its
	//     magic number.  (Check out your load_icode!)
	if (read(fd, (void *)&elf, sizeof(elf)) != sizeof(elf)) {
		close(fd);
		return -E_INVAL;
	}
	if (elf.e_magic != ELF_MAGIC) {
		close(fd);
		return -E_INVAL;
	}
	//
	//   - Use sys_exofork() to create a new environment.
	if ((child = sys_exofork()) < 0) {
		close(fd);
		return child;
	}
	//
	//   - Set child_tf to an initial struct Trapframe for the child.
	//     Hint: The sys_exofork() system call has already created
	//     a good basis, in envs[ENVX(child)].env_tf.
	//     Hint: You must do something with the program's entry point.
	//     What?  (See load_icode!)
	//
	//   - Call the init_stack() function above to set up
	//     the initial stack page for the child environment.
	if ((r = init_stack(child, argv, &esp)) < 0) {
		close(fd);
		return r;
	}
	child_tf = envs[ENVX(child)].env_tf;
	child_tf.tf_eip = elf.e_entry;
	child_tf.tf_esp = esp;
	//
	//   - Map all of the program's segments that are of p_type
	//     ELF_PROG_LOAD into the new environment's address space.
	//     Use the p_flags field in the Proghdr for each segment
	//     to determine how to map the segment:
	//
	//	* If the ELF flags do not include ELF_PROG_FLAG_WRITE,
	//	  then the segment contains text and read-only data.
	//	  Use read_map() to read the contents of this segment,
	//	  and map the pages it returns directly into the child
	//        so that multiple instances of the same program
	//	  will share the same copy of the program text.
	//        Be sure to map the program text read-only in the child.
	//        Read_map is like read but returns a pointer to the data in
	//        *blk rather than copying the data into another buffer.
	//
	//	* If the ELF segment flags DO include ELF_PROG_FLAG_WRITE,
	//	  then the segment contains read/write data and bss.
	//	  As with load_icode() in Lab 3, such an ELF segment
	//	  occupies p_memsz bytes in memory, but only the FIRST
	//	  p_filesz bytes of the segment are actually loaded
	//	  from the executable file - you must clear the rest to zero.
	//        For each page to be mapped for a read/write segment,
	//        allocate a page in the parent temporarily at UTEMP,
	//        read() the appropriate portion of the file into that page
	//	  and/or use memset() to zero non-loaded portions.
	//	  (You can avoid calling memset(), if you like, if
	//	  page_alloc() returns zeroed pages already.)
	//        Then insert the page mapping into the child.
	//        Look at init_stack() for inspiration.
	//        Be sure you understand why you can't use read_map() here.
	//
	//     Note: None of the segment addresses or lengths above
	//     are guaranteed to be page-aligned, so you must deal with
	//     these non-page-aligned values appropriately.
	//     The ELF linker does, however, guarantee that no two segments
	//     will overlap on the same page; and it guarantees that
	//     PGOFF(ph->p_offset) == PGOFF(ph->p_va).
	for (i = 0; i < elf.e_phnum; i++) {
		seek(fd, elf.e_phoff + sizeof(struct Proghdr) * i);
		read(fd, &ph, sizeof(struct Proghdr));

		if (ph.p_type == ELF_PROG_LOAD) {
			r = load_elf_to_child(fd, &ph, child);
			if (r < 0) {
				cprintf("load elf error: %e", r);
				close(fd);
				return r;
			}
		}
	}
	close(fd);
	// loop through all the page table entries
	pn = UTOP / PGSIZE - 1;
	while (--pn >= 0)
		if (!(vpd[pn >> 10] & PTE_P))
			pn = (pn >> 10) << 10;
		else if ((vpt[pn] & PTE_P) && (vpt[pn] & PTE_SHARE)) {
			// propagate the PTE_SHARE pages
			r = sys_page_map(0, (void *)(pn*PGSIZE),
					 child, (void *)(pn*PGSIZE),
					 vpt[pn] & PTE_USER);
			if (r < 0)
				return r;
		}
	//
	//   - Call sys_env_set_trapframe(child, &child_tf) to set up the
	//     correct initial eip and esp values in the child.
	if ((r = sys_env_set_trapframe(child, &child_tf)) < 0)
		return r;
	//
	//   - Start the child process running with sys_env_set_status().
	if ((r = sys_env_set_status(child, ENV_RUNNABLE)) < 0)
		return r;

	return child;
}

// Spawn, taking command-line arguments array directly on the stack.
int
spawnl(const char *prog, const char *arg0, ...)
{
	return spawn(prog, &arg0);
}


// Set up the initial stack page for the new child process with envid 'child'
// using the arguments array pointed to by 'argv',
// which is a null-terminated array of pointers to null-terminated strings.
//
// On success, returns 0 and sets *init_esp
// to the initial stack pointer with which the child should start.
// Returns < 0 on failure.
static int
init_stack(envid_t child, const char **argv, uintptr_t *init_esp)
{
	size_t string_size;
	char *string_store;
	uintptr_t *argv_store;
	int argc, i, r, len;

	// Count the number of arguments (argc)
	// and the total amount of space needed for strings (string_size).
	string_size = 0;
	for (argc = 0; argv[argc] != 0; argc++)
		string_size += strlen(argv[argc]) + 1;

	// Determine where to place the strings and the argv array.
	// Set up pointers into the temporary page 'UTEMP'; we'll map a page
	// there later, then remap that page into the child environment
	// at (USTACKTOP - PGSIZE).
	// strings is the topmost thing on the stack.
	string_store = (char*) UTEMP + PGSIZE - string_size;
	// argv is below that.  There's one argument pointer per argument, plus
	// a null pointer.
	argv_store = (uintptr_t*) (ROUNDDOWN(string_store, 4) - 4 * (argc + 1));
	
	// Make sure that argv, strings, and the 2 words that hold 'argc'
	// and 'argv' themselves will all fit in a single stack page.
	if ((void*) (argv_store - 2) < (void*) UTEMP)
		return -E_NO_MEM;

	// Allocate the single stack page at UTEMP.
	if ((r = sys_page_alloc(0, (void*) UTEMP, PTE_P|PTE_U|PTE_W)) < 0)
		return r;

	// Replace this with your code to:
	//
	//	* Initialize 'argv_store[i]' to point to argument string i,
	//	  for all 0 <= i < argc.
	//	  Also, copy the argument strings from 'argv' into the
	//	  newly-allocated stack page.
	//	  Hint: Copy the argument strings into string_store.
	//	  Hint: Make sure that argv_store uses addresses valid in the
	//	  CHILD'S environment!  The string_store variable itself
	//	  points into page UTEMP, but the child environment will have
	//	  this page mapped at USTACKTOP - PGSIZE.  Check out the
	//	  UTEMP2USTACK macro defined above.
	//
	//	* Set 'argv_store[argc]' to 0 to null-terminate the args array.
	//
	//	* Push two more words onto the child's stack below 'args',
	//	  containing the argc and argv parameters to be passed
	//	  to the child's umain() function.
	//	  argv should be below argc on the stack.
	//	  (Again, argv should use an address valid in the child's
	//	  environment.)
	//
	//	* Set *init_esp to the initial stack pointer for the child,
	//	  (Again, use an address valid in the child's environment.)
	//
	for (i = 0; i < argc; i++) {
		len = strlen(argv[i]);
		// copy the arguments to string_store
		memmove(string_store, argv[i], len);
		// make it null-terminated
		string_store[len] = 0;
		// make the right reference
		argv_store[i] = UTEMP2USTACK(string_store);
		// advance the string_store pointer
		string_store += len + 1;
	}
	argv_store[argc] = 0;
	// push 'argv' ptr onto the stack
	argv_store[-1] = (uintptr_t)UTEMP2USTACK(argv_store);
	// push 'argc' onto the stack
	argv_store[-2] = argc;
	argv_store -= 2;
	// return the right esp value to the child process
	*init_esp = UTEMP2USTACK(argv_store);
	// After completing the stack, map it into the child's address space
	// and unmap it from ours!
	if ((r = sys_page_map(0, UTEMP,
			      child, (void*) (USTACKTOP - PGSIZE),
			      PTE_P | PTE_U | PTE_W)) < 0) {
		sys_page_unmap(0, UTEMP);
		return r;
	}
	if ((r = sys_page_unmap(0, UTEMP)) < 0)
		return r;

	return 0;
}

static int load_elf_to_child(int fd, struct Proghdr *ph, envid_t child)
{
	uint32_t offset, cur_va;
	int r, size;
	void *blk;

	if (ph->p_flags & ELF_PROG_FLAG_WRITE) {
		// read/write data here
		offset = ph->p_offset;
		cur_va = ph->p_va;

		while (cur_va < ph->p_va + ph->p_filesz) {
			// calculate 'size', if it exceeds p_filesz,
			// then, adjust the 'size'.
			size = PGSIZE - (offset & 0xfff);
			if (cur_va + size >= ph->p_va + ph->p_filesz)
				size = ph->p_va + ph->p_filesz - cur_va;

			if ((r = sys_page_alloc(0,
						(void *)UTEMP,
						PTE_P |PTE_U |PTE_W)) < 0)
				return r;
			if ((r = seek(fd, ROUNDDOWN(offset, PGSIZE))) < 0)
				return r;
			if ((r = read(fd,
				      (void *)UTEMP + (offset & 0xfff),
				      size)) < 0)
				return r;
			if ((r = sys_page_map(0,
					      (void *)UTEMP,
					      child,
					      (void *)ROUNDDOWN(cur_va, PGSIZE),
					      PTE_P |PTE_U |PTE_W)) < 0)
				return r;
			if ((r = sys_page_unmap(0, (void *)UTEMP)) < 0)
				return r;

			offset += size;
			cur_va += size;
		}
		// zero the region from ph->p_filesz to ph->p_memsz
		cur_va = ROUNDUP(cur_va, PGSIZE);
		offset = ROUNDUP(offset, PGSIZE);
		while (cur_va < ph->p_va + ph->p_memsz) {
			size = PGSIZE - (offset & 0xfff);
			if (cur_va + size >= ph->p_va + ph->p_memsz)
				size = ph->p_va + ph->p_memsz - cur_va;

			if ((r = sys_page_alloc(0,
						(void *)UTEMP,
						PTE_P |PTE_U |PTE_W)) < 0)
				return r;

			// zero them
			memset((void *)UTEMP + (offset & 0xfff), 0, size);

			if ((r = sys_page_map(0,
					      (void *)UTEMP,
					      child,
					      (void *)ROUNDDOWN(cur_va, PGSIZE),
					      PTE_P |PTE_U |PTE_W)) < 0)
				return r;
			if ((r = sys_page_unmap(0, (void *)UTEMP)) < 0)
				return r;

			offset += size;
			cur_va += size;
		}
	} else {
		// text and read-only data
		offset = ph->p_offset;
		cur_va = ph->p_va;
		while (cur_va < ph->p_va + ph->p_filesz) {
			size = PGSIZE - (offset & 0xfff);

			if ((r = read_map(fd, offset, &blk)) < 0)
				return r;
			if ((r = sys_page_map(0,
					      ROUNDDOWN(blk, PGSIZE),
					      child,
					      (void *)ROUNDDOWN(cur_va, PGSIZE),
					      PTE_P |PTE_U)) < 0)
				return r;

			offset += size;
			cur_va += size;
		}
	}
	return 0;
}
