#ifndef __INTERRUPT_H__
#define __INTERRUPT_H__

#include <arch/bathos-arch.h>

#ifndef interrupt_disable
#define interrupt_disable(flags)
#endif

#ifndef interrupt_restore
#define interrupt_restore(flags)
#endif

#ifndef __isr
#define __isr
#endif

#endif /* __INTERRUPT_H__ */
