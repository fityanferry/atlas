/*
 * Based on arch/arm/kernel/traps.c
 *
 * Copyright (C) 1995-2009 Russell King
 * Copyright (C) 2012 ARM Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/bug.h>
#include <linux/signal.h>
#include <linux/personality.h>
#include <linux/kallsyms.h>
#include <linux/spinlock.h>
#include <linux/uaccess.h>
#include <linux/hardirq.h>
#include <linux/kdebug.h>
#include <linux/module.h>
#include <linux/kexec.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/sched/signal.h>
#include <linux/sched/debug.h>
#include <linux/sched/task_stack.h>
#include <linux/sizes.h>
#include <linux/syscalls.h>
#include <linux/mm_types.h>
#include <linux/debug-snapshot.h>
#include <linux/kasan.h>
#include <linux/sec_debug.h>

#include <asm/atomic.h>
#include <asm/bug.h>
#include <asm/cpufeature.h>
#include <asm/daifflags.h>
#include <asm/debug-monitors.h>
#include <asm/esr.h>
#include <asm/insn.h>
#include <asm/traps.h>
#include <asm/smp.h>
#include <asm/stack_pointer.h>
#include <asm/stacktrace.h>
#include <asm/exception.h>
#include <asm/system_misc.h>
#include <asm/sysreg.h>

static const char *handler[]= {
	"Synchronous Abort",
	"IRQ",
	"FIQ",
	"Error"
};

int show_unhandled_signals = 0;

/*
 * Dump out the contents of some kernel memory nicely...
 */
static void dump_mem(const char *lvl, const char *str, unsigned long bottom,
		     unsigned long top)
{
	unsigned long first;
	mm_segment_t fs;
	int i;

	/*
	 * We need to switch to kernel mode so that we can use __get_user
	 * to safely read from kernel space.
	 */
	fs = get_fs();
	set_fs(KERNEL_DS);

	printk("%s%s(0x%016lx to 0x%016lx)\n", lvl, str, bottom, top);

	for (first = bottom & ~31; first < top; first += 32) {
		unsigned long p;
		char str[sizeof(" 12345678") * 8 + 1];

		memset(str, ' ', sizeof(str));
		str[sizeof(str) - 1] = '\0';

		for (p = first, i = 0; i < (32 / 8)
					&& p < top; i++, p += 8) {
			if (p >= bottom && p < top) {
				unsigned long val;

				if (__get_user(val, (unsigned long *)p) == 0)
					sprintf(str + i * 17, " %016lx", val);
				else
					sprintf(str + i * 17, " ????????????????");
			}
		}
		printk("%s%04lx:%s\n", lvl, first & 0xffff, str);
	}

	set_fs(fs);
}

static void dump_backtrace_entry(unsigned long where)
{
	/*
	 * Note that 'where' can have a physical address, but it's not handled.
	 */
	print_ip_sym(where);
}

#ifdef CONFIG_SEC_DEBUG_AUTO_COMMENT
static void dump_backtrace_entry_auto_summary(unsigned long where)
{
	/*
	 * Note that 'where' can have a physical address, but it's not handled.
	 */
	pr_auto(ASL2, "[<%px>] %pS\n", (void *) where, (void *) where);
}
#endif

static void __dump_instr(const char *lvl, struct pt_regs *regs)
{
	unsigned long addr = instruction_pointer(regs);
	char str[sizeof("00000000 ") * 5 + 2 + 1], *p = str;
	int i;

	for (i = -4; i < 1; i++) {
		unsigned int val, bad;

		bad = get_user(val, &((u32 *)addr)[i]);

		if (!bad)
			p += sprintf(p, i == 0 ? "(%08x) " : "%08x ", val);
		else {
			p += sprintf(p, "bad PC value");
			break;
		}
	}
	printk("%sCode: %s\n", lvl, str);
}

static void dump_instr(const char *lvl, struct pt_regs *regs)
{
	if (!user_mode(regs)) {
		mm_segment_t fs = get_fs();
		set_fs(KERNEL_DS);
		__dump_instr(lvl, regs);
		set_fs(fs);
	} else {
		__dump_instr(lvl, regs);
	}
}

#ifdef CONFIG_SEC_DEBUG_LIMIT_BACKTRACE
#define MAX_UNWINDING_LOOP 256 /* maximum number of unwind frame */
#endif

void dump_backtrace(struct pt_regs *regs, struct task_struct *tsk)
{
	struct stackframe frame;
	int skip = 0;
	int cnt = 0;

	pr_debug("%s(regs = %p tsk = %p)\n", __func__, regs, tsk);

	if (regs) {
		if (user_mode(regs))
			return;
		skip = 1;
	}

	if (!tsk)
		tsk = current;

	if (!try_get_task_stack(tsk))
		return;

	if (tsk == current) {
		frame.fp = (unsigned long)__builtin_frame_address(0);
		frame.pc = (unsigned long)dump_backtrace;
	} else {
		/*
		 * task blocked in __switch_to
		 */
		frame.fp = thread_saved_fp(tsk);
		frame.pc = thread_saved_pc(tsk);
	}
#ifdef CONFIG_FUNCTION_GRAPH_TRACER
	frame.graph = tsk->curr_ret_stack;
#endif

	printk("Call trace:\n");
	while (1) {
		unsigned long stack;
		int ret;

#ifdef CONFIG_SEC_DEBUG_LIMIT_BACKTRACE
		if (cnt > MAX_UNWINDING_LOOP) {
			pr_info("%s: Forcely break dump_backtrace to avoid infinity backtrace\n", __func__);
			break;
		}
#endif

		/* skip until specified stack frame */
		if (!skip) {
			dump_backtrace_entry(frame.pc);
			dbg_snapshot_save_log(raw_smp_processor_id(), frame.pc);
		} else if (frame.fp == regs->regs[29]) {
			skip = 0;
			/*
			 * Mostly, this is the case where this function is
			 * called in panic/abort. As exception handler's
			 * stack frame does not contain the corresponding pc
			 * at which an exception has taken place, use regs->pc
			 * instead.
			 */
			dump_backtrace_entry(regs->pc);
			dbg_snapshot_save_log(raw_smp_processor_id(), regs->pc);
		}
		ret = unwind_frame(tsk, &frame);
		if (ret < 0)
			break;
		if (in_entry_text(frame.pc)) {
			stack = frame.fp - offsetof(struct pt_regs, stackframe);

			if (on_accessible_stack(tsk, stack, NULL))
				dump_mem("", "Exception stack", stack,
					 stack + sizeof(struct pt_regs));
		}
		cnt++;
	}

	put_task_stack(tsk);
}

#ifdef CONFIG_SEC_DEBUG_AUTO_COMMENT
void dump_backtrace_auto_summary(struct pt_regs *regs, struct task_struct *tsk)
{
	struct stackframe frame;
	int skip = 0;
	int cnt = 0;

	pr_debug("%s(regs = %p tsk = %p)\n", __func__, regs, tsk);
	if (regs) {
		if (user_mode(regs))
			return;
		skip = 1;
	}
	if (!tsk)
		tsk = current;
	if (!try_get_task_stack(tsk))
		return;
	if (tsk == current) {
		frame.fp = (unsigned long)__builtin_frame_address(0);
		frame.pc = (unsigned long)dump_backtrace_auto_summary;
	} else {
		/*
		 * task blocked in __switch_to
		 */
		frame.fp = thread_saved_fp(tsk);
		frame.pc = thread_saved_pc(tsk);
	}
#ifdef CONFIG_FUNCTION_GRAPH_TRACER
	frame.graph = tsk->curr_ret_stack;
#endif
	pr_auto_once(2);
	pr_auto(ASL2, "Call trace:\n");
	while (1) {
		unsigned long stack;
		int ret;
#ifdef CONFIG_SEC_DEBUG_LIMIT_BACKTRACE
		if (cnt > MAX_UNWINDING_LOOP) {
			pr_info("%s: Forcely break dump_backtrace to avoid infinity backtrace\n", __func__);
			break;
		}
#endif
		/* skip until specified stack frame */
		if (!skip) {
			dump_backtrace_entry_auto_summary(frame.pc);
			dbg_snapshot_save_log(raw_smp_processor_id(), frame.pc);
		} else if (frame.fp == regs->regs[29]) {
			skip = 0;
			/*
			 * Mostly, this is the case where this function is
			 * called in panic/abort. As exception handler's
			 * stack frame does not contain the corresponding pc
			 * at which an exception has taken place, use regs->pc
			 * instead.
			 */
			dump_backtrace_entry_auto_summary(regs->pc);
			dbg_snapshot_save_log(raw_smp_processor_id(), regs->pc);
		}
		ret = unwind_frame(tsk, &frame);
		if (ret < 0)
			break;
		if (in_entry_text(frame.pc)) {
			stack = frame.fp - offsetof(struct pt_regs, stackframe);
			if (on_accessible_stack(tsk, stack, NULL))
				dump_mem("", "Exception stack", stack,
					 stack + sizeof(struct pt_regs));
		}
		cnt++;
	}
	put_task_stack(tsk);
}
#endif

void show_stack(struct task_struct *tsk, unsigned long *sp)
{
	dump_backtrace(NULL, tsk);
	barrier();
}

#ifdef CONFIG_SEC_DEBUG_AUTO_COMMENT
void show_stack_auto_comment(struct task_struct *tsk, unsigned long *sp)
{
	dump_backtrace_auto_summary(NULL, tsk);
	barrier();
}
#endif /* CONFIG_SEC_DEBUG_AUTO_COMMENT */

#ifdef CONFIG_PREEMPT
#define S_PREEMPT " PREEMPT"
#else
#define S_PREEMPT ""
#endif
#define S_SMP " SMP"

static int __die(const char *str, int err, struct pt_regs *regs)
{
	struct task_struct *tsk = current;
	static int die_counter;
	int ret;

	pr_emerg("Internal error: %s: %x [#%d]" S_PREEMPT S_SMP "\n",
		 str, err, ++die_counter);

	/* trap and error numbers are mostly meaningless on ARM */
	ret = notify_die(DIE_OOPS, str, regs, err, 0, SIGSEGV);
	if (ret == NOTIFY_STOP)
		return ret;

	print_modules();
	pr_emerg("Process %.*s (pid: %d, stack limit = 0x%p)\n",
		 TASK_COMM_LEN, tsk->comm, task_pid_nr(tsk),
		 end_of_stack(tsk));
#ifdef CONFIG_SEC_DEBUG_AUTO_COMMENT
	__show_regs(regs);
	dump_backtrace_auto_summary(regs, NULL);
#else
	show_regs(regs);
#endif

	if (!user_mode(regs))
		dump_instr(KERN_EMERG, regs);

	return ret;
}

static DEFINE_RAW_SPINLOCK(die_lock);

/*
 * This function is protected against re-entrancy.
 */
void die(const char *str, struct pt_regs *regs, int err)
{
	int ret;
	unsigned long flags;

	if (IS_ENABLED(CONFIG_SEC_DEBUG_DISABLE_IRQ_IN_DIE))
		local_irq_save(flags);
	oops_enter();
	if (IS_ENABLED(CONFIG_SEC_DEBUG_DISABLE_IRQ_IN_DIE))
		raw_spin_lock(&die_lock);
	else
		raw_spin_lock_irq(&die_lock);

	console_verbose();
	bust_spinlocks(1);
	ret = __die(str, err, regs);

	if (regs && kexec_should_crash(current))
		crash_kexec(regs);

	bust_spinlocks(0);
	add_taint(TAINT_DIE, LOCKDEP_NOW_UNRELIABLE);
	if (IS_ENABLED(CONFIG_SEC_DEBUG_DISABLE_IRQ_IN_DIE))
		raw_spin_unlock(&die_lock);
	else
		raw_spin_unlock_irq(&die_lock);
	oops_exit();
#ifdef CONFIG_SEC_DEBUG_EXTRA_INFO
	if (regs && (!user_mode(regs)))
		secdbg_exin_set_backtrace(regs);
#endif
#if defined(CONFIG_SEC_DEBUG_PRINT_PCLR)
	if (in_interrupt()) {
		if (regs)
			panic("%s\nPC is at %pS\nLR is at %pS",
			      "Fatal exception in interrupt", (void *)(regs)->pc,
			      (compat_user_mode(regs)) ? (void *)regs->compat_lr : (void *)regs->regs[30]);
		else
			panic("Fatal exception in interrupt");
	}
	if (panic_on_oops) {
		if (regs)
			panic("%s\nPC is at %pS\nLR is at %pS",
			      "Fatal exception", (void *)(regs)->pc,
			      (compat_user_mode(regs)) ? (void *)regs->compat_lr : (void *)regs->regs[30]);
		else
			panic("Fatal exception");
	}
#else

	if (in_interrupt())
		panic("Fatal exception in interrupt");
	if (panic_on_oops)
		panic("Fatal exception");

#endif
	if (IS_ENABLED(CONFIG_SEC_DEBUG_DISABLE_IRQ_IN_DIE))
		local_irq_restore(flags);

	if (ret != NOTIFY_STOP)
		make_task_dead(SIGSEGV);
}

static bool show_unhandled_signals_ratelimited(void)
{
	static DEFINE_RATELIMIT_STATE(rs, DEFAULT_RATELIMIT_INTERVAL,
				      DEFAULT_RATELIMIT_BURST);
	return show_unhandled_signals && __ratelimit(&rs);
}

void arm64_force_sig_info(struct siginfo *info, const char *str,
			  struct task_struct *tsk)
{
	unsigned int esr = tsk->thread.fault_code;
	struct pt_regs *regs = task_pt_regs(tsk);

	if (!unhandled_signal(tsk, info->si_signo))
		goto send_sig;

	if (!show_unhandled_signals_ratelimited())
		goto send_sig;

	pr_info("%s[%d]: unhandled exception: ", tsk->comm, task_pid_nr(tsk));
	if (esr)
		pr_cont("%s, ESR 0x%08x, ", esr_get_class_string(esr), esr);

	pr_cont("%s", str);
	print_vma_addr(KERN_CONT " in ", regs->pc);
	pr_cont("\n");
	__show_regs(regs);

send_sig:
	force_sig_info(info->si_signo, info, tsk);
}

void arm64_notify_die(const char *str, struct pt_regs *regs,
		      struct siginfo *info, int err)
{
	if (user_mode(regs)) {
		WARN_ON(regs != current_pt_regs());
		current->thread.fault_address = 0;
		current->thread.fault_code = err;
		arm64_force_sig_info(info, str, current);
	} else {
		die(str, regs, err);
	}
}

void arm64_skip_faulting_instruction(struct pt_regs *regs, unsigned long size)
{
	regs->pc += size;

	/*
	 * If we were single stepping, we want to get the step exception after
	 * we return from the trap.
	 */
	if (user_mode(regs))
		user_fastforward_single_step(current);
}

static LIST_HEAD(undef_hook);
static DEFINE_RAW_SPINLOCK(undef_lock);

void register_undef_hook(struct undef_hook *hook)
{
	unsigned long flags;

	raw_spin_lock_irqsave(&undef_lock, flags);
	list_add(&hook->node, &undef_hook);
	raw_spin_unlock_irqrestore(&undef_lock, flags);
}

void unregister_undef_hook(struct undef_hook *hook)
{
	unsigned long flags;

	raw_spin_lock_irqsave(&undef_lock, flags);
	list_del(&hook->node);
	raw_spin_unlock_irqrestore(&undef_lock, flags);
}

static int call_undef_hook(struct pt_regs *regs)
{
	struct undef_hook *hook;
	unsigned long flags;
	u32 instr;
	int (*fn)(struct pt_regs *regs, u32 instr) = NULL;
	void __user *pc = (void __user *)instruction_pointer(regs);

	if (!user_mode(regs)) {
		__le32 instr_le;
		if (probe_kernel_address((__force __le32 *)pc, instr_le))
			goto exit;
		instr = le32_to_cpu(instr_le);
	} else if (compat_thumb_mode(regs)) {
		/* 16-bit Thumb instruction */
		__le16 instr_le;
		if (get_user(instr_le, (__le16 __user *)pc))
			goto exit;
		instr = le16_to_cpu(instr_le);
		if (aarch32_insn_is_wide(instr)) {
			u32 instr2;

			if (get_user(instr_le, (__le16 __user *)(pc + 2)))
				goto exit;
			instr2 = le16_to_cpu(instr_le);
			instr = (instr << 16) | instr2;
		}
	} else {
		/* 32-bit ARM instruction */
		__le32 instr_le;
		if (get_user(instr_le, (__le32 __user *)pc))
			goto exit;
		instr = le32_to_cpu(instr_le);
	}

	raw_spin_lock_irqsave(&undef_lock, flags);
	list_for_each_entry(hook, &undef_hook, node)
		if ((instr & hook->instr_mask) == hook->instr_val &&
			(regs->pstate & hook->pstate_mask) == hook->pstate_val)
			fn = hook->fn;

	raw_spin_unlock_irqrestore(&undef_lock, flags);
exit:
	return fn ? fn(regs, instr) : 1;
}

void force_signal_inject(int signal, int code, unsigned long address)
{
	siginfo_t info;
	const char *desc;
	struct pt_regs *regs = current_pt_regs();

	clear_siginfo(&info);

	switch (signal) {
	case SIGILL:
		desc = "undefined instruction";
		break;
	case SIGSEGV:
		desc = "illegal memory access";
		break;
	default:
		desc = "unknown or unrecoverable error";
		break;
	}

	/* Force signals we don't understand to SIGKILL */
	if (WARN_ON(signal != SIGKILL &&
		    siginfo_layout(signal, code) != SIL_FAULT)) {
		signal = SIGKILL;
	}

	info.si_signo = signal;
	info.si_errno = 0;
	info.si_code  = code;
	info.si_addr  = (void __user *)address;

	arm64_notify_die(desc, regs, &info, 0);
}

/*
 * Set up process info to signal segmentation fault - called on access error.
 */
void arm64_notify_segfault(unsigned long addr)
{
	int code;

	down_read(&current->mm->mmap_sem);
	if (find_vma(current->mm, addr) == NULL)
		code = SEGV_MAPERR;
	else
		code = SEGV_ACCERR;
	up_read(&current->mm->mmap_sem);

	force_signal_inject(SIGSEGV, code, addr);
}

#if defined(CONFIG_SEC_DEBUG_FAULT_MSG_ADV)
asmlinkage void __exception do_undefinstr(struct pt_regs *regs, unsigned int esr)
#else
asmlinkage void __exception do_undefinstr(struct pt_regs *regs)
#endif
{
#ifdef CONFIG_SEC_DEBUG_EXTRA_INFO
	if (!user_mode(regs)) {
		secdbg_exin_set_fault(UNDEF_FAULT, (unsigned long)regs->pc, regs);
#if defined(CONFIG_SEC_DEBUG_FAULT_MSG_ADV)
		secdbg_exin_set_esr(esr);
#endif
	}
#endif /* CONFIG_SEC_DEBUG_EXTRA_INFO */
	/* check for AArch32 breakpoint instructions */
	if (!aarch32_break_handler(regs))
		return;
	if (call_undef_hook(regs) == 0)
		return;
#if defined(CONFIG_SEC_DEBUG_FAULT_MSG_ADV)
	if (!user_mode(regs)) {
		pr_auto(ASL1, "%s: pc=0x%016llx (esr=0x%08x)\n",
			"undefined instruction", regs->pc, esr);
		dump_instr(KERN_INFO, regs);
		die("undefined instruction", regs, esr);
	}
#endif

	force_signal_inject(SIGILL, ILL_ILLOPC, regs->pc);
	BUG_ON(!user_mode(regs));
}

void cpu_enable_cache_maint_trap(const struct arm64_cpu_capabilities *__unused)
{
	sysreg_clear_set(sctlr_el1, SCTLR_EL1_UCI, 0);
}

#define __user_cache_maint(insn, address, res)			\
	if (address >= user_addr_max()) {			\
		res = -EFAULT;					\
	} else {						\
		uaccess_ttbr0_enable();				\
		asm volatile (					\
			"1:	" insn ", %1\n"			\
			"	mov	%w0, #0\n"		\
			"2:\n"					\
			"	.pushsection .fixup,\"ax\"\n"	\
			"	.align	2\n"			\
			"3:	mov	%w0, %w2\n"		\
			"	b	2b\n"			\
			"	.popsection\n"			\
			_ASM_EXTABLE(1b, 3b)			\
			: "=r" (res)				\
			: "r" (address), "i" (-EFAULT));	\
		uaccess_ttbr0_disable();			\
	}

static void user_cache_maint_handler(unsigned int esr, struct pt_regs *regs)
{
	unsigned long address;
	int rt = (esr & ESR_ELx_SYS64_ISS_RT_MASK) >> ESR_ELx_SYS64_ISS_RT_SHIFT;
	int crm = (esr & ESR_ELx_SYS64_ISS_CRM_MASK) >> ESR_ELx_SYS64_ISS_CRM_SHIFT;
	int ret = 0;

	address = untagged_addr(pt_regs_read_reg(regs, rt));

	switch (crm) {
	case ESR_ELx_SYS64_ISS_CRM_DC_CVAU:	/* DC CVAU, gets promoted */
		__user_cache_maint("dc civac", address, ret);
		break;
	case ESR_ELx_SYS64_ISS_CRM_DC_CVAC:	/* DC CVAC, gets promoted */
		__user_cache_maint("dc civac", address, ret);
		break;
	case ESR_ELx_SYS64_ISS_CRM_DC_CVAP:	/* DC CVAP */
		__user_cache_maint("sys 3, c7, c12, 1", address, ret);
		break;
	case ESR_ELx_SYS64_ISS_CRM_DC_CIVAC:	/* DC CIVAC */
		__user_cache_maint("dc civac", address, ret);
		break;
	case ESR_ELx_SYS64_ISS_CRM_IC_IVAU:	/* IC IVAU */
		__user_cache_maint("ic ivau", address, ret);
		break;
	default:
		force_signal_inject(SIGILL, ILL_ILLOPC, regs->pc);
		return;
	}

	if (ret)
		arm64_notify_segfault(address);
	else
		arm64_skip_faulting_instruction(regs, AARCH64_INSN_SIZE);
}

static void ctr_read_handler(unsigned int esr, struct pt_regs *regs)
{
	int rt = (esr & ESR_ELx_SYS64_ISS_RT_MASK) >> ESR_ELx_SYS64_ISS_RT_SHIFT;
	unsigned long val = arm64_ftr_reg_user_value(&arm64_ftr_reg_ctrel0);

	if (cpus_have_const_cap(ARM64_WORKAROUND_1542419)) {
		/* Hide DIC so that we can trap the unnecessary maintenance...*/
		val &= ~BIT(CTR_DIC_SHIFT);

		/* ... and fake IminLine to reduce the number of traps. */
		val &= ~CTR_IMINLINE_MASK;
		val |= (PAGE_SHIFT - 2) & CTR_IMINLINE_MASK;
	}

	pt_regs_write_reg(regs, rt, val);

	arm64_skip_faulting_instruction(regs, AARCH64_INSN_SIZE);
}

static void cntvct_read_handler(unsigned int esr, struct pt_regs *regs)
{
	int rt = (esr & ESR_ELx_SYS64_ISS_RT_MASK) >> ESR_ELx_SYS64_ISS_RT_SHIFT;

	pt_regs_write_reg(regs, rt, arch_counter_get_cntvct());
	arm64_skip_faulting_instruction(regs, AARCH64_INSN_SIZE);
}

static void cntfrq_read_handler(unsigned int esr, struct pt_regs *regs)
{
	int rt = (esr & ESR_ELx_SYS64_ISS_RT_MASK) >> ESR_ELx_SYS64_ISS_RT_SHIFT;

	pt_regs_write_reg(regs, rt, arch_timer_get_rate());
	arm64_skip_faulting_instruction(regs, AARCH64_INSN_SIZE);
}

struct sys64_hook {
	unsigned int esr_mask;
	unsigned int esr_val;
	void (*handler)(unsigned int esr, struct pt_regs *regs);
};

static struct sys64_hook sys64_hooks[] = {
	{
		.esr_mask = ESR_ELx_SYS64_ISS_EL0_CACHE_OP_MASK,
		.esr_val = ESR_ELx_SYS64_ISS_EL0_CACHE_OP_VAL,
		.handler = user_cache_maint_handler,
	},
	{
		/* Trap read access to CTR_EL0 */
		.esr_mask = ESR_ELx_SYS64_ISS_SYS_OP_MASK,
		.esr_val = ESR_ELx_SYS64_ISS_SYS_CTR_READ,
		.handler = ctr_read_handler,
	},
	{
		/* Trap read access to CNTVCT_EL0 */
		.esr_mask = ESR_ELx_SYS64_ISS_SYS_OP_MASK,
		.esr_val = ESR_ELx_SYS64_ISS_SYS_CNTVCT,
		.handler = cntvct_read_handler,
	},
	{
		/* Trap read access to CNTFRQ_EL0 */
		.esr_mask = ESR_ELx_SYS64_ISS_SYS_OP_MASK,
		.esr_val = ESR_ELx_SYS64_ISS_SYS_CNTFRQ,
		.handler = cntfrq_read_handler,
	},
	{},
};

asmlinkage void __exception do_sysinstr(unsigned int esr, struct pt_regs *regs)
{
	struct sys64_hook *hook;

	for (hook = sys64_hooks; hook->handler; hook++)
		if ((hook->esr_mask & esr) == hook->esr_val) {
			hook->handler(esr, regs);
			return;
		}

	/*
	 * New SYS instructions may previously have been undefined at EL0. Fall
	 * back to our usual undefined instruction handler so that we handle
	 * these consistently.
	 */
#if defined(CONFIG_SEC_DEBUG_FAULT_MSG_ADV)
	do_undefinstr(regs, esr);
#else
	do_undefinstr(regs);
#endif
}

static const char *esr_class_str[] = {
	[0 ... ESR_ELx_EC_MAX]		= "UNRECOGNIZED EC",
	[ESR_ELx_EC_UNKNOWN]		= "Unknown/Uncategorized",
	[ESR_ELx_EC_WFx]		= "WFI/WFE",
	[ESR_ELx_EC_CP15_32]		= "CP15 MCR/MRC",
	[ESR_ELx_EC_CP15_64]		= "CP15 MCRR/MRRC",
	[ESR_ELx_EC_CP14_MR]		= "CP14 MCR/MRC",
	[ESR_ELx_EC_CP14_LS]		= "CP14 LDC/STC",
	[ESR_ELx_EC_FP_ASIMD]		= "ASIMD",
	[ESR_ELx_EC_CP10_ID]		= "CP10 MRC/VMRS",
	[ESR_ELx_EC_CP14_64]		= "CP14 MCRR/MRRC",
	[ESR_ELx_EC_ILL]		= "PSTATE.IL",
	[ESR_ELx_EC_SVC32]		= "SVC (AArch32)",
	[ESR_ELx_EC_HVC32]		= "HVC (AArch32)",
	[ESR_ELx_EC_SMC32]		= "SMC (AArch32)",
	[ESR_ELx_EC_SVC64]		= "SVC (AArch64)",
	[ESR_ELx_EC_HVC64]		= "HVC (AArch64)",
	[ESR_ELx_EC_SMC64]		= "SMC (AArch64)",
	[ESR_ELx_EC_SYS64]		= "MSR/MRS (AArch64)",
	[ESR_ELx_EC_SVE]		= "SVE",
	[ESR_ELx_EC_IMP_DEF]		= "EL3 IMP DEF",
	[ESR_ELx_EC_IABT_LOW]		= "IABT (lower EL)",
	[ESR_ELx_EC_IABT_CUR]		= "IABT (current EL)",
	[ESR_ELx_EC_PC_ALIGN]		= "PC Alignment",
	[ESR_ELx_EC_DABT_LOW]		= "DABT (lower EL)",
	[ESR_ELx_EC_DABT_CUR]		= "DABT (current EL)",
	[ESR_ELx_EC_SP_ALIGN]		= "SP Alignment",
	[ESR_ELx_EC_FP_EXC32]		= "FP (AArch32)",
	[ESR_ELx_EC_FP_EXC64]		= "FP (AArch64)",
	[ESR_ELx_EC_SERROR]		= "SError",
	[ESR_ELx_EC_BREAKPT_LOW]	= "Breakpoint (lower EL)",
	[ESR_ELx_EC_BREAKPT_CUR]	= "Breakpoint (current EL)",
	[ESR_ELx_EC_SOFTSTP_LOW]	= "Software Step (lower EL)",
	[ESR_ELx_EC_SOFTSTP_CUR]	= "Software Step (current EL)",
	[ESR_ELx_EC_WATCHPT_LOW]	= "Watchpoint (lower EL)",
	[ESR_ELx_EC_WATCHPT_CUR]	= "Watchpoint (current EL)",
	[ESR_ELx_EC_BKPT32]		= "BKPT (AArch32)",
	[ESR_ELx_EC_VECTOR32]		= "Vector catch (AArch32)",
	[ESR_ELx_EC_BRK64]		= "BRK (AArch64)",
};

const char *esr_get_class_string(u32 esr)
{
	return esr_class_str[ESR_ELx_EC(esr)];
}

/*
 * bad_mode handles the impossible case in the exception vector. This is always
 * fatal.
 */
asmlinkage void bad_mode(struct pt_regs *regs, int reason, unsigned int esr)
{
	console_verbose();

	pr_auto(ASL1,
		"Bad mode in %s handler detected on CPU%d, code 0x%08x -- %s\n",
		handler[reason], smp_processor_id(), esr,
		esr_get_class_string(esr));
#ifdef CONFIG_SEC_DEBUG_EXTRA_INFO
	if (!user_mode(regs)) {
		secdbg_exin_set_fault(BAD_MODE_FAULT, (unsigned long)regs->pc, regs);
		secdbg_exin_set_esr(esr);
	}
#endif

	local_daif_mask();
	panic("bad mode");
}

/*
 * bad_el0_sync handles unexpected, but potentially recoverable synchronous
 * exceptions taken from EL0. Unlike bad_mode, this returns.
 */
asmlinkage void bad_el0_sync(struct pt_regs *regs, int reason, unsigned int esr)
{
	siginfo_t info;
	void __user *pc = (void __user *)instruction_pointer(regs);

	clear_siginfo(&info);
	info.si_signo = SIGILL;
	info.si_errno = 0;
	info.si_code  = ILL_ILLOPC;
	info.si_addr  = pc;

	current->thread.fault_address = 0;
	current->thread.fault_code = esr;

	arm64_force_sig_info(&info, "Bad EL0 synchronous exception", current);
}

#ifdef CONFIG_VMAP_STACK

DEFINE_PER_CPU(unsigned long [OVERFLOW_STACK_SIZE/sizeof(long)], overflow_stack)
	__aligned(16);

#ifdef CONFIG_SEC_DEBUG_BAD_STACK_CAREFULLY
#define thread_virt_addr_valid(xaddr)   pfn_valid(__pa(xaddr) >> PAGE_SHIFT)
extern int __do_kernel_fault_safe(struct mm_struct *mm, unsigned long addr,
		unsigned int esr, struct pt_regs *regs);
#endif /* CONFIG_SEC_DEBUG_BAD_STACK_CAREFULLY */

asmlinkage void handle_bad_stack(struct pt_regs *regs)
{
	if (IS_ENABLED(CONFIG_SEC_DEBUG_BAD_STACK_INFO))
		secdbg_base_set_bs_info_phase(1);
#ifdef CONFIG_SEC_DEBUG_BAD_STACK_CAREFULLY
	/* check sp_el0 address*/
	if (!thread_virt_addr_valid(current_thread_info()))
		__do_kernel_fault_safe(NULL, 0, 0, regs);
#endif /* CONFIG_SEC_DEBUG_BAD_STACK_CAREFULLY */
	{
		unsigned long tsk_stk = (unsigned long)current->stack;
		unsigned long irq_stk = (unsigned long)this_cpu_read(irq_stack_ptr);
		unsigned long ovf_stk = (unsigned long)this_cpu_ptr(overflow_stack);
		unsigned int esr = read_sysreg(esr_el1);
		unsigned long far = read_sysreg(far_el1);
		if (IS_ENABLED(CONFIG_SEC_DEBUG_BAD_STACK_INFO))
			secdbg_base_set_bs_info_phase(2);
		console_verbose();
		pr_emerg("Insufficient stack space to handle exception!");
		pr_emerg("ESR: 0x%08x -- %s\n", esr, esr_get_class_string(esr));
		pr_emerg("FAR: 0x%016lx\n", far);
		pr_emerg("Task stack:     [0x%016lx..0x%016lx]\n",
			 tsk_stk, tsk_stk + THREAD_SIZE);
		pr_emerg("IRQ stack:      [0x%016lx..0x%016lx]\n",
			 irq_stk, irq_stk + THREAD_SIZE);
		pr_emerg("Overflow stack: [0x%016lx..0x%016lx]\n",
			 ovf_stk, ovf_stk + OVERFLOW_STACK_SIZE);
		__show_regs(regs);
		/*
		 * We use nmi_panic to limit the potential for recusive overflows, and
		 * to get a better stack trace.
		 */
		panic("kernel stack overflow");
		cpu_park_loop();
	}
}
#endif

void __noreturn arm64_serror_panic(struct pt_regs *regs, u32 esr)
{
	console_verbose();
	printk_nmi_exit();

	pr_auto(ASL1, "SError Interrupt on CPU%d, code 0x%08x -- %s\n",
		smp_processor_id(), esr, esr_get_class_string(esr));
#ifdef CONFIG_SEC_DEBUG_EXTRA_INFO
	if (regs && !user_mode(regs)) {
		secdbg_exin_set_fault(SERROR_FAULT, (unsigned long)regs->pc, regs);
		secdbg_exin_set_esr(esr);
	}
#endif
	if (regs)
		__show_regs(regs);

	panic("Asynchronous SError Interrupt");

	cpu_park_loop();
	unreachable();
}

bool arm64_is_fatal_ras_serror(struct pt_regs *regs, unsigned int esr)
{
	u32 aet = arm64_ras_serror_get_severity(esr);

	switch (aet) {
	case ESR_ELx_AET_CE:	/* corrected error */
	case ESR_ELx_AET_UEO:	/* restartable, not yet consumed */
		/*
		 * The CPU can make progress. We may take UEO again as
		 * a more severe error.
		 */
		return false;

	case ESR_ELx_AET_UEU:	/* Uncorrected Unrecoverable */
	case ESR_ELx_AET_UER:	/* Uncorrected Recoverable */
		/*
		 * The CPU can't make progress. The exception may have
		 * been imprecise.
		 */
		return true;

	case ESR_ELx_AET_UC:	/* Uncontainable or Uncategorized error */
	default:
		/* Error has been silently propagated */
		arm64_serror_panic(regs, esr);
	}
}

asmlinkage void do_serror(struct pt_regs *regs, unsigned int esr)
{
	nmi_enter();

	/* non-RAS errors are not containable */
	if (!arm64_is_ras_serror(esr) || arm64_is_fatal_ras_serror(regs, esr))
		arm64_serror_panic(regs, esr);

	nmi_exit();
}

void __pte_error(const char *file, int line, unsigned long val)
{
	pr_err("%s:%d: bad pte %016lx.\n", file, line, val);
}

void __pmd_error(const char *file, int line, unsigned long val)
{
	pr_err("%s:%d: bad pmd %016lx.\n", file, line, val);
}

void __pud_error(const char *file, int line, unsigned long val)
{
	pr_err("%s:%d: bad pud %016lx.\n", file, line, val);
}

void __pgd_error(const char *file, int line, unsigned long val)
{
	pr_err("%s:%d: bad pgd %016lx.\n", file, line, val);
}

/* GENERIC_BUG traps */

int is_valid_bugaddr(unsigned long addr)
{
	/*
	 * bug_handler() only called for BRK #BUG_BRK_IMM.
	 * So the answer is trivial -- any spurious instances with no
	 * bug table entry will be rejected by report_bug() and passed
	 * back to the debug-monitors code and handled as a fatal
	 * unexpected debug exception.
	 */
	return 1;
}

static int bug_handler(struct pt_regs *regs, unsigned int esr)
{
	if (user_mode(regs))
		return DBG_HOOK_ERROR;

	/*
	 * If recalling hardlockup core has been run before,
	 * PC value must be replaced to real PC value.
	 */
	dbg_snapshot_hook_hardlockup_entry((void *)regs);

	switch (report_bug(regs->pc, regs)) {
	case BUG_TRAP_TYPE_BUG:
#ifdef CONFIG_SEC_DEBUG_EXTRA_INFO
		secdbg_exin_set_fault(BUG_FAULT, (unsigned long)regs->pc, regs);
		secdbg_exin_set_esr(esr);
#endif
		die("Oops - BUG", regs, 0);
		break;

	case BUG_TRAP_TYPE_WARN:
		break;

	default:
		/* unknown/unrecognised bug trap type */
		return DBG_HOOK_ERROR;
	}

	/* If thread survives, skip over the BUG instruction and continue: */
	arm64_skip_faulting_instruction(regs, AARCH64_INSN_SIZE);
	return DBG_HOOK_HANDLED;
}

static struct break_hook bug_break_hook = {
	.esr_val = 0xf2000000 | BUG_BRK_IMM,
	.esr_mask = 0xffffffff,
	.fn = bug_handler,
};

#ifdef CONFIG_KASAN_SW_TAGS

#define KASAN_ESR_RECOVER	0x20
#define KASAN_ESR_WRITE	0x10
#define KASAN_ESR_SIZE_MASK	0x0f
#define KASAN_ESR_SIZE(esr)	(1 << ((esr) & KASAN_ESR_SIZE_MASK))

static int kasan_handler(struct pt_regs *regs, unsigned int esr)
{
	bool recover = esr & KASAN_ESR_RECOVER;
	bool write = esr & KASAN_ESR_WRITE;
	size_t size = KASAN_ESR_SIZE(esr);
	u64 addr = regs->regs[0];
	u64 pc = regs->pc;

	if (user_mode(regs))
		return DBG_HOOK_ERROR;

	kasan_report(addr, size, write, pc);

	/*
	 * The instrumentation allows to control whether we can proceed after
	 * a crash was detected. This is done by passing the -recover flag to
	 * the compiler. Disabling recovery allows to generate more compact
	 * code.
	 *
	 * Unfortunately disabling recovery doesn't work for the kernel right
	 * now. KASAN reporting is disabled in some contexts (for example when
	 * the allocator accesses slab object metadata; this is controlled by
	 * current->kasan_depth). All these accesses are detected by the tool,
	 * even though the reports for them are not printed.
	 *
	 * This is something that might be fixed at some point in the future.
	 */
	if (!recover)
		die("Oops - KASAN", regs, 0);

	/* If thread survives, skip over the brk instruction and continue: */
	arm64_skip_faulting_instruction(regs, AARCH64_INSN_SIZE);
	return DBG_HOOK_HANDLED;
}

#define KASAN_ESR_VAL (0xf2000000 | KASAN_BRK_IMM)
#define KASAN_ESR_MASK 0xffffff00

static struct break_hook kasan_break_hook = {
	.esr_val = KASAN_ESR_VAL,
	.esr_mask = KASAN_ESR_MASK,
	.fn = kasan_handler,
};
#endif

/*
 * Initial handler for AArch64 BRK exceptions
 * This handler only used until debug_traps_init().
 */
int __init early_brk64(unsigned long addr, unsigned int esr,
		struct pt_regs *regs)
{
#ifdef CONFIG_KASAN_SW_TAGS
	if ((esr & KASAN_ESR_MASK) == KASAN_ESR_VAL)
		return kasan_handler(regs, esr) != DBG_HOOK_HANDLED;
#endif
	return bug_handler(regs, esr) != DBG_HOOK_HANDLED;
}

/* This registration must happen early, before debug_traps_init(). */
void __init trap_init(void)
{
	register_break_hook(&bug_break_hook);
#ifdef CONFIG_KASAN_SW_TAGS
	register_break_hook(&kasan_break_hook);
#endif
}
