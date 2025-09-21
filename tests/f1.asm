section .bss
resb 2 * 32

section file1data

strHello db "Hello world!", 0Ah  ; 自定义数据段
STRLEN equ $-strHello

section file1text      ; 自定义代码段
extern print           ; 告知编译器print为外部函数

global _start

_start:
    push STRLEN        ; 传入参数，字符串长度
    push strHello      ; 传入参数，待打印的字符串
    call print         ; print定义在f2.asm

; 返回到系统
    mov ebx, 0
    mov eax, 1          ; 系统调用号1: sys_exit
    int 0x80            ; 进行系统调用中断