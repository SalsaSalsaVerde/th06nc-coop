; Register-preserving entry stubs for every inline hook (docs/11).
;
; th06nc.exe was built with whole-program optimization, so a caller may keep
; values in volatile registers across a call to an internal function it knows
; doesn't touch them. The player hit test does exactly that around its call
; to the bomb-clear-zone test (RDX, R8 and R9 are read again after the call).
; A C++ detour honors only the standard ABI and clobbers them, which crashed
; the game on the first bullet. Every hook therefore enters through one of
; these stubs: it saves the caller's volatile registers, calls the C++ detour
; with the same arguments (the first four in their registers untouched, up to
; eight more copied from the caller's stack), then restores everything except
; the return value.
;
; g_hookThunkTargets[i] holds the C++ detour for stub i; g_hookThunkVoid[i]
; is nonzero when the hooked function returns nothing, in which case RAX is
; restored too (a caller may keep a value there across a void call).

EXTERN g_hookThunkTargets:QWORD
EXTERN g_hookThunkVoid:BYTE

; Frame (after push rbp / sub rsp,100h; rsp is 16-byte aligned here):
;   [rsp+00h..20h)  shadow space for the detour
;   [rsp+20h..60h)  up to eight forwarded stack arguments
;   [rsp+60h..A0h)  rax rcx rdx r8 r9 r10 r11
;   [rsp+A0h..100h) xmm0..xmm5
; Caller's stack arguments start at [rbp+30h] (return address at [rbp+8],
; the caller's shadow space at [rbp+10h..30h)).

HOOK_THUNK MACRO index
PUBLIC HookThunk&index
HookThunk&index PROC FRAME
    push rbp
    .pushreg rbp
    mov rbp, rsp
    .setframe rbp, 0
    sub rsp, 100h
    .allocstack 100h
    .endprolog

    mov [rsp+60h], rax
    mov [rsp+68h], rcx
    mov [rsp+70h], rdx
    mov [rsp+78h], r8
    mov [rsp+80h], r9
    mov [rsp+88h], r10
    mov [rsp+90h], r11
    movaps xmmword ptr [rsp+0A0h], xmm0
    movaps xmmword ptr [rsp+0B0h], xmm1
    movaps xmmword ptr [rsp+0C0h], xmm2
    movaps xmmword ptr [rsp+0D0h], xmm3
    movaps xmmword ptr [rsp+0E0h], xmm4
    movaps xmmword ptr [rsp+0F0h], xmm5

    mov rax, [rbp+30h]
    mov [rsp+20h], rax
    mov rax, [rbp+38h]
    mov [rsp+28h], rax
    mov rax, [rbp+40h]
    mov [rsp+30h], rax
    mov rax, [rbp+48h]
    mov [rsp+38h], rax
    mov rax, [rbp+50h]
    mov [rsp+40h], rax
    mov rax, [rbp+58h]
    mov [rsp+48h], rax
    mov rax, [rbp+60h]
    mov [rsp+50h], rax
    mov rax, [rbp+68h]
    mov [rsp+58h], rax

    call qword ptr [g_hookThunkTargets + index * 8]

    movaps xmm0, xmmword ptr [rsp+0A0h]
    movaps xmm1, xmmword ptr [rsp+0B0h]
    movaps xmm2, xmmword ptr [rsp+0C0h]
    movaps xmm3, xmmword ptr [rsp+0D0h]
    movaps xmm4, xmmword ptr [rsp+0E0h]
    movaps xmm5, xmmword ptr [rsp+0F0h]
    mov rcx, [rsp+68h]
    mov rdx, [rsp+70h]
    mov r8, [rsp+78h]
    mov r9, [rsp+80h]
    mov r10, [rsp+88h]
    mov r11, [rsp+90h]
    cmp byte ptr [g_hookThunkVoid + index], 0
    je @F
    mov rax, [rsp+60h]
@@:
    lea rsp, [rbp]
    pop rbp
    ret
HookThunk&index ENDP
ENDM

.code

thunkIndex = 0
REPEAT 32
    HOOK_THUNK %thunkIndex
    thunkIndex = thunkIndex + 1
ENDM

END
