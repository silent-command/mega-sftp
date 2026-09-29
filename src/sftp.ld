/* The client: the MEGA65 target's link.ld with a second region,
 * $E000-$FEFF in bank 0 -- RAM once m65_own_vectors has mapped the
 * KERNAL out, below mega-net's vector stub at $FF00 (PLATFORM.md). A
 * PRG cannot load there, so the objects named below make a second disk
 * image, HIGH, which ck_boot loads with the bank images (the IRC
 * client's pattern, irc 5.17). They are compiled apart, without LTO,
 * so they can be named here; a pattern that matches nothing is silent,
 * so check the map after touching this (gemini 5.8). */

__basic_zp_start = 0x0002;
__basic_zp_end = 0x0090;

MEMORY {
    ram (rw) : ORIGIN = 0x2001, LENGTH = 0xafff
    hi (rwx) : ORIGIN = 0xE000, LENGTH = 0x1F00
}

__rc0 = __basic_zp_start;
INCLUDE imag-regs.ld
__basic_zp_size = __basic_zp_end - __basic_zp_start;
MEMORY { zp : ORIGIN = __rc31 + 1, LENGTH = __basic_zp_end - (__rc31 + 1) }

INPUT(basic-header.o)
INPUT(unmap-basic.o)

REGION_ALIAS("c_readonly", ram)
REGION_ALIAS("c_writeable", ram)

SECTIONS {
    /* BASIC program that jumps to _start. */
    .basic_header : { *(.basic_header) }

    /* the call-light modules: the disk layer, the known hosts and the
     * hypervisor services (build.py HIGH_OBJS) */
    .text.hi : {
        *m65_cbmdos.o(.text .text.* .rodata .rodata.*)
        *m65_f011.o(.text .text.* .rodata .rodata.*)
        *hosts.o(.text .text.* .rodata .rodata.*)
        *m65_hyppo.o(.text .text.* .rodata .rodata.*)
    } > hi
    .bss.hi (NOLOAD) : {
        __hi_bss_start = .;
        *m65_cbmdos.o(.bss .bss.* .data .data.*)
        *m65_f011.o(.bss .bss.*) *hosts.o(.bss .bss.*) *m65_hyppo.o(.bss .bss.*)
        __hi_bss_end = .;
    } > hi

    INCLUDE c.ld
}

__stack = 0xd000;

/* the whole first region, then the second trimmed: build.py splits the
 * file after 2 + 0xAFFF bytes into sftp.prg and HIGH */
OUTPUT_FORMAT {
    SHORT(0x2001)
    FULL(ram)
    TRIM(hi)
}
