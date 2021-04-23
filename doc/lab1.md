# lab1

## 518021910515 许嘉琦

## 练习1

1. ARMv8有两种长度的寄存器，AArch32和AArch64
2. ARM使用link register储存返回地址
3. ARM有四种exception levels EL0 EL1 EL2 EL3
4. 算术运算指令可以指定储存位置，也增加了很多类型，可以进行更多的操作
5. ARM的汇编指令集比X86数量更多，可以进行更多操作，也更复杂

## 练习2

`_start`: ` 0x0000000000080000`

## 练习3

```shell
os@ubuntu:~/chcore-lab-2021$ readelf -S build/kernel.img
There are 9 section headers, starting at offset 0x20cd8:

Section Headers:
  [Nr] Name              Type             Address           Offset
       Size              EntSize          Flags  Link  Info  Align
  [ 0]                   NULL             0000000000000000  00000000
       0000000000000000  0000000000000000           0     0     0
  [ 1] init              PROGBITS         0000000000080000  00010000
       000000000000b5b0  0000000000000008 WAX       0     0     4096
  [ 2] .text             PROGBITS         ffffff000008c000  0001c000
       00000000000011dc  0000000000000000  AX       0     0     8
  [ 3] .rodata           PROGBITS         ffffff0000090000  00020000
       00000000000000f8  0000000000000001 AMS       0     0     8
  [ 4] .bss              NOBITS           ffffff0000090100  000200f8
       0000000000008000  0000000000000000  WA       0     0     16
  [ 5] .comment          PROGBITS         0000000000000000  000200f8
       0000000000000032  0000000000000001  MS       0     0     1
  [ 6] .symtab           SYMTAB           0000000000000000  00020130
       0000000000000858  0000000000000018           7    46     8
  [ 7] .strtab           STRTAB           0000000000000000  00020988
       000000000000030f  0000000000000000           0     0     1
  [ 8] .shstrtab         STRTAB           0000000000000000  00020c97
       000000000000003c  0000000000000000           0     0     1
Key to Flags:
  W (write), A (alloc), X (execute), M (merge), S (strings), I (info),
  L (link order), O (extra OS processing required), G (group), T (TLS),
  C (compressed), x (unknown), o (OS specific), E (exclude),
  p (processor specific)
```

`init`段的地址为`0x0000000000080000`，对应`_start`，位于`boot/start.S`中

```nasm
BEGIN_FUNC(_start)
	mrs	x8, mpidr_el1
	and	x8, x8,	#0xFF
	cbz	x8, primary

  /* hang all secondary processors before we intorduce multi-processors */
secondary_hang:
	bl secondary_hang
```

`start.S`部分代码如上所示，读取了`mpidr_el1`，判断如果是单核处理器，则跳转到`primary`执行，否则会在挂在`secondary_hang`

## 练习4

```shell
os@ubuntu:~/chcore-lab-2021$ objdump -h build/kernel.img

build/kernel.img:     file format elf64-little

Sections:
Idx Name          Size      VMA               LMA               File off  Algn
  0 init          0000b5b0  0000000000080000  0000000000080000  00010000  2**12
                  CONTENTS, ALLOC, LOAD, CODE
  1 .text         000011dc  ffffff000008c000  000000000008c000  0001c000  2**3
                  CONTENTS, ALLOC, LOAD, READONLY, CODE
  2 .rodata       000000f8  ffffff0000090000  0000000000090000  00020000  2**3
                  CONTENTS, ALLOC, LOAD, READONLY, DATA
  3 .bss          00008000  ffffff0000090100  0000000000090100  000200f8  2**4
                  ALLOC
  4 .comment      00000032  0000000000000000  0000000000000000  000200f8  2**0
                  CONTENTS, READONLY

```

`init`的LMA与VMA相等，`.text`, `.rodata`, `.bss`, `.comment`的LMA都与VMA相差`0xffffff0000000000`

因为`init`保存的是`bootloader`的代码和数据，其余部分都为真正的`ChCore`内核

`bootloader`初始化`页表`和`MMU`，记录VMA到LMA的映射关系

## 练习5

```c
s = print_buf + PRINT_BUF_LEN;
	*s = '\0';

while(u > 0){
	char ca;
	t = u % base;
	if(base > 10){
		if(t < 10){
			ca = t + '0';
		}
		else{
			if(letbase == 1){
				ca = t - 10 + 'A';
			}
			else
			{
				ca = t - 10 + 'a';
			}
		}
	}
	else
	{
		ca = t + '0';
	}
	*--s = ca;
	u = u/base;
}
```

## 练习6

```nasm
BEGIN_FUNC(start_kernel)
    /* 
     * Code in bootloader specified only the primary 
     * cpu with MPIDR = 0 can be boot here. So we directly
     * set the TPIDR_EL1 to 0, which represent the logical
     * cpuid in the kernel 
     */
    mov     x3, #0
    msr     TPIDR_EL1, x3

    ldr     x2, =kernel_stack
    add     x2, x2, KERNEL_STACK_SIZE
    mov     sp, x2
    bl      main
END_FUNC(start_kernel)

```

初始化`SP`和`FP`的代码在`/kernel/head.S`中

`sp = kernel_stack + KERNEL_STACK_SIZE = 0xffffff0000092100`

```c
ALIGN(STACK_ALIGNMENT)
char kernel_stack[PLAT_CPU_NUM][KERNEL_STACK_SIZE];
```

`kernel_stack定义`在`/boot/init_c.c`中

```c
/* Leave 8K space to kernel stack */
#define KERNEL_STACK_SIZE       (8192)
```

`KERNEL_STACK_SIZE`定义在`/kernel/common/vars.h`中

`kernel_statck = 0xffffff0000090100`与`.bss`的起始地址相同。内核在`.bss`分配一整块大小为`KERNEL_STACK_SIZE`连续区域作为内核栈。

## 练习7

```shell
(gdb) x/10g $x29
0xffffff00000920b0 <kernel_stack+8112>:	0xffffff00000920d0 <kernel_stack+8144>	0xffffff000008c070 <stack_test+80>
0xffffff00000920c0 <kernel_stack+8128>:	0x5	0xffffffc0
0xffffff00000920d0 <kernel_stack+8144>:	0xffffff00000920f0 <kernel_stack+8176>	0xffffff000008c0d4 <main+72>
0xffffff00000920e0 <kernel_stack+8160>:	0x0	0xffffffc0
0xffffff00000920f0 <kernel_stack+8176>:	0x0	0xffffff000008c018
(gdb) x/10g $x29
0xffffff0000092090 <kernel_stack+8080>:	0xffffff00000920b0 <kernel_stack+8112>	0xffffff000008c070 <stack_test+80>
0xffffff00000920a0 <kernel_stack+8096>:	0x4	0xffffffc0
0xffffff00000920b0 <kernel_stack+8112>:	0xffffff00000920d0 <kernel_stack+8144>	0xffffff000008c070 <stack_test+80>
0xffffff00000920c0 <kernel_stack+8128>:	0x5	0xffffffc0
0xffffff00000920d0 <kernel_stack+8144>:	0xffffff00000920f0 <kernel_stack+8176>	0xffffff000008c0d4 <main+72>
```

每个stack_test递归嵌套级别将三个64位值压入堆栈，分别是函数调用之前fp的位置，函数的返回地址，函数的参数。

## 练习8



![截屏2021-03-19 下午6.54.07.png](/var/folders/07/t6n9hdtj60n_3yc72wdhn6p80000gn/T/TemporaryItems/（screencaptureui正在存储文稿，已完成10）/截屏2021-03-19%20下午6.54.07.png)

## 练习9

一个参数的情况

```c
u64* fp=(u64*)(*(u64*)read_fp());

while(fp!=0){

printk("LR %lx FP %lx Args %d \n",*(fp+1),fp,*(fp-2));

fp=(u64*)*fp;

}
```

假设有多个参数

```c
u64* fp=(u64*)(*(u64*)read_fp());
	while(fp!=0){
		printk("LR %llx FP %llx Args",*(fp+1),fp);
		u64* a = fp - 2;
		for(int i = 5;i > 0;i--){
			printk("%llx ", *a);
			a++;
		}
		printk("\n");
		fp=(u64*)*fp;
	}
```


