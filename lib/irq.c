/*
 * Copyright (c) dog hunter AG - Zug - CH
 * General Public License version 2 (GPLv2)
 * Author: Davide Ciminaghi <ciminaghi@gnudd.com>
 */
#include <bathos/bathos.h>
#include <bathos/errno.h>
#include <bathos/irq.h>
#include <bathos/interrupt.h>
#include <bathos/irq-controller.h>

int bathos_request_irq(int irq, bathos_irq_handler handler)
{
	struct bathos_irq_controller *ctrl = bathos_irq_to_ctrl(irq);
	int ret = -EINVAL;

	if (!ctrl)
		return ret;
	ret = arch_set_irq_vector(irq, handler);
	if (ret < 0)
		return ret;
	ctrl->enable_irq(ctrl, irq);
	return 0;
}

int __isr bathos_enable_irq(int irq)
{
	struct bathos_irq_controller *ctrl = bathos_irq_to_ctrl(irq);

	if (!ctrl)
		return -EINVAL;
	ctrl->enable_irq(ctrl, irq);
	return 0;
}

int __isr bathos_disable_irq(int irq)
{
	struct bathos_irq_controller *ctrl = bathos_irq_to_ctrl(irq);

	if (!ctrl)
		return -EINVAL;
	ctrl->disable_irq(ctrl, irq);
	return 0;
}

int __isr bathos_set_pending_irq(int irq)
{
	struct bathos_irq_controller *ctrl = bathos_irq_to_ctrl(irq);

	if (!ctrl)
		return -EINVAL;
	ctrl->set_pending(ctrl, irq);
	return 0;
}

int __isr bathos_clear_pending_irq(int irq)
{
	struct bathos_irq_controller *ctrl = bathos_irq_to_ctrl(irq);

	if (!ctrl)
		return -EINVAL;
	ctrl->clear_pending(ctrl, irq);
	return 0;
}

int __isr bathos_mask_ack_irq(int irq)
{
	struct bathos_irq_controller *ctrl = bathos_irq_to_ctrl(irq);

	if (!ctrl)
		return -EINVAL;
	ctrl->mask_ack(ctrl, irq);
	return 0;
}

int __isr bathos_unmask_irq(int irq)
{
	struct bathos_irq_controller *ctrl = bathos_irq_to_ctrl(irq);

	if (!ctrl)
		return -EINVAL;
	ctrl->unmask(ctrl, irq);
	return 0;
}
