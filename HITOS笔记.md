

# Lab1 启动

## 源码梳理

### bootsect.s

在计算机加电之后，硬件电路会初始化PC寄存器的值，将其置为0xFFFF，IP寄存器的值默认为0x0000，根据x86实模式的寻址规则，第一条要执行的指令就在CS:IP=0xFFFF0处，此处指向的是BIOS（一个非易失性的固件）中的指令，这段指令会对基本的硬件进行检测，如主板、CPU、内存、显卡、键盘/显示器、硬盘、USB端口等。完成硬件自检后，BIOS会将磁盘的主引导扇区（以0x55aa结尾）加载到内存0x7c00处，这个扇区包含了加载操作系统的少量关键代码，也称为引导加载程序（Boot Loader）。

在Linux0.11中，这个主引导扇区的代码在**boot/bootsect.s**中，我们可以看到Linus对这个文件的介绍注释：

```assembly
! SYS_SIZE is the number of clicks (16 bytes) to be loaded.
! 0x3000 is 0x30000 bytes = 196kB, more than enough for current
! versions of linux
!
SYSSIZE = 0x3000
!
!	bootsect.s		(C) 1991 Linus Torvalds
!
! bootsect.s is loaded at 0x7c00 by the bios-startup routines, and moves
! iself out of the way to address 0x90000, and jumps there.
!
! It then loads 'setup' directly after itself (0x90200), and the system
! at 0x10000, using BIOS interrupts. 
!
! NOTE! currently system is at most 8*65536 bytes long. This should be no
! problem, even in the future. I want to keep it simple. This 512 kB
! kernel size should be enough, especially as this doesn't contain the
! buffer cache as in minix
!
! The loader has been made as simple as possible, and continuos
! read errors will result in a unbreakable loop. Reboot by hand. It
! loads pretty fast by getting whole sectors at a time whenever possible.
```

bootsect.s由BIOS启动程序加载到0x7c00，然后将其自身（biossect.s）移动到0x90000处，然后跳转到那里。随后通过BIOS中断将setup加载到0x90200处，将system加载到0x10000处。这些信息还可以通过变量的定义看到：

```assembly
SETUPLEN = 4				! setup的扇区数
BOOTSEG  = 0x07c0			! bootsect的起始地址
INITSEG  = 0x9000			! bootsect移动的目的地址
SETUPSEG = 0x9020			! setup的起始地址
SYSSEG   = 0x1000			! 系统的起始地址
ENDSEG   = SYSSEG + SYSSIZE ! 系统的结束地址
```

来看看这段指令具体做了什么：

```assembly
entry _start
_start:
	mov	ax,#BOOTSEG
	mov	ds,ax
	mov	ax,#INITSEG
	mov	es,ax
	mov	cx,#256         
	sub	si,si
	sub	di,di
	rep
	movw		
	jmpi	go,INITSEG
```

这段代码的功能是将起始地址为ds:si的256个字（一个字为两个字节）移动到es:di处，即将从0x7c00开始的512个字节移动到0x90000处。然后通过jmpi指令，跳转到0x9000 + go处，这个go是bootsect中的一个标签，表示偏移。

```assembly
go:	mov	ax,cs
	mov	ds,ax
	mov	es,ax
! put stack at 0x9ff00.
	mov	ss,ax
	mov	sp,#0xFF00		! arbitrary value >>512
```

可以看到跳转后，设置了ds（数据段寄存器）、es（附加段寄存器）、ss（栈段寄存器）、sp（栈指针寄存器），现在栈顶指向0x9ff00。

```assembly
load_setup:
	mov	dx,#0x0000		! drive 0, head 0
	mov	cx,#0x0002		! sector 2, track 0
	mov	bx,#0x0200		! address = 512, in INITSEG
	mov	ax,#0x0200+SETUPLEN	! service 2, nr of sectors
	int	0x13			! read it
	
	jnc	ok_load_setup		! ok - continue
	mov	dx,#0x0000
	mov	ax,#0x0000		! reset the diskette
	int	0x13
	j	load_setup
```

这里调用了BIOS中断来读取磁盘的内容到内存，这里的0x13指明了要调用的中断功能，寄存器ah=0x02表示读取磁盘内容到内存，al=SETUPLEN（0x04）表示要读入4个扇区，寄存器cl=0x02表示要读取的起始扇区，寄存器ch=0x00表示要读的磁盘扇区所在的柱面号为0，寄存器dh=0x00表示要读的磁盘扇区所在的磁头号为0，寄存器dl表示读扇区所在驱动号为0，寄存器ES:BX表示要读入的目标内存地址（0x90200）。综上，这段代码的含义是：将0号驱动器中从0号柱面、0号磁头、2号扇区开始的4个扇区读入内存0x90200处（紧跟bootsect之后）

> BIOS中断中提供了很多操作硬件的功能，例如0x10表示视频服务（设置显示模式、字符输出、屏幕滚动），0x11表示设备列表（返回一个列表，列出系统中的设备），0x12表示内存服务（读取内存大小），0x13表示磁盘服务（读取/写入扇区、格式化磁盘、获取驱动器参数），0x15表示系统服务（等待指定时间、获取系统时间、访问扩展内存），0x16表示键盘服务（读取按键、检查按键状态）。AH寄存器用于指明要使用的功能。
>
> 不过BIOS中断服务只能在实模式下使用，后续我们会让CPU进入保护模式，因此BIOS服务会失效。

`jnc ok_load_setup`这条指令会检查CF位，如果CF被设置为1，表示操作失败，需要重置dx和ax，然后再次尝试加载setup；CF为0表示操作成功，跳转到后面的代码继续执行。

```assembly
ok_load_setup:

! Get disk drive parameters, specifically nr of sectors/track

	mov	dl,#0x00
	mov	ax,#0x0800		! AH=8 is get drive parameters
	int	0x13
	mov	ch,#0x00
	seg cs
	mov	sectors,cx
	mov	ax,#INITSEG
	mov	es,ax     		! ax = INITSEG，INITSEG = 0x9000，正是bootsect所在的位置

sectors:
	.word 0
```

这段代码用于在读取system之前获取一些磁盘参数，ah=0x08表示获取每个磁道的扇区数，dl=0x00表示要读取0号驱动器，通过0x13中断将每磁道的扇区数加载到CX寄存器的低6位中。`seg cs`是一个段超越前缀，它强制CPU使用**代码段寄存器（CS）**来访问接下来的操作数，而不是默认的数据段寄存器（DS）。然后将保存在CX寄存器中的每磁道扇区数加载到sectors中（两个字节），以备后用，最后将段寄存器es设置为0x90000。

```assembly
! Print some inane message

	mov	ah,#0x03		! read cursor pos
	xor	bh,bh
	int	0x10
	
	mov	cx,#26
	mov	bx,#0x0004		! page 0, attribute 7 (normal)
	mov	bp,#msg1
	mov	ax,#0x1301		! write string, move cursor
	int	0x10
	
msg1:
	.byte 13,10
	.ascii "MiniOS is booting..."  
	.byte 13,10,13,10
```

这里使用BIOS的0x10中断获取光标的位置，ah=0x03表示要使用读取光标位置的功能，bh=0x00表示读取第一个显示页，执行完0x10中断后，返回光标的行号到dh寄存器，返回光标的列号到dl寄存器。然后是通过0x10中断输出一个字符串（在msg1处定义），cx=26表示要显示的字符串长度为26个字符，bh=0x00表示显示的页码为0（第一页），bl=0x04表示红色前景（字符颜色）+黑色背景，ah=0x13表示写字符串，al=0x01表示写完字符后将光标移动到字符串末尾。

> 属性字节的结构：
>
> 位 7		  位 6			    位 5				位 4			    位 3			   位 2				位 1				位 0
>
> 闪烁		后景（B）	后景（G）	后景（R）	前景（I）	前景（B）	前景（G）	前景（R）
>
> `000` = 黑色，`001` = 蓝色，`010` = 绿色，`011` = 青色，`100` = 红色，`101` = 洋红色，`110` = 棕色（或黄色），`111` = 白色

```assembly
! ok, we've written the message, now
! we want to load the system (at 0x10000)
	mov	ax,#SYSSEG
	mov	es,ax		! segment of 0x010000
	call	read_it
	call	kill_motor
	
	jmpi	0,SETUPSEG
	
.org 510
boot_flag:
	.word 0xAA55
```

将段寄存器es设置为0x1000，然后调用read_it，这个子程序负责执行实际的磁盘读取操作，通常会利用 BIOS `INT 13h` 中断来读取磁盘上的多个扇区，将系统文件从磁盘加载到由 `ES` 寄存器指向的内存区域，关于这个例程的细节就不深究了，就是一个循环，然后一个磁道一个·磁道地读入，循环的条件是AX是否大于ENDSEG（SYSSEG + SYSSIZE）。kill_motor，这个子程序的作用是**关闭软驱马达**。在完成磁盘读取操作后，为了节省电量并减少噪音，软驱马达不再需要转动，所以通常会调用这个例程来停止它。最后需要注意的是，`.org 510`+`.word 0xAA55`会将中间偏移510之前的空余部分填入0，然后在该扇区的最后两个字节写入0x55AA（小端写法为0xAA55）。

bootsect总结：

- 将自身从内存0x7c00处移动到内存0x90000处
- 加载sect（磁盘第2号~5号四个扇区）到内存0x902000处
- 在显示器上输出系统提示
- 加载system（内核，从磁盘第6号扇区起，长度为SYSSIZE）到内存0x10000处
- 跳转到setup（0x90200）

### setup.s

先来看看Linus的注释：

```assembly
!	setup.s		(C) 1991 Linus Torvalds
!
! setup.s is responsible for getting the system data from the BIOS,
! and putting them into the appropriate places in system memory.
! both setup.s and system has been loaded by the bootblock.
!
! This code asks the bios for memory/disk/other parameters, and
! puts them in a "safe" place: 0x90000-0x901FF, ie where the
! boot-block used to be. It is then up to the protected mode
! system to read them from there before the area is overwritten
! for buffer-blocks.
```

setup负责通过BIOS获取一些系统数据（内存、磁盘或其它参数），并将其放入系统内存的合适位置（0x90000-0x901FF）处。后续内核需要将这些硬件参数保存起来，不然有可能被覆盖掉。这里强调了内核必须在特定时间内（即在将内存区域用于其他目的之前）从这个临时位置检索所有必要的系统数据。

```assembly
entry start
start:

! ok, the read went well so we get current cursor position and save it for
! posterity.
! 保存光标位置，以备后用，INITSEG为0x9000，将各种参数保存在0x90000处

	mov	ax,#INITSEG	! this is done in bootsect already, but...
	mov	ds,ax		! ds中保存的就是0x9000，后面的[0]表示 ds<<4 + 0 处，即指向0x90000
	mov	ah,#0x03	! read cursor pos
	xor	bh,bh
	int	0x10		! save it in known place, con_init fetches
	mov	[0],dx		! it from 0x90000.
! Get memory size (extended mem, kB)

	mov	ah,#0x88
	int	0x15
	mov	[2],ax
```

#INITSEG=0x9000加载到DS寄存器，即0x9000作为段基址，通过0x13BIOS中断读取光标位置到DX寄存器，然后将其存到0x90000处（[0]表示DS+偏移0）

```assembly
! Get memory size (extended mem, kB)

	mov	ah,#0x88
	int	0x15
	mov	[2],ax
	
! Get video-card data:

	mov	ah,#0x0f
	int	0x10
	mov	[4],bx		! bh = display page
	mov	[6],ax		! al = video mode, ah = window width

! check for EGA/VGA and some config parameters

	mov	ah,#0x12
	mov	bl,#0x10
	int	0x10
	mov	[8],ax
	mov	[10],bx
	mov	[12],cx

! Get hd0 data

	mov	ax,#0x0000
	mov	ds,ax
	lds	si,[4*0x41]
	mov	ax,#INITSEG
	mov	es,ax
	mov	di,#0x0080
	mov	cx,#0x10
	rep
	movsb

! Get hd1 data

	mov	ax,#0x0000
	mov	ds,ax
	lds	si,[4*0x46]
	mov	ax,#INITSEG
	mov	es,ax
	mov	di,#0x0090
	mov	cx,#0x10
	rep
	movsb
```

同理，获取内存大小、显卡、磁盘数据存入临时内存地址，并检查显示适配器。

```assembly
! Check that there IS a hd1 :-)

	mov	ax,#0x01500
	mov	dl,#0x81
	int	0x13
	jc	no_disk1
	cmp	ah,#3
	je	is_disk1
no_disk1:
	mov	ax,#INITSEG
	mov	es,ax
	mov	di,#0x0090
	mov	cx,#0x10
	mov	ax,#0x00
	rep
	stosb
is_disk1:
```

代码首先尝试通过 **`INT 13h`** 中断（`AH=0x15`，`DL=0x81`）来检测系统中是否存在第二个硬盘。如果**存在** (`jc no_disk1` 跳过)，则程序继续。如果**不存在**（`jc` 跳转到 `no_disk1`），则将用于存储第二个硬盘参数的内存区域清零，以标记该设备不存在。

```assembly
! now we want to move to protected mode ...

	cli			! no interrupts allowed !

! first we move the system to it's rightful place

	mov	ax,#0x0000
	cld			! 'direction'=0, movs moves forward
do_move:
	mov	es,ax		! destination segment
	add	ax,#0x1000
	cmp	ax,#0x9000
	jz	end_move
	mov	ds,ax		! source segment
	sub	di,di
	sub	si,si
	mov cx,#0x8000
	rep
	movsw
	jmp	do_move
```

这段代码用于将内核从内存0x10000处移动到内存0x0000起的位置。首先用过cli指令关闭中断，确保在内存复制过程中不会受到干扰，从而保证操作的原子性和完整性。cld指令清空方向标志位（Direction Flag），确保字符串操作指令（如 `movsw`）会从低地址向高地址移动数据。起始AX=0x0000，每次循环先将AX的值赋给ES（目的地址），在加上0x1000后赋给DS（原地址），即每次分块将内存地址0x10000~0x90000的数据移动到内存地址0x0000~0x80000处。cx=0x80000表示每次移动0x8000 * 2 = 0x10000个字节(64KB)的数据，`jmp	do_move`无条件跳转到循环开始处，进行下一块数据的移动，直至完成全部数据块的移动（AX=0x9000）。

```ass
end_move:
	mov	ax,#SETUPSEG	! right, forgot this at first. didn't work :-)
	mov	ds,ax
	lidt	idt_48		! load idt with 0,0
	lgdt	gdt_48		! load gdt with whatever appropriate

gdt:
	.word	0,0,0,0		! dummy

	.word	0x07FF		! 8Mb - limit=2047 (2048*4096=8Mb)
	.word	0x0000		! base address=0
	.word	0x9A00		! code read/exec
	.word	0x00C0		! granularity=4096, 386

	.word	0x07FF		! 8Mb - limit=2047 (2048*4096=8Mb)
	.word	0x0000		! base address=0
	.word	0x9200		! data read/write
	.word	0x00C0		! granularity=4096, 386

idt_48:
	.word	0			! idt limit=0
	.word	0,0			! idt base=0L

gdt_48:
	.word	0x800		! gdt limit=2048, 256 GDT entries
	.word	512+gdt,0x9	! gdt base = 0X9xxxx
```

这段代码加载了中断描述符表（IDT）和全局描述符表（GDT）的地址到CPU寄存器中，为保护模式的内存管理和中断处理做准备。它定义了 `gdt`、`idt_48` 和 `gdt_48` 三个数据结构，其中 `gdt` 包含了代码段和数据段的描述符，而 `idt_48` 和 `gdt_48` 则分别包含了 IDT 和 GDT 的基地址和大小。

gdt定义了全局描述符：

`gdt` 定义了内核在保护模式下使用的**内存段描述符**。每个描述符都是 8 字节长。

1. **第一个描述符 (`.word 0,0,0,0`)**：一个**空描述符**，这是 GDT 的标准要求。

2. **第二个描述符（代码段）**：

   - **`0x07FF`**：段的**限制**。当粒度为 4KB 时，这个值表示段的长度为 `(0x7FF + 1) * 4KB = 2048 * 4KB = 8MB`。

   - **`0x0000`**：段的**基地址**，这里为 `0`。

   - **`0x9A00`**：**访问权限字节**。
     - `9`（`1001b`）：`1` 表示存在（present），`00` 是特权级 0（最高权限），`1` 是代码段。
     - `A`（`1010b`）：`1` 是可读/可执行。

   - **`0x00C0`**：**其他标志**。
     - `C`（`1100b`）：`1` 表示粒度为 4KB，`1` 表示 32 位代码。

3. **第三个描述符（数据段）**：与代码段类似，但权限为可读/可写 (`9200h`)。

这里使用的是平坦模式，即没有通过偏移来区分段，而是将所有段都以0为起始地址，段的大小为整个地址空间，通过分页的方式来管理内存。那问题就来了，既然我们不再需要段了，为什么还要通过这种别扭的方式先分段再分页？这是因为x86架构的CPU为了保证后向兼容，仍然保留了分段模式，这种操作也是其能取得商业上成功的关键，但是对于开发者来说，这种后向兼容增加了大量不必要的代码，导致复杂性增加，像RISC-V这种后来设计的架构就简洁很多。