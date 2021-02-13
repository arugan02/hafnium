/*
 * Copyright 2018 The Hafnium Authors.
 *
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file or at
 * https://opensource.org/licenses/BSD-3-Clause.
 */

#pragma once

#include <stdatomic.h>

#include "hf/arch/types.h"

#include "hf/cpu.h"
#include "hf/list.h"
#include "hf/mm.h"
#include "hf/mpool.h"

#include "vmapi/hf/ffa.h"

#define MAX_SMCS 32
#define LOG_BUFFER_SIZE 256

/**
 * The state of an RX buffer.
 *
 * EMPTY is the initial state. The follow state transitions are possible:
 * * EMPTY => RECEIVED: message sent to the VM.
 * * RECEIVED => READ: secondary VM returns from FFA_MSG_WAIT or
 *   FFA_MSG_POLL, or primary VM returns from FFA_RUN with an FFA_MSG_SEND
 *   where the receiver is itself.
 * * READ => EMPTY: VM called FFA_RX_RELEASE.
 */
enum mailbox_state {
	/** There is no message in the mailbox. */
	MAILBOX_STATE_EMPTY,

	/** There is a message in the mailbox that is waiting for a reader. */
	MAILBOX_STATE_RECEIVED,

	/** There is a message in the mailbox that has been read. */
	MAILBOX_STATE_READ,
};

struct wait_entry {
	/** The VM that is waiting for a mailbox to become writable. */
	struct vm *waiting_vm;

	/**
	 * Links used to add entry to a VM's waiter_list. This is protected by
	 * the notifying VM's lock.
	 */
	struct list_entry wait_links;

	/**
	 * Links used to add entry to a VM's ready_list. This is protected by
	 * the waiting VM's lock.
	 */
	struct list_entry ready_links;
};

struct mailbox {
	enum mailbox_state state;
	void *recv;
	const void *send;

	/** The ID of the VM which sent the message currently in `recv`. */
	ffa_vm_id_t recv_sender;

	/** The size of the message currently in `recv`. */
	uint32_t recv_size;

	/**
	 * The FF-A function ID to use to deliver the message currently in
	 * `recv`.
	 */
	uint32_t recv_func;

	/**
	 * List of wait_entry structs representing VMs that want to be notified
	 * when the mailbox becomes writable. Once the mailbox does become
	 * writable, the entry is removed from this list and added to the
	 * waiting VM's ready_list.
	 */
	struct list_entry waiter_list;

	/**
	 * List of wait_entry structs representing VMs whose mailboxes became
	 * writable since the owner of the mailbox registers for notification.
	 */
	struct list_entry ready_list;
};

struct notifications_state {
	/**
	 * To keep track of the notifications pending.
	 * Set on call to FFA_NOTIFICATION_SET, and cleared on call to
	 * FFA_NOTIFICATION_GET.
	 */
	ffa_notifications_bitmap_t pending;

	/**
	 * Set on FFA_NOTIFICATION_INFO_GET to keep track of the notifications
	 * whose information has been retrieved by the referred ABI.
	 * Cleared on call to FFA_NOTIFICATION_GET.
	 */
	ffa_notifications_bitmap_t info_get_retrieved;
};

struct notifications {
	/**
	 * The following array maps the notifications to the bound FF-A
	 * endpoint.
	 * The index in the bindings array relates to the notification
	 * ID, and bit position in 'ffa_notifications_bitmap_t'.
	 */
	ffa_vm_id_t bindings_sender_id[MAX_FFA_NOTIFICATIONS];
	ffa_notifications_bitmap_t bindings_per_vcpu;

	/* The index of the array below relates to the ID of the VCPU. */
	struct notifications_state per_vcpu[MAX_CPUS];
	struct notifications_state global;
};

struct smc_whitelist {
	uint32_t smcs[MAX_SMCS];
	uint16_t smc_count;
	bool permissive;
};

struct vm {
	ffa_vm_id_t id;
	struct ffa_uuid uuid;
	struct smc_whitelist smc_whitelist;

	/** See api.c for the partial ordering on locks. */
	struct spinlock lock;
	ffa_vcpu_count_t vcpu_count;
	struct vcpu vcpus[MAX_CPUS];
	struct mm_ptable ptable;
	struct mailbox mailbox;

	struct {
		/**
		 * State structures for notifications coming from VMs or coming
		 * from SPs. Both fields are maintained by the SPMC.
		 * The hypervisor ignores the 'from_sp' field, given VM
		 * notifications from SPs are managed by the SPMC.
		 */
		struct notifications from_vm;
		struct notifications from_sp;
		/* TODO: include framework notifications */
		bool enabled;
	} notifications;

	char log_buffer[LOG_BUFFER_SIZE];
	uint16_t log_buffer_length;

	/**
	 * Wait entries to be used when waiting on other VM mailboxes. See
	 * comments on `struct wait_entry` for the lock discipline of these.
	 */
	struct wait_entry wait_entries[MAX_VMS];

	atomic_bool aborting;

	/**
	 * Booting parameters (FF-A SP partitions).
	 */
	bool initialized;
	uint16_t boot_order;
	uint8_t messaging_method;
	bool managed_exit;
	struct vm *next_boot;

	/**
	 * Secondary entry point supplied by FFA_SECONDARY_EP_REGISTER used
	 * for cold and warm boot of SP execution contexts.
	 */
	ipaddr_t secondary_ep;

	/** Arch-specific VM information. */
	struct arch_vm arch;
	bool el0_partition;
};

/** Encapsulates a VM whose lock is held. */
struct vm_locked {
	struct vm *vm;
};

/** Container for two vm_locked structures */
struct two_vm_locked {
	struct vm_locked vm1;
	struct vm_locked vm2;
};

struct vm *vm_init(ffa_vm_id_t id, ffa_vcpu_count_t vcpu_count,
		   struct mpool *ppool, bool el0_partition);
bool vm_init_next(ffa_vcpu_count_t vcpu_count, struct mpool *ppool,
		  struct vm **new_vm, bool el0_partition);
ffa_vm_count_t vm_get_count(void);
struct vm *vm_find(ffa_vm_id_t id);
struct vm_locked vm_find_locked(ffa_vm_id_t id);
struct vm *vm_find_index(uint16_t index);
struct vm_locked vm_lock(struct vm *vm);
struct two_vm_locked vm_lock_both(struct vm *vm1, struct vm *vm2);
void vm_unlock(struct vm_locked *locked);
struct vcpu *vm_get_vcpu(struct vm *vm, ffa_vcpu_index_t vcpu_index);
struct wait_entry *vm_get_wait_entry(struct vm *vm, ffa_vm_id_t for_vm);
ffa_vm_id_t vm_id_for_wait_entry(struct vm *vm, struct wait_entry *entry);
bool vm_id_is_current_world(ffa_vm_id_t vm_id);

bool vm_identity_map(struct vm_locked vm_locked, paddr_t begin, paddr_t end,
		     uint32_t mode, struct mpool *ppool, ipaddr_t *ipa);
bool vm_identity_prepare(struct vm_locked vm_locked, paddr_t begin, paddr_t end,
			 uint32_t mode, struct mpool *ppool);
void vm_identity_commit(struct vm_locked vm_locked, paddr_t begin, paddr_t end,
			uint32_t mode, struct mpool *ppool, ipaddr_t *ipa);
bool vm_unmap(struct vm_locked vm_locked, paddr_t begin, paddr_t end,
	      struct mpool *ppool);
bool vm_unmap_hypervisor(struct vm_locked vm_locked, struct mpool *ppool);

void vm_update_boot(struct vm *vm);
struct vm *vm_get_first_boot(void);

void vm_notifications_init_bindings(struct notifications *n);
bool vm_mem_get_mode(struct vm_locked vm_locked, ipaddr_t begin, ipaddr_t end,
		     uint32_t *mode);
