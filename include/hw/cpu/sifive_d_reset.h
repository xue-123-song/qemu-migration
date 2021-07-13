/*
 * QEMU Test Finisher interface
 *
 * Copyright (c) 2018 SiFive, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef HW_SIFIVE_D_RESET_H
#define HW_SIFIVE_D_RESET_H

#include "hw/sysbus.h"
#include "qom/object.h"

#define TYPE_SIFIVE_D_RESET "riscv.sifive.dreset"

typedef struct SiFiveDResetState SiFiveDResetState;
DECLARE_INSTANCE_CHECKER(SiFiveDResetState, SIFIVE_D_TEST,
                         TYPE_SIFIVE_D_RESET)

struct SiFiveDResetState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion mmio;
};

enum {
    FINISHER_FAIL = 0x3333,
    FINISHER_PASS = 0x5555,
    FINISHER_RESET = 0x7777
};

DeviceState *sifive_d_reset_create(hwaddr addr);

#endif
