section .text
use64

global entry
entry:
    mov rax, rcx
    add rax, rdx
    out 78h, al
