/*
 *  linux/kernel/fork.c
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 *  'fork.c' contains the help-routines for the 'fork' system call
 * (see also system_call.s), and some misc functions ('verify_area').
 * Fork is rather simple, once you get the hang of it, but the memory
 * management can be a bitch. See 'mm/mm.c': 'copy_page_tables()'
 */
#include <errno.h>

#include <linux/sched.h>
#include <linux/kernel.h>
#include <asm/segment.h>
#include <asm/system.h>

extern void write_verify(unsigned long address);

long last_pid=0;

void verify_area(void * addr,int size)
{
	unsigned long start;

	start = (unsigned long) addr;
	size += start & 0xfff;
	start &= 0xfffff000;
	start += get_base(current->ldt[2]);
	while (size>0) {
		size -= 4096;
		write_verify(start);
		start += 4096;
	}
}

int copy_mem(int nr,struct task_struct * p)
{
	unsigned long old_data_base,new_data_base,data_limit;
	unsigned long old_code_base,new_code_base,code_limit;

	code_limit=get_limit(0x0f);
	data_limit=get_limit(0x17);
	old_code_base = get_base(current->ldt[1]);  // 获取父进程的代码段基址
	old_data_base = get_base(current->ldt[2]);  // 获取父进程的数据段基址
	if (old_data_base != old_code_base)
		panic("We don't support separate I&D");
	if (data_limit < code_limit)
		panic("Bad data_limit");
	new_data_base = new_code_base = nr * 0x4000000; // 子进程代码段和数据段的基址为nr*64MB
	p->start_code = new_code_base;     
	set_base(p->ldt[1],new_code_base);
	set_base(p->ldt[2],new_data_base);
	if (copy_page_tables(old_data_base,new_data_base,data_limit)) { // 复制父进程的页表到子进程
		printk("free_page_tables: from copy_mem\n");
		free_page_tables(new_data_base,data_limit);
		return -ENOMEM;
	}
	return 0;
}

/*
 *  Ok, this is the main fork-routine. It copies the system process
 * information (task[nr]) and sets up the necessary registers. It
 * also copies the data segment in it's entirety.
 */
int copy_process(int nr,long ebp,long edi,long esi,long gs,long none,
		long ebx,long ecx,long edx,
		long fs,long es,long ds,
		long eip,long cs,long eflags,long esp,long ss)
{
	struct task_struct *p;
	int i;
	struct file *f;

	p = (struct task_struct *) get_free_page();  // 在主存中申请一个空闲页面给子进程
	if (!p)
		return -EAGAIN;
	task[nr] = p;  // nr就是新获取的task下标

	// 将父进程的内容拷贝给子进程
	*p = *current;	/* NOTE! this doesn't copy the supervisor stack */ 
	p->state = TASK_UNINTERRUPTIBLE; // 进程1才创建PCB，设置为不可中断

	// 下面是进程1的一些自己的状态设置
	p->pid = last_pid;
	p->father = current->pid;
	p->counter = p->priority;
	p->signal = 0;
	p->alarm = 0;
	p->leader = 0;		/* process leadership doesn't inherit */
	p->utime = p->stime = 0;
	p->cutime = p->cstime = 0;
	p->start_time = jiffies;
	//=================================================//
	// 将进程状态输出到process.log中
	fprintk(3, "%d\t%s\t%ld\n", last_pid, "N", jiffies); 
	//=================================================//
	p->tss.back_link = 0;
	p->tss.esp0 = PAGE_SIZE + (long) p;
	p->tss.ss0 = 0x10;
	p->tss.eip = eip;	// ip寄存器指向是当前程序的偏移，即程序执行到哪个条指令了，结合下面的eax，构成了fork的奇特行为
	p->tss.eflags = eflags;
	p->tss.eax = 0;   // eax表示返回值，这里将子进程的eax设为0，对应了fork在子进程中返回0
	p->tss.ecx = ecx;
	p->tss.edx = edx;
	p->tss.ebx = ebx;
	p->tss.esp = esp;
	p->tss.ebp = ebp;
	p->tss.esi = esi;
	p->tss.edi = edi;
	p->tss.es = es & 0xffff;
	p->tss.cs = cs & 0xffff;
	p->tss.ss = ss & 0xffff;
	p->tss.ds = ds & 0xffff;
	p->tss.fs = fs & 0xffff;
	p->tss.gs = gs & 0xffff;
	p->tss.ldt = _LDT(nr);
	p->tss.trace_bitmap = 0x80000000;

	if (last_task_used_math == current)
		__asm__("clts ; fnsave %0"::"m" (p->tss.i387));

	if (copy_mem(nr,p)) {
		task[nr] = NULL;
		free_page((long) p);
		return -EAGAIN;
	}
	
	for (i=0; i<NR_OPEN;i++)
		if ((f=p->filp[i]))
			f->f_count++;
	if (current->pwd)
		current->pwd->i_count++;
	if (current->root)
		current->root->i_count++;
	if (current->executable)
		current->executable->i_count++;

	// 将进程1的TSS和LDT挂载到GDT上
	set_tss_desc(gdt+(nr<<1)+FIRST_TSS_ENTRY,&(p->tss));  
	set_ldt_desc(gdt+(nr<<1)+FIRST_LDT_ENTRY,&(p->ldt));
	// 将进程1设置为就绪态，使其可以参加调度
	p->state = TASK_RUNNING;	/* do this last, just in case */
	//=================================================//
	fprintk(3, "%d\t%s\t%ld\n", p->pid, "J", jiffies); 
	//=================================================//
	return last_pid; // 父进程得到fork返回的子进程的pid
}

int find_empty_process(void)
{
	int i;

	repeat:
		if ((++last_pid)<0) last_pid=1;
		for(i=0 ; i<NR_TASKS ; i++)
			if (task[i] && task[i]->pid == last_pid) goto repeat;  // 如果该pid被占用就尝试下一个
	for(i=1 ; i<NR_TASKS ; i++)
		if (!task[i])  // 从task数组中找到一个空位
			return i;
	return -EAGAIN;
}
