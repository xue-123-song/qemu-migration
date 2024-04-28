/*
 * QEMU RISC-V CPU
 *
 * Copyright (c) 2016-2017 Sagar Karandikar, sagark@eecs.berkeley.edu
 * Copyright (c) 2017-2018 SiFive, Inc.
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
#include "qemu/qemu-print.h"
#include "qemu/ctype.h"
#include "qemu/log.h"
#include "cpu.h"
#include "internals.h"
#include "exec/exec-all.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"
#include "fpu/softfloat-helpers.h"

/* RISC-V CPU definitions */

static const char riscv_exts[26] = "IEMAFDQCLBJTPVNSUHKORWXYZG";

const char * const riscv_int_regnames[] = {
  "x0/zero", "x1/ra",  "x2/sp",  "x3/gp",  "x4/tp",  "x5/t0",   "x6/t1",
  "x7/t2",   "x8/s0",  "x9/s1",  "x10/a0", "x11/a1", "x12/a2",  "x13/a3",
  "x14/a4",  "x15/a5", "x16/a6", "x17/a7", "x18/s2", "x19/s3",  "x20/s4",
  "x21/s5",  "x22/s6", "x23/s7", "x24/s8", "x25/s9", "x26/s10", "x27/s11",
  "x28/t3",  "x29/t4", "x30/t5", "x31/t6"
};

const char * const riscv_fpr_regnames[] = {
  "f0/ft0",   "f1/ft1",  "f2/ft2",   "f3/ft3",   "f4/ft4",  "f5/ft5",
  "f6/ft6",   "f7/ft7",  "f8/fs0",   "f9/fs1",   "f10/fa0", "f11/fa1",
  "f12/fa2",  "f13/fa3", "f14/fa4",  "f15/fa5",  "f16/fa6", "f17/fa7",
  "f18/fs2",  "f19/fs3", "f20/fs4",  "f21/fs5",  "f22/fs6", "f23/fs7",
  "f24/fs8",  "f25/fs9", "f26/fs10", "f27/fs11", "f28/ft8", "f29/ft9",
  "f30/ft10", "f31/ft11"
};

static const char * const riscv_excp_names[] = {
    "misaligned_fetch",
    "fault_fetch",
    "illegal_instruction",
    "breakpoint",
    "misaligned_load",
    "fault_load",
    "misaligned_store",
    "fault_store",
    "user_ecall",
    "supervisor_ecall",
    "hypervisor_ecall",
    "machine_ecall",
    "exec_page_fault",
    "load_page_fault",
    "reserved",
    "store_page_fault",
    "reserved",
    "reserved",
    "reserved",
    "reserved",
    "guest_exec_page_fault",
    "guest_load_page_fault",
    "reserved",
    "guest_store_page_fault",
};

static const char * const riscv_intr_names[] = {
    "u_software",
    "s_software",
    "vs_software",
    "m_software",
    "u_timer",
    "s_timer",
    "vs_timer",
    "m_timer",
    "u_external",
    "s_external",
    "vs_external",
    "m_external",
    "reserved",
    "reserved",
    "reserved",
    "reserved"
};

const char *riscv_cpu_get_trap_name(target_ulong cause, bool async)
{
    if (async) {
        return (cause < ARRAY_SIZE(riscv_intr_names)) ?
               riscv_intr_names[cause] : "(unknown)";
    } else {
        return (cause < ARRAY_SIZE(riscv_excp_names)) ?
               riscv_excp_names[cause] : "(unknown)";
    }
}

bool riscv_cpu_is_32bit(CPURISCVState *env)
{
    if (env->misa & RV64) {
        return false;
    }

    return true;
}

static void set_misa(CPURISCVState *env, target_ulong misa)
{
    env->misa_mask = env->misa = misa;
}

static void set_priv_version(CPURISCVState *env, int priv_ver)
{
    env->priv_ver = priv_ver;
}

static void set_bext_version(CPURISCVState *env, int bext_ver)
{
    env->bext_ver = bext_ver;
}

static void set_vext_version(CPURISCVState *env, int vext_ver)
{
    env->vext_ver = vext_ver;
}

static void set_feature(CPURISCVState *env, int feature)
{
    env->features |= (1ULL << feature);
}

static void set_resetvec(CPURISCVState *env, target_ulong resetvec)
{
#ifndef CONFIG_USER_ONLY
    env->resetvec = resetvec;
#endif
}

static void riscv_any_cpu_init(Object *obj)
{
    CPURISCVState *env = &RISCV_CPU(obj)->env;
#if defined(TARGET_RISCV32)
    set_misa(env, RV32 | RVI | RVM | RVA | RVF | RVD | RVC | RVU);
#elif defined(TARGET_RISCV64)
    set_misa(env, RV64 | RVI | RVM | RVA | RVF | RVD | RVC | RVU);
#endif
    set_priv_version(env, PRIV_VERSION_1_11_0);
}

#if defined(TARGET_RISCV64)
static void rv64_base_cpu_init(Object *obj)
{
    CPURISCVState *env = &RISCV_CPU(obj)->env;
    /* We set this in the realise function */
    set_misa(env, RV64);
}

static void rv64_sifive_u_cpu_init(Object *obj)
{
    CPURISCVState *env = &RISCV_CPU(obj)->env;
    set_misa(env, RV64 | RVI | RVM | RVA | RVF | RVD | RVC | RVS | RVU);
    set_priv_version(env, PRIV_VERSION_1_10_0);
}

static void rv64_sifive_e_cpu_init(Object *obj)
{
    CPURISCVState *env = &RISCV_CPU(obj)->env;
    set_misa(env, RV64 | RVI | RVM | RVA | RVC | RVU);
    set_priv_version(env, PRIV_VERSION_1_10_0);
    qdev_prop_set_bit(DEVICE(obj), "mmu", false);
}
#else
static void rv32_base_cpu_init(Object *obj)
{
    CPURISCVState *env = &RISCV_CPU(obj)->env;
    /* We set this in the realise function */
    set_misa(env, RV32);
}

static void rv32_sifive_u_cpu_init(Object *obj)
{
    CPURISCVState *env = &RISCV_CPU(obj)->env;
    set_misa(env, RV32 | RVI | RVM | RVA | RVF | RVD | RVC | RVS | RVU);
    set_priv_version(env, PRIV_VERSION_1_10_0);
}

static void rv32_sifive_e_cpu_init(Object *obj)
{
    CPURISCVState *env = &RISCV_CPU(obj)->env;
    set_misa(env, RV32 | RVI | RVM | RVA | RVC | RVU);
    set_priv_version(env, PRIV_VERSION_1_10_0);
    qdev_prop_set_bit(DEVICE(obj), "mmu", false);
}

static void rv32_ibex_cpu_init(Object *obj)
{
    CPURISCVState *env = &RISCV_CPU(obj)->env;
    set_misa(env, RV32 | RVI | RVM | RVC | RVU);
    set_priv_version(env, PRIV_VERSION_1_10_0);
    qdev_prop_set_bit(DEVICE(obj), "mmu", false);
    qdev_prop_set_bit(DEVICE(obj), "x-epmp", true);
}

static void rv32_imafcu_nommu_cpu_init(Object *obj)
{
    CPURISCVState *env = &RISCV_CPU(obj)->env;
    set_misa(env, RV32 | RVI | RVM | RVA | RVF | RVC | RVU);
    set_priv_version(env, PRIV_VERSION_1_10_0);
    set_resetvec(env, DEFAULT_RSTVEC);
    qdev_prop_set_bit(DEVICE(obj), "mmu", false);
}
#endif

static ObjectClass *riscv_cpu_class_by_name(const char *cpu_model)
{
    ObjectClass *oc;
    char *typename;
    char **cpuname;

    cpuname = g_strsplit(cpu_model, ",", 1);
    typename = g_strdup_printf(RISCV_CPU_TYPE_NAME("%s"), cpuname[0]);
    oc = object_class_by_name(typename);
    g_strfreev(cpuname);
    g_free(typename);
    if (!oc || !object_class_dynamic_cast(oc, TYPE_RISCV_CPU) ||
        object_class_is_abstract(oc)) {
        return NULL;
    }
    return oc;
}

#define DUMP_CPU_MIGRATE 

static void riscv_cpu_dump_state(CPUState *cs, FILE *f, int flags)
{
    RISCVCPU *cpu = RISCV_CPU(cs);
    CPURISCVState *env = &cpu->env;
    int i;

#if !defined(CONFIG_USER_ONLY)
    if (riscv_has_ext(env, RVH)) {
        qemu_fprintf(f, " %s %d\n", "V      =  ", riscv_cpu_virt_enabled(env));
    }
#endif

    qemu_fprintf(f, " %s " TARGET_FMT_lx "\n", "pc      ", env->pc);

#ifndef CONFIG_USER_ONLY
    qemu_fprintf(f, " %s " TARGET_FMT_lx "\n", "mhartid ", env->mhartid);
    qemu_fprintf(f, " %s " TARGET_FMT_lx "\n", "mstatus ", (target_ulong)env->mstatus);
    qemu_fprintf(f, " %s " TARGET_FMT_lx "\n", "mip     ", env->mip);
    qemu_fprintf(f, " %s " TARGET_FMT_lx "\n", "mie     ", env->mie);
    qemu_fprintf(f, " %s " TARGET_FMT_lx "\n", "mideleg ", env->mideleg);
    qemu_fprintf(f, " %s " TARGET_FMT_lx "\n", "medeleg ", env->medeleg);
    qemu_fprintf(f, " %s " TARGET_FMT_lx "\n", "mtvec   ", env->mtvec);
    qemu_fprintf(f, " %s " TARGET_FMT_lx "\n", "stvec   ", env->stvec);
    qemu_fprintf(f, " %s " TARGET_FMT_lx "\n", "mepc    ", env->mepc);
    qemu_fprintf(f, " %s " TARGET_FMT_lx "\n", "sepc    ", env->sepc);
    qemu_fprintf(f, " %s " TARGET_FMT_lx "\n", "mcause  ", env->mcause);
    qemu_fprintf(f, " %s " TARGET_FMT_lx "\n", "scause  ", env->scause);
    qemu_fprintf(f, " %s " TARGET_FMT_lx "\n", "mtval   ", env->mtval);
    qemu_fprintf(f, " %s " TARGET_FMT_lx "\n", "stval   ", env->stval);
    qemu_fprintf(f, " %s " TARGET_FMT_lx "\n", "mscratch", env->mscratch);
    qemu_fprintf(f, " %s " TARGET_FMT_lx "\n", "sscratch", env->sscratch);
    qemu_fprintf(f, " %s " TARGET_FMT_lx "\n", "satp    ", env->satp);

    if (riscv_cpu_is_32bit(env)) {
        qemu_fprintf(f, " %s " TARGET_FMT_lx "\n", "mstatush ",
                     (target_ulong)(env->mstatus >> 32));
    }

    if (riscv_has_ext(env, RVH)) {
        qemu_fprintf(f, " %s " TARGET_FMT_lx "\n", "hstatus ", env->hstatus);
        qemu_fprintf(f, " %s " TARGET_FMT_lx "\n", "vsstatus ",
                     (target_ulong)env->vsstatus);
        qemu_fprintf(f, " %s " TARGET_FMT_lx "\n", "htval ", env->htval);
        qemu_fprintf(f, " %s " TARGET_FMT_lx "\n", "vscause ", env->vscause);
        qemu_fprintf(f, " %s " TARGET_FMT_lx "\n", "mtval2 ", env->mtval2);
        qemu_fprintf(f, " %s " TARGET_FMT_lx "\n", "hideleg ", env->hideleg);
        qemu_fprintf(f, " %s " TARGET_FMT_lx "\n", "hedeleg ", env->hedeleg);
        qemu_fprintf(f, " %s " TARGET_FMT_lx "\n", "vstvec  ", env->vstvec);
        qemu_fprintf(f, " %s " TARGET_FMT_lx "\n", "vsepc   ", env->vsepc);
    }
#endif

    for (i = 0; i < 32; i++) {
        qemu_fprintf(f, " %s " TARGET_FMT_lx,
                     riscv_int_regnames[i], env->gpr[i]);
        if ((i & 3) == 3) {
            qemu_fprintf(f, "\n");
        }
    }

    if (flags & CPU_DUMP_FPU) {
        for (i = 0; i < 32; i++) {
            qemu_fprintf(f, " %s %016" PRIx64,
                         riscv_fpr_regnames[i], env->fpr[i]);
            if ((i & 3) == 3) {
                qemu_fprintf(f, "\n");
            }
        }
    }

    if (cpu->cfg.pmp) {
        for(i =0; i< MAX_RISCV_PMPS; ++i){
            qemu_fprintf(f, "%s_%d " TARGET_FMT_lx "\n", "pmpaddr", i, pmpaddr_csr_read(env, i));
        }
        for(i =0; i< MAX_RISCV_PMPS/4; ++i){
            qemu_fprintf(f, "%s_%d " TARGET_FMT_lx "\n", "pmpcfg", i, pmpcfg_csr_read(env, i));
        }
        qemu_fprintf(f, "%s %d\n", "pmprules", env->pmp_state.num_rules);
    }

#ifdef DUMP_CPU_MIGRATE
    FILE *fp;
    fp = fopen("/tmp/qemu-cpu.txt", "w");

    fprintf(fp, " %s " TARGET_FMT_lx "\n", "pc      ", env->pc);
    fprintf(fp, " %s " TARGET_FMT_lx "\n", "mhartid ", env->mhartid);
    fprintf(fp, " %s " TARGET_FMT_lx "\n", "mstatus ", (target_ulong)env->mstatus);
    fprintf(fp, " %s " TARGET_FMT_lx "\n", "mip     ", env->mip);
    fprintf(fp, " %s " TARGET_FMT_lx "\n", "mie     ", env->mie);
    fprintf(fp, " %s " TARGET_FMT_lx "\n", "mideleg ", env->mideleg);
    fprintf(fp, " %s " TARGET_FMT_lx "\n", "medeleg ", env->medeleg);
    fprintf(fp, " %s " TARGET_FMT_lx "\n", "mtvec   ", env->mtvec);
    fprintf(fp, " %s " TARGET_FMT_lx "\n", "stvec   ", env->stvec);
    fprintf(fp, " %s " TARGET_FMT_lx "\n", "mepc    ", env->mepc);
    fprintf(fp, " %s " TARGET_FMT_lx "\n", "sepc    ", env->sepc);
    fprintf(fp, " %s " TARGET_FMT_lx "\n", "mcause  ", env->mcause);
    fprintf(fp, " %s " TARGET_FMT_lx "\n", "scause  ", env->scause);
    fprintf(fp, " %s " TARGET_FMT_lx "\n", "mtval   ", env->mtval);
    fprintf(fp, " %s " TARGET_FMT_lx "\n", "stval   ", env->stval);
    fprintf(fp, " %s " TARGET_FMT_lx "\n", "mscratch", env->mscratch);
    fprintf(fp, " %s " TARGET_FMT_lx "\n", "sscratch", env->sscratch);
    fprintf(fp, " %s " TARGET_FMT_lx "\n", "satp    ", env->satp);
    fprintf(fp, " %s " TARGET_FMT_lx "\n", "load_res", env->load_res);
    fprintf(fp, " %s " TARGET_FMT_lx "\n", "load_val", env->load_val);
    fprintf(fp, " %s " TARGET_FMT_lx "\n", "frm     ", env->frm);
    fprintf(fp, " %s " TARGET_FMT_lx "\n", "badaddr ", env->badaddr);
    fprintf(fp, " %s " TARGET_FMT_lx "\n", "guest_phys_fault_addr", env->guest_phys_fault_addr);
    fprintf(fp, " %s " TARGET_FMT_lx "\n", "priv_ver  ", env->priv_ver);
    fprintf(fp, " %s " TARGET_FMT_lx "\n", "vext_ver  ", env->vext_ver);
    fprintf(fp, " %s " TARGET_FMT_lx "\n", "misa      ", env->misa);
    fprintf(fp, " %s " TARGET_FMT_lx "\n", "misa_mask ", env->misa_mask);
    fprintf(fp, " %s " "%x" "\n", "features  ", env->features);
    fprintf(fp, " %s " TARGET_FMT_lx "\n", "priv      ", env->priv);
    fprintf(fp, " %s " TARGET_FMT_lx "\n", "virt      ", env->virt);
    fprintf(fp, " %s " TARGET_FMT_lx "\n", "resetvec  ", env->resetvec);
    fprintf(fp, " %s " TARGET_FMT_lx "\n", "scounteren   ", env->scounteren);
    fprintf(fp, " %s " TARGET_FMT_lx "\n", "mcounteren  ", env->mcounteren);
    fprintf(fp, " %s " TARGET_FMT_lx "\n", "mfromhost ", env->mfromhost);
    fprintf(fp, " %s " TARGET_FMT_lx "\n", "mtohost   ", env->mtohost);
    fprintf(fp, " %s " TARGET_FMT_lx "\n", "timecmp   ", env->timecmp);

    if (riscv_cpu_is_32bit(env)) {
        fprintf(fp, " %s " TARGET_FMT_lx "\n", "mstatush ",
                     (target_ulong)(env->mstatus >> 32));
    }

    for (i = 0; i < 32; i++) {
        fprintf(fp, " %s " TARGET_FMT_lx "\n", riscv_int_regnames[i], env->gpr[i]);
    }
    
    for (i = 0; i < 32; i++) {
        fprintf(fp, " %s %016" PRIx64 "\n", riscv_fpr_regnames[i], env->fpr[i]);
    }

    if (cpu->cfg.pmp) {
        for(i = 0; i < MAX_RISCV_PMPS; ++i){
            fprintf(fp, "%s_%d " TARGET_FMT_lx "\n", "pmpaddr", i, pmpaddr_csr_read(env, i));
        }
        for(i = 0; i < MAX_RISCV_PMPS/4; ++i){
            fprintf(fp, "%s_%d " TARGET_FMT_lx "\n", "pmpcfg", i, pmpcfg_csr_read(env, i));
        }
    }

    fclose(fp);
#endif
}

#define FILE_LINE_MAX 100

static void riscv_cpu_load_state(CPUState *cs, const char *filename)
{
    RISCVCPU *cpu = RISCV_CPU(cs);
    CPURISCVState *env = &cpu->env;
    bool flag;
    char str_line[FILE_LINE_MAX], reg[FILE_LINE_MAX];
    FILE *fp, *out;

//     RISCVCPUClass *mcc = RISCV_CPU_GET_CLASS(cpu);
//     mcc->parent_reset(dev);
// #ifndef CONFIG_USER_ONLY
//     env->priv = PRV_M;
//     env->mstatus &= ~(MSTATUS_MIE | MSTATUS_MPRV);
//     env->mcause = 0;
//     env->pc = env->resetvec;
//     env->two_stage_lookup = false;
//     env->satp = 0;
//     env->scause = 0;
//     env->sepc = 0;
//     env->stvec = 0;
//     env->mcause = 0;
//     env->mepc = 0;
//     env->mtvec = 0;
// #endif
//     memset(&env->pmp_state, 0, sizeof(env->pmp_state));
//     cs->exception_index = RISCV_EXCP_NONE;
//     env->load_res = -1;
//     set_default_nan_mode(1, &env->fp_status);

    fp = fopen(filename, "r");
    out = fopen("/home/xst/Desktop/out-test.txt", "w");

    flag = fgets(str_line, FILE_LINE_MAX, fp);
    if (sscanf(str_line, "pc %lx", &env->pc) == 1) {
        fprintf(out, " %s %lx\n", str_line, env->pc);  
    } else {
        error_printf("Error: failed to read pc\n");
        goto end;
    }

    flag = fgets(str_line, FILE_LINE_MAX, fp);
    if (sscanf(str_line, "mhartid %lx", &env->mhartid) == 1) {
        fprintf(out, " %s %lx\n", str_line, env->mhartid);  
    } else {
        error_printf("Error: failed to read mhartid\n");
        goto end;
    }

    flag = fgets(str_line, FILE_LINE_MAX, fp);
    if (sscanf(str_line, "mstatus %lx", &env->mstatus) == 1) {
        fprintf(out, " %s %lx\n", str_line, env->mstatus);  
    } else {
        error_printf("Error: failed to read mstatus\n");
        goto end;
    }

    flag = fgets(str_line, FILE_LINE_MAX, fp);
    if (sscanf(str_line, "mip %lx", &env->mip) == 1) {
        fprintf(out, " %s %lx\n", str_line, env->mip);  
    } else {
        error_printf("Error: failed to read mip\n");
        goto end;
    }

    flag = fgets(str_line, FILE_LINE_MAX, fp);
    if (sscanf(str_line, "mie %lx", &env->mie) == 1) {
        fprintf(out, " %s %lx\n", str_line, env->mie);  
    } else {
        error_printf("Error: failed to read mie\n");
        goto end;
    }

    flag = fgets(str_line, FILE_LINE_MAX, fp);
    if (sscanf(str_line, "mideleg %lx", &env->mideleg) == 1) {
        fprintf(out, " %s %lx\n", str_line, env->mideleg);  
    } else {
        error_printf("Error: failed to read mideleg\n");
        goto end;
    }

    flag = fgets(str_line, FILE_LINE_MAX, fp);
    if (sscanf(str_line, "medeleg %lx", &env->medeleg) == 1) {
        fprintf(out, " %s %lx\n", str_line, env->medeleg);  
    } else {
        error_printf("Error: failed to read medeleg\n");
        goto end;
    }

    flag = fgets(str_line, FILE_LINE_MAX, fp);
    if (sscanf(str_line, "mtvec %lx", &env->mtvec) == 1) {
        fprintf(out, " %s %lx\n", str_line, env->mtvec);  
    } else {
        error_printf("Error: failed to read mtvec\n");
        goto end;
    }

    flag = fgets(str_line, FILE_LINE_MAX, fp);
    if (sscanf(str_line, "stvec %lx", &env->stvec) == 1) {
        fprintf(out, " %s %lx\n", str_line, env->stvec);  
    } else {
        error_printf("Error: failed to read stvec\n");
        goto end;
    }

    flag = fgets(str_line, FILE_LINE_MAX, fp);
    if (sscanf(str_line, "mepc %lx", &env->mepc) == 1) {
        fprintf(out, " %s %lx\n", str_line, env->mepc);  
    } else {
        error_printf("Error: failed to read mepc\n");
        goto end;
    }

    flag = fgets(str_line, FILE_LINE_MAX, fp);
    if (sscanf(str_line, "sepc %lx", &env->sepc) == 1) {
        fprintf(out, " %s %lx\n", str_line, env->sepc);  
    } else {
        error_printf("Error: failed to read sepc\n");
        goto end;
    }

    flag = fgets(str_line, FILE_LINE_MAX, fp);
    if (sscanf(str_line, "mcause %lx", &env->mcause) == 1) {
        fprintf(out, " %s %lx\n", str_line, env->mcause);  
    } else {
        error_printf("Error: failed to read mcause\n");
        goto end;
    }

    flag = fgets(str_line, FILE_LINE_MAX, fp);
    if (sscanf(str_line, "scause %lx", &env->scause) == 1) {
        fprintf(out, " %s %lx\n", str_line, env->scause);  
    } else {
        error_printf("Error: failed to read scause\n");
        goto end;
    }

    flag = fgets(str_line, FILE_LINE_MAX, fp);
    if (sscanf(str_line, "mtval %lx", &env->mtval) == 1) {
        fprintf(out, " %s %lx\n", str_line, env->mtval);  
    } else {
        error_printf("Error: failed to read mtval\n");
        goto end;
    }

    flag = fgets(str_line, FILE_LINE_MAX, fp);
    if (sscanf(str_line, "stval %lx", &env->stval) == 1) {
        fprintf(out, " %s %lx\n", str_line, env->stval);  
    } else {
        error_printf("Error: failed to read stval\n");
        goto end;
    }

    flag = fgets(str_line, FILE_LINE_MAX, fp);
    if (sscanf(str_line, "mscratch %lx", &env->mscratch) == 1) {
        fprintf(out, " %s %lx\n", str_line, env->mscratch);  
    } else {
        error_printf("Error: failed to read mscratch\n");
        goto end;
    }

    flag = fgets(str_line, FILE_LINE_MAX, fp);
    if (sscanf(str_line, "sscratch %lx", &env->sscratch) == 1) {
        fprintf(out, " %s %lx\n", str_line, env->sscratch);  
    } else {
        error_printf("Error: failed to read sscratch\n");
        goto end;
    }
    
    flag = fgets(str_line, FILE_LINE_MAX, fp);
    if (sscanf(str_line, "satp %lx", &env->satp) == 1) {
        fprintf(out, " %s %lx\n", str_line, env->satp);  
    } else {
        error_printf("Error: failed to read satp\n");
        goto end;
    }

    flag = fgets(str_line, FILE_LINE_MAX, fp);
    if (sscanf(str_line, "load_res %lx", &env->load_res) == 1) {
        fprintf(out, " %s %lx\n", str_line, env->load_res);  
    } else {
        error_printf("Error: failed to read load_res\n");
        goto end;
    }

    flag = fgets(str_line, FILE_LINE_MAX, fp);
    if (sscanf(str_line, "load_val %lx", &env->load_val) == 1) {
        fprintf(out, " %s %lx\n", str_line, env->load_val);  
    } else {
        error_printf("Error: failed to read load_val\n");
        goto end;
    }

    flag = fgets(str_line, FILE_LINE_MAX, fp);
    if (sscanf(str_line, "frm %lx", &env->frm) == 1) {
        fprintf(out, " %s %lx\n", str_line, env->frm);  
    } else {
        error_printf("Error: failed to read frm\n");
        goto end;
    }

    flag = fgets(str_line, FILE_LINE_MAX, fp);
    if (sscanf(str_line, "badaddr %lx", &env->badaddr) == 1) {
        fprintf(out, " %s %lx\n", str_line, env->badaddr);  
    } else {
        error_printf("Error: failed to read badaddr\n");
        goto end;
    }

    flag = fgets(str_line, FILE_LINE_MAX, fp);
    if (sscanf(str_line, "guest_phys_fault_addr %lx", &env->guest_phys_fault_addr) == 1) {
        fprintf(out, " %s %lx\n", str_line, env->guest_phys_fault_addr);  
    } else {
        error_printf("Error: failed to read guest_phys_fault_addr\n");
        goto end;
    }

    flag = fgets(str_line, FILE_LINE_MAX, fp);
    if (sscanf(str_line, "priv_ver %lx", &env->priv_ver) == 1) {
        fprintf(out, " %s %lx\n", str_line, env->priv_ver);  
    } else {
        error_printf("Error: failed to read priv_ver\n");
        goto end;
    }

    flag = fgets(str_line, FILE_LINE_MAX, fp);
    if (sscanf(str_line, "vext_ver %lx", &env->vext_ver) == 1) {
        fprintf(out, " %s %lx\n", str_line, env->vext_ver);  
    } else {
        error_printf("Error: failed to read vext_ver\n");
        goto end;
    }

    flag = fgets(str_line, FILE_LINE_MAX, fp);
    if (sscanf(str_line, "misa %lx", &env->misa) == 1) {
        fprintf(out, " %s %lx\n", str_line, env->misa);  
    } else {
        error_printf("Error: failed to read misa\n");
        goto end;
    }

    flag = fgets(str_line, FILE_LINE_MAX, fp);
    if (sscanf(str_line, "misa_mask %lx", &env->misa_mask) == 1) {
        fprintf(out, " %s %lx\n", str_line, env->misa_mask);  
    } else {
        error_printf("Error: failed to read misa_mask\n");
        goto end;
    }

    flag = fgets(str_line, FILE_LINE_MAX, fp);
    if (sscanf(str_line, "features %x", &env->features) == 1) {
        fprintf(out, " %s %x\n", str_line, env->features);  
    } else {
        error_printf("Error: failed to read features\n");
        goto end;
    }

    flag = fgets(str_line, FILE_LINE_MAX, fp);
    if (sscanf(str_line, "priv %lx", &env->priv) == 1) {
        fprintf(out, " %s %lx\n", str_line, env->priv);  
    } else {
        error_printf("Error: failed to read priv\n");
        goto end;
    }

    flag = fgets(str_line, FILE_LINE_MAX, fp);
    if (sscanf(str_line, "virt %lx", &env->virt) == 1) {
        fprintf(out, " %s %lx\n", str_line, env->virt);  
    } else {
        error_printf("Error: failed to read virt\n");
        goto end;
    }

    flag = fgets(str_line, FILE_LINE_MAX, fp);
    if (sscanf(str_line, "resetvec %lx", &env->resetvec) == 1) {
        fprintf(out, " %s %lx\n", str_line, env->resetvec);  
    } else {
        error_printf("Error: failed to read resetvec\n");
        goto end;
    }

    flag = fgets(str_line, FILE_LINE_MAX, fp);
    if (sscanf(str_line, "scounteren %lx", &env->scounteren) == 1) {
        fprintf(out, " %s %lx\n", str_line, env->scounteren);  
    } else {
        error_printf("Error: failed to read scounteren\n");
        goto end;
    }

    flag = fgets(str_line, FILE_LINE_MAX, fp);
    if (sscanf(str_line, "mcounteren %lx", &env->mcounteren) == 1) {
        fprintf(out, " %s %lx\n", str_line, env->mcounteren);  
    } else {
        error_printf("Error: failed to read mcounteren\n");
        goto end;
    }

    flag = fgets(str_line, FILE_LINE_MAX, fp);
    if (sscanf(str_line, "mfromhost %lx", &env->mfromhost) == 1) {
        fprintf(out, " %s %lx\n", str_line, env->mfromhost);  
    } else {
        error_printf("Error: failed to read mfromhost\n");
        goto end;
    }

    flag = fgets(str_line, FILE_LINE_MAX, fp);
    if (sscanf(str_line, "mtohost %lx", &env->mtohost) == 1) {
        fprintf(out, " %s %lx\n", str_line, env->mtohost);  
    } else {
        error_printf("Error: failed to read mtohost\n");
        goto end;
    }

    flag = fgets(str_line, FILE_LINE_MAX, fp);
    if (sscanf(str_line, "timecmp %lx", &env->timecmp) == 1) {
        fprintf(out, " %s %lx\n", str_line, env->timecmp);  
    } else {
        error_printf("Error: failed to read timecmp\n");
        goto end;
    }

    if (riscv_cpu_is_32bit(env)) {
        flag = fgets(str_line, FILE_LINE_MAX, fp);
        if (sscanf(str_line, "mstatush %lx", &env->mstatus) == 1) {
            fprintf(out, " %s %lx\n", str_line, env->mstatus);  
        } else {
            error_printf("Error: failed to read mstatush\n");
            goto end;
        }
    }

    for (int i = 0; i < 32; i++) {
        flag = fgets(str_line, FILE_LINE_MAX, fp);
        if (sscanf(str_line, "%s %lx", reg, &env->gpr[i]) == 2) {
            fprintf(out, " %s %lx\n", reg, env->gpr[i]);  
        } else {
            error_printf("Error: failed to read gpr[%d]\n", i);
            goto end;
        }
    }

    for (int i = 0; i < 32; i++) {
        flag = fgets(str_line, FILE_LINE_MAX, fp);
        if (sscanf(str_line, "%s %lx", reg, &env->fpr[i]) == 2) {
            fprintf(out, " %s %lx\n", reg, env->fpr[i]);  
        } else {
            error_printf("Error: failed to read fpr[%d]\n", i);
            goto end;
        }
    }

    if (cpu->cfg.pmp) {
        int j;
        target_ulong tmp;
        memset(&env->pmp_state, 0, sizeof(env->pmp_state));

        for(int i = 0; i < MAX_RISCV_PMPS; ++i){
            flag = fgets(str_line, FILE_LINE_MAX, fp);
            if (sscanf(str_line, "pmpaddr_%d %lx", &j, &tmp) == 2) {
                pmpaddr_csr_write(env, i, tmp);
                fprintf(out, "pmpaddr_%d %lx\n", j, pmpaddr_csr_read(env, i));  
            } else {
                error_printf("Error: failed to read pmpaddr[%d]\n", i);
                goto end;
            }
        }

        for(int i = 0; i < MAX_RISCV_PMPS/4; ++i){
            flag = fgets(str_line, FILE_LINE_MAX, fp);
            if (sscanf(str_line, "pmpcfg_%d %lx", &j, &tmp) == 2) {
                pmpcfg_csr_write(env, i, tmp);
                fprintf(out, "pmpcfg_%d %lx\n", j, pmpcfg_csr_read(env, i));  
            } else {
                error_printf("Error: failed to read pmpcfg[%d]\n", i);
                goto end;
            }
        }

        for (int i = 0; i < MAX_RISCV_PMPS; i++) {
            pmp_update_rule_addr(env, i);
        }
        pmp_update_rule_nums(env);
    }


end:
    fclose(fp);
    fclose(out);
}

static void riscv_cpu_set_pc(CPUState *cs, vaddr value)
{
    RISCVCPU *cpu = RISCV_CPU(cs);
    CPURISCVState *env = &cpu->env;
    env->pc = value;
}

static void riscv_cpu_synchronize_from_tb(CPUState *cs,
                                          const TranslationBlock *tb)
{
    RISCVCPU *cpu = RISCV_CPU(cs);
    CPURISCVState *env = &cpu->env;
    env->pc = tb->pc;
}

static bool riscv_cpu_has_work(CPUState *cs)
{
#ifndef CONFIG_USER_ONLY
    RISCVCPU *cpu = RISCV_CPU(cs);
    CPURISCVState *env = &cpu->env;
    /*
     * Definition of the WFI instruction requires it to ignore the privilege
     * mode and delegation registers, but respect individual enables
     */
    return (env->mip & env->mie) != 0;
#else
    return true;
#endif
}

void restore_state_to_opc(CPURISCVState *env, TranslationBlock *tb,
                          target_ulong *data)
{
    env->pc = data[0];
}

static void riscv_cpu_reset(DeviceState *dev)
{
    CPUState *cs = CPU(dev);
    RISCVCPU *cpu = RISCV_CPU(cs);
    RISCVCPUClass *mcc = RISCV_CPU_GET_CLASS(cpu);
    CPURISCVState *env = &cpu->env;
    mcc->parent_reset(dev);
#ifndef CONFIG_USER_ONLY
    env->priv = PRV_M;
    env->mstatus &= ~(MSTATUS_MIE | MSTATUS_MPRV);
    env->mcause = 0;
    env->pc = env->resetvec;
    env->two_stage_lookup = false;
    env->satp = 0;
    env->scause = 0;
    env->sepc = 0;
    env->stvec = 0;
    env->mcause = 0;
    env->mepc = 0;
    env->mtvec = 0;
#endif
    memset(&env->pmp_state, 0, sizeof(env->pmp_state));
    cs->exception_index = RISCV_EXCP_NONE;
    env->load_res = -1;
    set_default_nan_mode(1, &env->fp_status);
}

static void riscv_cpu_disas_set_info(CPUState *s, disassemble_info *info)
{
    RISCVCPU *cpu = RISCV_CPU(s);
    if (riscv_cpu_is_32bit(&cpu->env)) {
        info->print_insn = print_insn_riscv32;
    } else {
        info->print_insn = print_insn_riscv64;
    }
}

static void riscv_cpu_realize(DeviceState *dev, Error **errp)
{
    CPUState *cs = CPU(dev);
    RISCVCPU *cpu = RISCV_CPU(dev);
    CPURISCVState *env = &cpu->env;
    RISCVCPUClass *mcc = RISCV_CPU_GET_CLASS(dev);
    int priv_version = PRIV_VERSION_1_11_0;
    int bext_version = BEXT_VERSION_0_93_0;
    int vext_version = VEXT_VERSION_0_07_1;
    target_ulong target_misa = env->misa;
    Error *local_err = NULL;

    cpu_exec_realizefn(cs, &local_err);
    if (local_err != NULL) {
        error_propagate(errp, local_err);
        return;
    }

    if (cpu->cfg.priv_spec) {
        if (!g_strcmp0(cpu->cfg.priv_spec, "v1.11.0")) {
            priv_version = PRIV_VERSION_1_11_0;
        } else if (!g_strcmp0(cpu->cfg.priv_spec, "v1.10.0")) {
            priv_version = PRIV_VERSION_1_10_0;
        } else {
            error_setg(errp,
                       "Unsupported privilege spec version '%s'",
                       cpu->cfg.priv_spec);
            return;
        }
    }

    set_priv_version(env, priv_version);
    set_bext_version(env, bext_version);
    set_vext_version(env, vext_version);

    if (cpu->cfg.mmu) {
        set_feature(env, RISCV_FEATURE_MMU);
    }

    if (cpu->cfg.pmp) {
        set_feature(env, RISCV_FEATURE_PMP);

        /*
         * Enhanced PMP should only be available
         * on harts with PMP support
         */
        if (cpu->cfg.epmp) {
            set_feature(env, RISCV_FEATURE_EPMP);
        }
    }

    set_resetvec(env, cpu->cfg.resetvec);

    /* If only XLEN is set for misa, then set misa from properties */
    if (env->misa == RV32 || env->misa == RV64) {
        /* Do some ISA extension error checking */
        if (cpu->cfg.ext_i && cpu->cfg.ext_e) {
            error_setg(errp,
                       "I and E extensions are incompatible");
                       return;
       }

        if (!cpu->cfg.ext_i && !cpu->cfg.ext_e) {
            error_setg(errp,
                       "Either I or E extension must be set");
                       return;
       }

       if (cpu->cfg.ext_g && !(cpu->cfg.ext_i & cpu->cfg.ext_m &
                               cpu->cfg.ext_a & cpu->cfg.ext_f &
                               cpu->cfg.ext_d)) {
            warn_report("Setting G will also set IMAFD");
            cpu->cfg.ext_i = true;
            cpu->cfg.ext_m = true;
            cpu->cfg.ext_a = true;
            cpu->cfg.ext_f = true;
            cpu->cfg.ext_d = true;
        }

        /* Set the ISA extensions, checks should have happened above */
        if (cpu->cfg.ext_i) {
            target_misa |= RVI;
        }
        if (cpu->cfg.ext_e) {
            target_misa |= RVE;
        }
        if (cpu->cfg.ext_m) {
            target_misa |= RVM;
        }
        if (cpu->cfg.ext_a) {
            target_misa |= RVA;
        }
        if (cpu->cfg.ext_f) {
            target_misa |= RVF;
        }
        if (cpu->cfg.ext_d) {
            target_misa |= RVD;
        }
        if (cpu->cfg.ext_c) {
            target_misa |= RVC;
        }
        if (cpu->cfg.ext_s) {
            target_misa |= RVS;
        }
        if (cpu->cfg.ext_u) {
            target_misa |= RVU;
        }
        if (cpu->cfg.ext_h) {
            target_misa |= RVH;
        }
        if (cpu->cfg.ext_b) {
            target_misa |= RVB;

            if (cpu->cfg.bext_spec) {
                if (!g_strcmp0(cpu->cfg.bext_spec, "v0.93")) {
                    bext_version = BEXT_VERSION_0_93_0;
                } else {
                    error_setg(errp,
                           "Unsupported bitmanip spec version '%s'",
                           cpu->cfg.bext_spec);
                    return;
                }
            } else {
                qemu_log("bitmanip version is not specified, "
                         "use the default value v0.93\n");
            }
            set_bext_version(env, bext_version);
        }
        if (cpu->cfg.ext_v) {
            target_misa |= RVV;
            if (!is_power_of_2(cpu->cfg.vlen)) {
                error_setg(errp,
                        "Vector extension VLEN must be power of 2");
                return;
            }
            if (cpu->cfg.vlen > RV_VLEN_MAX || cpu->cfg.vlen < 128) {
                error_setg(errp,
                        "Vector extension implementation only supports VLEN "
                        "in the range [128, %d]", RV_VLEN_MAX);
                return;
            }
            if (!is_power_of_2(cpu->cfg.elen)) {
                error_setg(errp,
                        "Vector extension ELEN must be power of 2");
                return;
            }
            if (cpu->cfg.elen > 64 || cpu->cfg.vlen < 8) {
                error_setg(errp,
                        "Vector extension implementation only supports ELEN "
                        "in the range [8, 64]");
                return;
            }
            if (cpu->cfg.vext_spec) {
                if (!g_strcmp0(cpu->cfg.vext_spec, "v0.7.1")) {
                    vext_version = VEXT_VERSION_0_07_1;
                } else {
                    error_setg(errp,
                           "Unsupported vector spec version '%s'",
                           cpu->cfg.vext_spec);
                    return;
                }
            } else {
                qemu_log("vector version is not specified, "
                        "use the default value v0.7.1\n");
            }
            set_vext_version(env, vext_version);
        }

        set_misa(env, target_misa);
    }

    riscv_cpu_register_gdb_regs_for_features(cs);

    qemu_init_vcpu(cs);
    cpu_reset(cs);

    mcc->parent_realize(dev, errp);
}

static void riscv_cpu_init(Object *obj)
{
    RISCVCPU *cpu = RISCV_CPU(obj);

    cpu_set_cpustate_pointers(cpu);
}

static Property riscv_cpu_properties[] = {
    DEFINE_PROP_BOOL("i", RISCVCPU, cfg.ext_i, true),
    DEFINE_PROP_BOOL("e", RISCVCPU, cfg.ext_e, false),
    DEFINE_PROP_BOOL("g", RISCVCPU, cfg.ext_g, true),
    DEFINE_PROP_BOOL("m", RISCVCPU, cfg.ext_m, true),
    DEFINE_PROP_BOOL("a", RISCVCPU, cfg.ext_a, true),
    DEFINE_PROP_BOOL("f", RISCVCPU, cfg.ext_f, true),
    DEFINE_PROP_BOOL("d", RISCVCPU, cfg.ext_d, true),
    DEFINE_PROP_BOOL("c", RISCVCPU, cfg.ext_c, true),
    DEFINE_PROP_BOOL("s", RISCVCPU, cfg.ext_s, true),
    DEFINE_PROP_BOOL("u", RISCVCPU, cfg.ext_u, true),
    /* This is experimental so mark with 'x-' */
    DEFINE_PROP_BOOL("x-b", RISCVCPU, cfg.ext_b, false),
    DEFINE_PROP_BOOL("x-h", RISCVCPU, cfg.ext_h, false),
    DEFINE_PROP_BOOL("x-v", RISCVCPU, cfg.ext_v, false),
    DEFINE_PROP_BOOL("Counters", RISCVCPU, cfg.ext_counters, true),
    DEFINE_PROP_BOOL("Zifencei", RISCVCPU, cfg.ext_ifencei, true),
    DEFINE_PROP_BOOL("Zicsr", RISCVCPU, cfg.ext_icsr, true),
    DEFINE_PROP_STRING("priv_spec", RISCVCPU, cfg.priv_spec),
    DEFINE_PROP_STRING("bext_spec", RISCVCPU, cfg.bext_spec),
    DEFINE_PROP_STRING("vext_spec", RISCVCPU, cfg.vext_spec),
    DEFINE_PROP_UINT16("vlen", RISCVCPU, cfg.vlen, 128),
    DEFINE_PROP_UINT16("elen", RISCVCPU, cfg.elen, 64),
    DEFINE_PROP_BOOL("mmu", RISCVCPU, cfg.mmu, true),
    DEFINE_PROP_BOOL("pmp", RISCVCPU, cfg.pmp, true),
    DEFINE_PROP_BOOL("x-epmp", RISCVCPU, cfg.epmp, false),

    DEFINE_PROP_UINT64("resetvec", RISCVCPU, cfg.resetvec, DEFAULT_RSTVEC),
    DEFINE_PROP_END_OF_LIST(),
};

static gchar *riscv_gdb_arch_name(CPUState *cs)
{
    RISCVCPU *cpu = RISCV_CPU(cs);
    CPURISCVState *env = &cpu->env;

    if (riscv_cpu_is_32bit(env)) {
        return g_strdup("riscv:rv32");
    } else {
        return g_strdup("riscv:rv64");
    }
}

static const char *riscv_gdb_get_dynamic_xml(CPUState *cs, const char *xmlname)
{
    RISCVCPU *cpu = RISCV_CPU(cs);

    if (strcmp(xmlname, "riscv-csr.xml") == 0) {
        return cpu->dyn_csr_xml;
    }

    return NULL;
}

#ifndef CONFIG_USER_ONLY
#include "hw/core/sysemu-cpu-ops.h"

static const struct SysemuCPUOps riscv_sysemu_ops = {
    .get_phys_page_debug = riscv_cpu_get_phys_page_debug,
    .write_elf64_note = riscv_cpu_write_elf64_note,
    .write_elf32_note = riscv_cpu_write_elf32_note,
    .legacy_vmsd = &vmstate_riscv_cpu,
};
#endif

#include "hw/core/tcg-cpu-ops.h"

static const struct TCGCPUOps riscv_tcg_ops = {
    .initialize = riscv_translate_init,
    .synchronize_from_tb = riscv_cpu_synchronize_from_tb,
    .cpu_exec_interrupt = riscv_cpu_exec_interrupt,
    .tlb_fill = riscv_cpu_tlb_fill,

#ifndef CONFIG_USER_ONLY
    .do_interrupt = riscv_cpu_do_interrupt,
    .do_transaction_failed = riscv_cpu_do_transaction_failed,
    .do_unaligned_access = riscv_cpu_do_unaligned_access,
#endif /* !CONFIG_USER_ONLY */
};

static void riscv_cpu_class_init(ObjectClass *c, void *data)
{
    RISCVCPUClass *mcc = RISCV_CPU_CLASS(c);
    CPUClass *cc = CPU_CLASS(c);
    DeviceClass *dc = DEVICE_CLASS(c);

    device_class_set_parent_realize(dc, riscv_cpu_realize,
                                    &mcc->parent_realize);

    device_class_set_parent_reset(dc, riscv_cpu_reset, &mcc->parent_reset);

    cc->class_by_name = riscv_cpu_class_by_name;
    cc->has_work = riscv_cpu_has_work;
    cc->dump_state = riscv_cpu_dump_state;
    cc->set_pc = riscv_cpu_set_pc;
    cc->gdb_read_register = riscv_cpu_gdb_read_register;
    cc->gdb_write_register = riscv_cpu_gdb_write_register;
    cc->gdb_num_core_regs = 33;
    cc->load_state = riscv_cpu_load_state;
#if defined(TARGET_RISCV32)
    cc->gdb_core_xml_file = "riscv-32bit-cpu.xml";
#elif defined(TARGET_RISCV64)
    cc->gdb_core_xml_file = "riscv-64bit-cpu.xml";
#endif
    cc->gdb_stop_before_watchpoint = true;
    cc->disas_set_info = riscv_cpu_disas_set_info;
#ifndef CONFIG_USER_ONLY
    cc->sysemu_ops = &riscv_sysemu_ops;
#endif
    cc->gdb_arch_name = riscv_gdb_arch_name;
    cc->gdb_get_dynamic_xml = riscv_gdb_get_dynamic_xml;
    cc->tcg_ops = &riscv_tcg_ops;

    device_class_set_props(dc, riscv_cpu_properties);
}

char *riscv_isa_string(RISCVCPU *cpu)
{
    int i;
    const size_t maxlen = sizeof("rv128") + sizeof(riscv_exts) + 1;
    char *isa_str = g_new(char, maxlen);
    char *p = isa_str + snprintf(isa_str, maxlen, "rv%d", TARGET_LONG_BITS);
    for (i = 0; i < sizeof(riscv_exts); i++) {
        if (cpu->env.misa & RV(riscv_exts[i])) {
            *p++ = qemu_tolower(riscv_exts[i]);
        }
    }
    *p = '\0';
    return isa_str;
}

static gint riscv_cpu_list_compare(gconstpointer a, gconstpointer b)
{
    ObjectClass *class_a = (ObjectClass *)a;
    ObjectClass *class_b = (ObjectClass *)b;
    const char *name_a, *name_b;

    name_a = object_class_get_name(class_a);
    name_b = object_class_get_name(class_b);
    return strcmp(name_a, name_b);
}

static void riscv_cpu_list_entry(gpointer data, gpointer user_data)
{
    const char *typename = object_class_get_name(OBJECT_CLASS(data));
    int len = strlen(typename) - strlen(RISCV_CPU_TYPE_SUFFIX);

    qemu_printf("%.*s\n", len, typename);
}

void riscv_cpu_list(void)
{
    GSList *list;

    list = object_class_get_list(TYPE_RISCV_CPU, false);
    list = g_slist_sort(list, riscv_cpu_list_compare);
    g_slist_foreach(list, riscv_cpu_list_entry, NULL);
    g_slist_free(list);
}

#define DEFINE_CPU(type_name, initfn)      \
    {                                      \
        .name = type_name,                 \
        .parent = TYPE_RISCV_CPU,          \
        .instance_init = initfn            \
    }

static const TypeInfo riscv_cpu_type_infos[] = {
    {
        .name = TYPE_RISCV_CPU,
        .parent = TYPE_CPU,
        .instance_size = sizeof(RISCVCPU),
        .instance_align = __alignof__(RISCVCPU),
        .instance_init = riscv_cpu_init,
        .abstract = true,
        .class_size = sizeof(RISCVCPUClass),
        .class_init = riscv_cpu_class_init,
    },
    DEFINE_CPU(TYPE_RISCV_CPU_ANY,              riscv_any_cpu_init),
#if defined(TARGET_RISCV32)
    DEFINE_CPU(TYPE_RISCV_CPU_BASE32,           rv32_base_cpu_init),
    DEFINE_CPU(TYPE_RISCV_CPU_IBEX,             rv32_ibex_cpu_init),
    DEFINE_CPU(TYPE_RISCV_CPU_SIFIVE_E31,       rv32_sifive_e_cpu_init),
    DEFINE_CPU(TYPE_RISCV_CPU_SIFIVE_E34,       rv32_imafcu_nommu_cpu_init),
    DEFINE_CPU(TYPE_RISCV_CPU_SIFIVE_U34,       rv32_sifive_u_cpu_init),
#elif defined(TARGET_RISCV64)
    DEFINE_CPU(TYPE_RISCV_CPU_BASE64,           rv64_base_cpu_init),
    DEFINE_CPU(TYPE_RISCV_CPU_SIFIVE_E51,       rv64_sifive_e_cpu_init),
    DEFINE_CPU(TYPE_RISCV_CPU_SIFIVE_U54,       rv64_sifive_u_cpu_init),
    DEFINE_CPU(TYPE_RISCV_CPU_SHAKTI_C,         rv64_sifive_u_cpu_init),
#endif
};

DEFINE_TYPES(riscv_cpu_type_infos)
