OUTPUT_FORMAT("elf32-avr")
OUTPUT_ARCH(avr:4)
ENTRY(_bathos_start)

regs = 0;
/* jiffies is maintained in software */

SECTIONS
{
	.vectors : {
		*(.vectors.*)
	}
}
