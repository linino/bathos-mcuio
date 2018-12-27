
#include <avr/interrupt.h>
#include <bathos/bathos.h>
#include <bathos/event.h>

/* Reset vector table entry */
static void __attribute__((used, naked, noreturn,
			   section(xstr(xcat(.vectors.,0)))))
__jump_0(void)
{
	__asm__ __volatile__ ("rjmp _bathos_start\nnop" ::);
}
