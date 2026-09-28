ENTRY(test_main)
MEMORY { FLASH (rx) : ORIGIN = 0x08000000, LENGTH = 1M
         RAM (rwx) : ORIGIN = 0x20000000, LENGTH = 128K }
SECTIONS {
 .text : { *(.text*) *(.rodata*) } > FLASH
 .ARM.exidx : { *(.ARM.exidx*) } > FLASH
 .data : { *(.data*) } > RAM AT> FLASH
 .bss : { *(.bss*) *(COMMON) } > RAM
}
