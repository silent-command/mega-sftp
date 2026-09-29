; Hyppo trap from C. The C side fills hyppo_a/x/y/z, calls
; m65_hyppo_trap(), and reads hyppo_ra/rx/ry/rz and hyppo_rc (1 = the
; carry was set = success). The trap is a write of A to $D640 followed by
; CLV; the CPU may or may not execute the CLV, which is why it is a CLV.
; Z is a real register on the 45GS02: STZ stores it, and C code expects it
; to be zero.
        .section .text,"ax",@progbits
        .global m65_hyppo_trap
m65_hyppo_trap:
        php
        sei
        ldx     hyppo_x
        ldy     hyppo_y
        ldz     hyppo_z
        lda     hyppo_a
        sta     0xD640
        clv
        sta     hyppo_ra
        stx     hyppo_rx
        sty     hyppo_ry
        stz     hyppo_rz
        lda     #0
        rol
        sta     hyppo_rc
        ldz     #0
        plp
        rts
