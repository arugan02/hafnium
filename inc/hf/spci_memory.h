/*
 * Copyright 2019 The Hafnium Authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include "hf/mpool.h"
#include "hf/vm.h"

#include "vmapi/hf/spci.h"

struct spci_value spci_memory_send(struct vm_locked to_locked,
				   struct vm_locked from_locked,
				   struct spci_memory_region *memory_region,
				   uint32_t memory_share_size,
				   uint32_t share_func,
				   struct mpool *page_pool);