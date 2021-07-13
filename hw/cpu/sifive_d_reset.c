/*
 * QEMU SiFive Test Finisher
 *
 * Copyright (c) 2018 SiFive, Inc.
 *
 * Test finisher memory mapped device used to exit simulation
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
#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qemu/module.h"
#include "hw/sysbus.h"
#include "qemu/log.h"
#include "qemu-common.h"
#include "sysemu/runstate.h"
#include "hw/cpu/core.h"
#include "cpu.h"
#include "hw/cpu/sifive_d_reset.h"
#include "sysemu/hw_accel.h"


static uint64_t d_reset_read(void *opaque, hwaddr addr, unsigned int size)
{
    return 0;
}

/*A core-granularity reset unit for confidential compute with hardware domian partition.

addr = 0 --> system-wide reset (hart 0 is a trusted host core).
addr = 4 --> hart 1 reset
addr = 8 --> hart 2 reset
...
*/
static void d_reset_write(void *opaque, hwaddr addr,
           uint64_t val64, unsigned int size)
{
    bool system_wide = false;
    qemu_log_mask(CPU_LOG_OPENSBI, "%s: write: addr=0x%x val=0x%016" PRIx64 "\n", __func__, (unsigned)addr, val64);
    if (addr == 0){
        system_wide  = true;
    }
    int hartid = addr >> 2;
    CPUState *cpu = qemu_get_cpu(hartid);
    {
        int status = val64 & 0xffff;
        int code = (val64 >> 16) & 0xffff;
        switch (status) {
        case FINISHER_FAIL:
            exit(code);
        case FINISHER_PASS:
            exit(0);
        case FINISHER_RESET:
        {
            if(system_wide){
                qemu_system_reset_request(SHUTDOWN_CAUSE_GUEST_RESET);
            }else{
                cpu_reset(cpu);
                qemu_log_mask(CPU_LOG_OPENSBI, "%s: cpu_reset %d\n", __func__, hartid);
                // qemu synchronizes the cpu state after a CPU RESET
                cpu_synchronize_post_reset(cpu);
                qemu_log_mask(CPU_LOG_OPENSBI, "%s: cpu_synchronize_post_reset %d\n", __func__, hartid);
            }
        }
            return;
        default:
            break;
        }
    }
    qemu_log_mask(LOG_GUEST_ERROR, "%s: write: addr=0x%x val=0x%016" PRIx64 "\n",
                  __func__, (int)addr, val64);
}

static const MemoryRegionOps d_reset_ops = {
    .read = d_reset_read,
    .write = d_reset_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4
    }
};

static void d_reset_init(Object *obj)
{
    SiFiveDResetState *s = SIFIVE_D_TEST(obj);

    memory_region_init_io(&s->mmio, obj, &d_reset_ops, s,
                          TYPE_SIFIVE_D_RESET, 0x1000);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mmio);
}

static const TypeInfo d_reset_info = {
    .name          = TYPE_SIFIVE_D_RESET,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(SiFiveDResetState),
    .instance_init = d_reset_init,
};

static void d_reset_register_types(void)
{
    type_register_static(&d_reset_info);
}

type_init(d_reset_register_types)


/*
 * Create Test device.
 */
DeviceState *sifive_d_reset_create(hwaddr addr)
{
    DeviceState *dev = qdev_new(TYPE_SIFIVE_D_RESET);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, addr);
    return dev;
}
