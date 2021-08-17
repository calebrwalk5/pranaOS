/*
 * Copyright (c) 2021, Krisna Pranav
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef _KERNEL_PLATFORM_AARCH32_INIT_H
#define _KERNEL_PLATFORM_AARCH32_INIT_H

#include <libkernel/types.h>

void platform_init_boot_cpu();
void platform_setup_boot_cpu();
void platform_setup_secondary_cpu();
void platform_drivers_setup();

#endif /* _KERNEL_PLATFORM_AARCH32_INIT_H */