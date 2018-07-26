#include "cpu.h"

#include "arch_cpu.h"
#include "dlog.h"
#include "std.h"
#include "vm.h"

/* The stack to be used by the CPUs. */
alignas(2 * sizeof(size_t)) static char callstacks[STACK_SIZE * MAX_CPUS];

/* State of all supported CPUs. The stack of the first one is initialized. */
struct cpu cpus[MAX_CPUS] = {
	{
		.is_on = 1,
		.stack_bottom = callstacks + STACK_SIZE,
	},
};

void cpu_module_init(void)
{
	size_t i;

	/* Initialize all CPUs. */
	for (i = 0; i < MAX_CPUS; i++) {
		struct cpu *c = cpus + i;
		cpu_init(c);
		c->id = i; /* TODO: Initialize ID. */
		c->stack_bottom = callstacks + STACK_SIZE * (i + 1);
	}
}

size_t cpu_index(struct cpu *c)
{
	return cpus - c;
}

void cpu_init(struct cpu *c)
{
	/* TODO: Assumes that c is zeroed out already. */
	sl_init(&c->lock);
	c->irq_disable_count = 1;
}

void cpu_irq_enable(struct cpu *c)
{
	c->irq_disable_count--;
	if (!c->irq_disable_count) {
		arch_irq_enable();
	}
}

void cpu_irq_disable(struct cpu *c)
{
	if (!c->irq_disable_count) {
		arch_irq_disable();
	}
	c->irq_disable_count++;
}

/**
 * Turns CPU on and returns the previous state.
 */
bool cpu_on(struct cpu *c)
{
	bool prev;

	sl_lock(&c->lock);
	prev = c->is_on;
	c->is_on = true;
	sl_unlock(&c->lock);

	if (!prev) {
		/* The CPU is currently off, we need to turn it on. */
		arch_cpu_on(c->id, c);
	}

	return prev;
}

/*
 * This must be called only from the same CPU.
 */
void cpu_off(struct cpu *c)
{
	sl_lock(&c->lock);
	c->is_on = false;
	sl_unlock(&c->lock);

	arch_cpu_off();
}

void vcpu_init(struct vcpu *vcpu, struct vm *vm)
{
	memset(vcpu, 0, sizeof(*vcpu));
	sl_init(&vcpu->lock);
	vcpu->vm = vm;
	vcpu->state = vcpu_state_off;
	/* TODO: Initialize vmid register. */
}

void vcpu_on(struct vcpu *vcpu)
{
	sl_lock(&vcpu->lock);
	vcpu->state = vcpu_state_ready;
	sl_unlock(&vcpu->lock);
}

void vcpu_off(struct vcpu *vcpu)
{
	sl_lock(&vcpu->lock);
	vcpu->state = vcpu_state_off;
	sl_unlock(&vcpu->lock);
}
