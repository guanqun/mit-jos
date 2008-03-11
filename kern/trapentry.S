/* See COPYRIGHT for copyright information. */

#include <inc/mmu.h>
#include <inc/memlayout.h>
#include <inc/trap.h>



###################################################################
# exceptions/interrupts
###################################################################

/* The TRAPHANDLER macro defines a globally-visible function for handling
 * a trap.  It pushes a trap number onto the stack, then jumps to _alltraps.
 * Use TRAPHANDLER for traps where the CPU automatically pushes an error code.
 */ 
#define TRAPHANDLER(name, num)						\
	.globl name;		/* define global symbol for 'name' */	\
	.type name, @function;	/* symbol type is function */		\
	.align 2;		/* align function definition */		\
	name:			/* function starts here */		\
	pushl $(num);							\
	jmp _alltraps

/* Use TRAPHANDLER_NOEC for traps where the CPU doesn't push an error code.
 * It pushes a 0 in place of the error code, so the trap frame has the same
 * format in either case.
 */
#define TRAPHANDLER_NOEC(name, num)					\
	.globl name;							\
	.type name, @function;						\
	.align 2;							\
	name:								\
	pushl $0;							\
	pushl $(num);							\
	jmp _alltraps

.text

/*
 * Lab 3: Your code here for generating entry points for the different traps.
 */
TRAPHANDLER_NOEC(divzero_entry, T_DIVIDE);
TRAPHANDLER_NOEC(debug_entry, T_DEBUG);
TRAPHANDLER_NOEC(nmi_entry, T_NMI);
TRAPHANDLER_NOEC(brkpt_entry, T_BRKPT);
TRAPHANDLER_NOEC(oflow_entry, T_OFLOW);
TRAPHANDLER_NOEC(bound_entry, T_BOUND);
TRAPHANDLER_NOEC(illop_entry, T_ILLOP);
TRAPHANDLER_NOEC(device_entry, T_DEVICE);
TRAPHANDLER(dblflt_entry, T_DBLFLT);
TRAPHANDLER(tss_entry, T_TSS);
TRAPHANDLER(segnp_entry, T_SEGNP);
TRAPHANDLER(stack_entry, T_STACK);
TRAPHANDLER(gpflt_entry, T_GPFLT);
TRAPHANDLER(pgflt_entry, T_PGFLT);
TRAPHANDLER_NOEC(fperr_entry, T_FPERR);
TRAPHANDLER(align_entry, T_ALIGN);
TRAPHANDLER_NOEC(mchk_entry, T_MCHK);
TRAPHANDLER_NOEC(simderr_entry, T_SIMDERR);

TRAPHANDLER_NOEC(syscall_entry, T_SYSCALL);

/*
 * Lab 3: Your code here for _alltraps
 */
_alltraps:
	pushl %ds
	pushl %es
	pushal
	movl $GD_KD, %eax
	movw %ax, %ds
	movw %ax, %es
	pushl %esp
	call trap
	pop %esp
	popal
	popl %es
	popl %ds
	iret

