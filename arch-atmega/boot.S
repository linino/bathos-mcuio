#include <generated/autoconf.h>

/* We need to provide interrupt vectors here */
.extern do_jiffies_irq
.section .vectors, "ax"

	rjmp _bathos_start /* 0 */
	nop /* 2 */
	nop /* 4 */
	nop /* 6 */
	nop /* 8 */
	nop /* a */
	nop /* c */
	nop /* e */
	nop /* 10 */
	nop /* 12 */
	nop /* 14 */
	nop /* 16 */
	nop /* 18 */
	nop /* 1a */
	nop /* 1c */
	nop /* 1e */
	nop /* 20 */
	nop /* 22 */
	nop /* 24 */
	nop /* 26 */
#if defined CONFIG_MACH_ATMEGA32U4 && defined CONFIG_ATMEGA_USB_UART
	rjmp __vector_10   /* 28 */
#else
	nop
#endif
	nop /* 2a */
#if defined CONFIG_MACH_ATMEGA32U4 && defined CONFIG_ATMEGA_USB_UART
	rjmp __vector_11   /* 2c */
#else
	nop
#endif
	nop /* 2e */
#if defined CONFIG_MACH_ATMEGA328P
	rjmp __vector_16 /* 30 */
#else
	nop
#endif
	nop /* 32 */
	nop /* 34 */
	nop /* 36 */
	nop /* 38 */
	nop /* 3a */
	nop /* 3c */
	nop /* 3e */
	nop /* 40 */
	nop /* 42 */
	nop /* 44 */
	nop /* 46 */
#if defined CONFIG_MACH_ATMEGA328P && defined CONFIG_ATMEGA_UART
	rjmp __vector_18 /* 48 */
#else
	nop
#endif
	nop /* 4a */
	nop /* 4c */
	nop /* 4e */
	nop /* 50 */
	nop /* 52 */
	nop /* 54 */
	nop /* 56 */
	nop /* 58 */
	nop /* 5a */
#if defined CONFIG_MACH_ATMEGA32U4
	rjmp __vector_23 /* 5c */
#else
	nop
#endif
	nop /* 5e */
	nop /* 60 */
#if defined CONFIG_MACH_ATMEGA32U4 && defined CONFIG_ATMEGA_SPI
	rjmp __vector_24 /* 62 */
#else
	nop
#endif
#if defined CONFIG_MACH_ATMEGA32U4 && defined CONFIG_ATMEGA_UART
	rjmp __vector_25 /* 64 */
#else
	nop
#endif

/* Fllowins is boot code */
.section .boot, "ax"

.global _bathos_start

/* The following two symbols are mandated, or libgcc code creeps in */
.global __do_copy_data
.global __do_clear_bss

/* The following is defined in the linker script */
.extern __data_load_start
.extern __data_load_end
.extern __data_start
.extern __bss_start
.extern __bss_end
_bathos_start:
	/* r1  must always be 0 by convention, and clear irq in status  */
	eor r1, r1
	out 0x3f, r1
	/* set SP at address 0x3d, 0x3e -- assume 1k RAM + 0x60 for regs */
	//ldi r28, lo8(1023 + 0x60)
	//ldi r29, hi8(1023 + 0x60)
	ldi r28, lo8(0xaff)
	ldi r29, hi8(0xaff)
	out 0x3d, r28
	out 0x3e, r29

	/* Then, we must copy data from program memory to RAM */
__do_copy_data:
	/* Z (r30, 31) loads from program memory */
	ldi r30, lo8(__data_load_start)
	ldi r31, hi8(__data_load_start)
	/* "cpc" only compares with a register, so save high part here */
	ldi r16, hi8(__data_load_end)
	/* X (r26, r27) stores in RAM */
	ldi r26, lo8(__data_start)
	ldi r27, hi8(__data_start)

	/* __data_load_end (and start) is provided by linker script */
0:	cpi r30, lo8(__data_load_end)
	cpc r31, r16
	brge 1f
	lpm r2, Z+
	st X+, r2
	rjmp 0b

	/* Now, clear bss, use X again, and r16 for high part of end */
__do_clear_bss:
1:	ldi r26, lo8(__bss_start)
	ldi r27, hi8(__bss_start)
	ldi r16, hi8(__bss_end)
0:	cpi r26, lo8(__bss_end)
	cpc r27, r16
	brge 1f
	st X+, r1
	rjmp 0b
	
1:	/* Finally, run real code */
	call bathos_setup
	sei
	call avr_bathos_main
	rjmp _bathos_start
