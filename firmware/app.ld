/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Blue Hare memory map.
 * Code + constants execute in place from XIP. app.bin offset 0 = 0x02000120 is the boot stub (firmware/stub, from
 * ChoralRoot FM-1, 2026-10-24: 0xEE0 bytes, flash 0x4120..0x4FFF); the app is linked from the next sector, XIP
 * 0x02001000 = flash 0x5000, up to the commit marker's sector at flash 0x91000 (docs/BOOT-SAFETY.md).
 * 0x01C00000..0x01C06000 holds .ram_text (code that must run while the flash
 * is busy: the flash driver), then .ram_fast (the VA family's loops, for speed); 0x01C06000..0x01C08000 stays for
 * RAM-run debug.
 * RAM  : .data/.bss (96 KiB)
 * POOL : big zero-initialised buffers (FX lines, canvases), section .pool
 * STACKS with 256-byte guard bands; NOINIT survives resets and UBOOT entry.
 * 0x01C7FD50.. is reserved: boot_info, boot-state mailbox, vector table. */
MEMORY
{
    XIP    (rx)  : ORIGIN = 0x02001000, LENGTH = 0x8C000
    RAMTEXT (rwx): ORIGIN = 0x01C00000, LENGTH = 0x6000
    RAM    (rwx) : ORIGIN = 0x01C08000, LENGTH = 96K
    POOL   (rw)  : ORIGIN = 0x01C20000, LENGTH = 0x54000
    NOINIT (rw)  : ORIGIN = 0x01C7C020, LENGTH = 0x3D30   /* (0x01C7C000..1F: the boot stub's rescue word and mailbox) */
}
SECTIONS
{
    .text : { KEEP(*(.text.crt0)) *(.text*) *(.rodata*) . = ALIGN(4); _etext = .; } > XIP
    /* .ram_fast (2026-10-24): hot code run from RAM for speed (eng_va.c VA_HOT, after ChoralRoot's CR_RAM_VA), every
     * call in it inlined (build.py: no calls in .ram_text) */
    .ram_text : { _rt_start = .; KEEP(*(.ram_text*)) KEEP(*(.ram_fast*)) . = ALIGN(4); } > RAMTEXT AT > XIP
    _rt_load = LOADADDR(.ram_text);
    /* (2026-10-24, from ChoralRoot FM-1) the second core's waiting loop (hal/fm1_cpu1.h, docs/DUALCORE.md): right after
     * .ram_text, in RAM and in the image; main.c copies both in one run and the guard's RAM code window covers both */
    .c1_text : { _c1_start = .; KEEP(*(.c1_text*)) . = ALIGN(4); _c1_end = .; } > RAMTEXT AT > XIP
    _c1_load = LOADADDR(.c1_text);
    _rt_end = _c1_end;
    .data : { . = ALIGN(4); _data_start = .; *(.data*) *(.sdata*) . = ALIGN(4); _data_end = .; } > RAM AT > XIP
    _data_load = LOADADDR(.data);
    .bss (NOLOAD) : { _bss_start = .; *(.bss*) *(.sbss*) *(COMMON) . = ALIGN(4); _bss_end = .; } > RAM
    .pool (NOLOAD) : { _pool_start = .; *(.pool*) . = ALIGN(4); _pool_end = .; } > POOL
    .noinit (NOLOAD) : { *(.noinit*) } > NOINIT
    _guard0 = 0x01C74000; _ustack_lo = 0x01C74100; _ustack_top = 0x01C7A000;
    _guard1 = 0x01C7A000; _sstack_lo = 0x01C7A100; _sstack_top = 0x01C7C000;
    /DISCARD/ : { *(.comment) *(.note*) }
}
