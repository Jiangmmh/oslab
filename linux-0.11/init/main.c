/*
 *  linux/init/main.c
 *
 *  (C) 1991  Linus Torvalds
 */

#define __LIBRARY__
#include <unistd.h>
#include <time.h>

/*
 * we need this inline - forking from kernel space will result
 * in NO COPY ON WRITE (!!!), until an execve is executed. This
 * is no problem, but for the stack. This is handled by not letting
 * main() use the stack at all after fork(). Thus, no function
 * calls - which means inline code for fork too, as otherwise we
 * would use the stack upon exit from 'fork()'.
 *
 * Actually only pause and fork are needed inline, so that there
 * won't be any messing with the stack from main(), but we define
 * some others too.
 */
static inline _syscall0(int,fork)
static inline _syscall0(int,pause)
static inline _syscall1(int,setup,void *,BIOS)
static inline _syscall0(int,sync)

#include <linux/tty.h>
#include <linux/sched.h>
#include <linux/head.h>
#include <asm/system.h>
#include <asm/io.h>

#include <stddef.h>
#include <stdarg.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>

#include <linux/fs.h>

static char printbuf[1024];

extern int vsprintf();
extern void init(void);
extern void blk_dev_init(void);
extern void chr_dev_init(void);
extern void hd_init(void);
extern void floppy_init(void);
extern void mem_init(long start, long end);
extern long rd_init(long mem_start, int length);
extern long kernel_mktime(struct tm * tm);
extern long startup_time;

/*
 * This is set up by the setup-routine at boot-time
 */
#define EXT_MEM_K (*(unsigned short *)0x90002)
#define DRIVE_INFO (*(struct drive_info *)0x90080)
#define ORIG_ROOT_DEV (*(unsigned short *)0x901FC)

/*
 * Yeah, yeah, it's ugly, but I cannot find how to do this correctly
 * and this seems to work. I anybody has more info on the real-time
 * clock I'd be interested. Most of this was trial and error, and some
 * bios-listing reading. Urghh.
 */

#define CMOS_READ(addr) ({ \
outb_p(0x80|addr,0x70); \
inb_p(0x71); \
})

#define BCD_TO_BIN(val) ((val)=((val)&15) + ((val)>>4)*10)

static void time_init(void)
{
	struct tm time;

	// 从主板上的一个CMOS芯片上读取启动时间
	do {
		time.tm_sec = CMOS_READ(0);
		time.tm_min = CMOS_READ(2);
		time.tm_hour = CMOS_READ(4);
		time.tm_mday = CMOS_READ(7);
		time.tm_mon = CMOS_READ(8);
		time.tm_year = CMOS_READ(9);
	} while (time.tm_sec != CMOS_READ(0));
	BCD_TO_BIN(time.tm_sec);
	BCD_TO_BIN(time.tm_min);
	BCD_TO_BIN(time.tm_hour);
	BCD_TO_BIN(time.tm_mday);
	BCD_TO_BIN(time.tm_mon);
	BCD_TO_BIN(time.tm_year);
	time.tm_mon--;
	startup_time = kernel_mktime(&time);
}

static long memory_end = 0;
static long buffer_memory_end = 0;
static long main_memory_start = 0;

struct drive_info { char dummy[32]; } drive_info;

void main(void)		/* This really IS void, no error here. */
{			/* The startup routine assumes (well, ...) this */
/*
 * Interrupts are still disabled. Do necessary setups, then
 * enable them
 */
 	ROOT_DEV = ORIG_ROOT_DEV; // 0x901FC 根设备号存放于此
 	drive_info = DRIVE_INFO;  // 0x90080 第一个硬盘参数表存放于此
	memory_end = (1<<20) + (EXT_MEM_K<<10);  // 1<<20为1M，<<10 为 *1K，EXT_MEM_K为扩展内存的字节数
	memory_end &= 0xfffff000;
	if (memory_end > 16*1024*1024)  // 内存大于16M，设置为16M
		memory_end = 16*1024*1024;
	if (memory_end > 12*1024*1024)   // 内存大于12M，设置缓冲区为4M
		buffer_memory_end = 4*1024*1024;  
	else if (memory_end > 6*1024*1024) // 内存为6M~12M，设置缓冲区为2M
		buffer_memory_end = 2*1024*1024;
	else
		buffer_memory_end = 1*1024*1024; // 内存<6M，设置缓冲区为1M
	main_memory_start = buffer_memory_end; // 主存紧挨在缓冲区之后
#ifdef RAMDISK
	main_memory_start += rd_init(main_memory_start, RAMDISK*1024); // 如果有虚拟盘的话则开辟2M，主存后移2M
#endif
	mem_init(main_memory_start,memory_end); // 初始化内存管理结构mem_map
	trap_init();	// 将异常中断服务程序挂接到IDT
	blk_dev_init(); // 初始化块设备的数据结构
	chr_dev_init(); // 本意为初始化字符设备的数据结构，其实是一个空函数
	tty_init();		// 设置外设相关中断处理函数到IDT
	time_init();	// 开机启动时间设置
	sched_init();   // 初始化进程0，设置TSS和LDT、打开时钟中断、将系统调用处理程序挂在到IDT
	buffer_init(buffer_memory_end); // 初始化缓冲区管理数据结构
	hd_init();  	// 初始化硬盘
	floppy_init();  // 初始化软盘
	sti();			// 开启中断，将EFLAG寄存器中的IF位置为1
	move_to_user_mode(); // 将进程0的特权级从0（内核态）转为1（用户态）

	//================将init的中的打开文件部分剪切到此处==================//
	// 目的是为了在进程1（shell）被创建之前打开process.log
	setup((void *) &drive_info);
	(void) open("/dev/tty0",O_RDWR,0);
	(void) dup(0);
	(void) dup(0);
	// 此时的文件描述符为3，O_TRUNC表示每次打开都清除之前的内容，0666为访问权限 -> -rw_rw_rw_ 
	(void) open("/var/process.log", O_CREAT|O_TRUNC|O_WRONLY, 0666);
	//==================================================================// 

	if (!fork()) {		/* we count on this going ok */
		init();
	}
/*
 *   NOTE!!   For any other task 'pause()' would mean we have to get a
 * signal to awaken, but task0 is the sole exception (see 'schedule()')
 * as task 0 gets activated at every idle moment (when no other tasks
 * can run). For task0 'pause()' just means we go check if some other
 * task can run, and if not we return here.
 */
	for(;;) pause(); // 进程0永远在此死循环
}

static int printf(const char *fmt, ...)
{
	va_list args;
	int i;

	va_start(args, fmt);
	write(1,printbuf,i=vsprintf(printbuf, fmt, args));
	va_end(args);
	return i;
}

static char * argv_rc[] = { "/bin/sh", NULL };
static char * envp_rc[] = { "HOME=/", NULL };

static char * argv[] = { "-/bin/sh",NULL };
static char * envp[] = { "HOME=/usr/root", NULL };

void init(void)
{
	int pid,i;
	printf("%d buffers = %d bytes buffer space\n\r",NR_BUFFERS,
		NR_BUFFERS*BLOCK_SIZE);
	printf("Free mem: %d bytes\n\r",memory_end-main_memory_start);
	if (!(pid=fork())) {
		close(0);
		if (open("/etc/rc",O_RDONLY,0))
			_exit(1);
		execve("/bin/sh",argv_rc,envp_rc);
		_exit(2);
	}
	if (pid>0)
		while (pid != wait(&i))
			/* nothing */;
	while (1) {
		if ((pid=fork())<0) {
			printf("Fork failed in init\r\n");
			continue;
		}
		if (!pid) {
			close(0);close(1);close(2);
			setsid();
			(void) open("/dev/tty0",O_RDWR,0);
			(void) dup(0);
			(void) dup(0);
			_exit(execve("/bin/sh",argv,envp));
		}
		while (1)
			if (pid == wait(&i))
				break;
		printf("\n\rchild %d died with code %04x\n\r",pid,i);
		sync();
	}
	_exit(0);	/* NOTE! _exit, not exit() */
}
