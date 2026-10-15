#include "arm7.h"
#include <string.h>
#include <stdlib.h>
#if defined(_MSC_VER)
#define ARM7_HOT_INLINE __forceinline
#else
#define ARM7_HOT_INLINE inline
#endif
static unsigned bank_index(uint32_t mode)
{
    switch (mode & 31) {
    case 0x10: case 0x1f: return 0;
    case 0x11: return 1;
    case 0x12: return 2;
    case 0x13: return 3;
    case 0x17: return 4;
    case 0x1b: return 5;
    default: return 6;
    }
}
static int fail(Arm7 *c, const char *message)
{
    c->halted = 1; c->error = message; return 0;
}
int arm7_set_cpsr(Arm7 *c, uint32_t value)
{
    unsigned old = bank_index(c->cpsr), next = bank_index(value), i;
    if (next == 6) return fail(c, "invalid CPSR mode");
    if (old != next) {
        c->bank[old][0] = c->r[13]; c->bank[old][1] = c->r[14];
        c->r[13] = c->bank[next][0]; c->r[14] = c->bank[next][1];
        if (old == 1 || next == 1) {
            for (i = 0; i < 5; ++i) {
                if (old == 1) { c->fiq_hi[i] = c->r[8 + i]; c->r[8 + i] = c->user_hi[i]; }
                else { c->user_hi[i] = c->r[8 + i]; c->r[8 + i] = c->fiq_hi[i]; }
            }
        }
    }
    c->cpsr = value; return 1;
}
void arm7_reset(Arm7 *c, Bus *bus, uint32_t pc)
{
    memset(c, 0, sizeof(*c)); c->bus = bus; c->cpsr = 0xd3; c->r[15] = pc & ~3u;
}
static uint32_t reg(const Arm7 *c, unsigned r)
{
    return r == 15 ? c->last_pc + ((c->cpsr & ARM_T) ? 4u : 8u) : c->r[r];
}
static uint32_t user_reg(const Arm7 *c, unsigned r)
{
    unsigned bank = bank_index(c->cpsr);
    if (r >= 8 && r <= 12 && bank == 1) return c->user_hi[r - 8];
    if (r >= 13 && r <= 14 && bank) return c->bank[0][r - 13];
    return reg(c, r);
}
static void set_user_reg(Arm7 *c, unsigned r, uint32_t v)
{
    unsigned bank = bank_index(c->cpsr);
    if (r >= 8 && r <= 12 && bank == 1) c->user_hi[r - 8] = v;
    else if (r >= 13 && r <= 14 && bank) c->bank[0][r - 13] = v;
    else c->r[r] = v;
}
static uint32_t ror(uint32_t x, unsigned n)
{
    n &= 31; return n ? (x >> n) | (x << (32 - n)) : x;
}
static void nz(Arm7 *c, uint32_t v)
{
    c->cpsr = (c->cpsr & ~(ARM_N | ARM_Z)) | (v & ARM_N) | (v == 0 ? ARM_Z : 0);
}
static void carry(Arm7 *c, unsigned bit)
{
    c->cpsr = (c->cpsr & ~ARM_C) | (bit ? ARM_C : 0);
}
static uint32_t shift(uint32_t v, unsigned type, unsigned n, int immediate, unsigned *cy)
{
    if (!n) {
        if (!immediate || type == 0) return v;
        if (type == 3) { uint32_t out = (*cy << 31) | (v >> 1); *cy = v & 1; return out; }
        n = 32;
    }
    switch (type) {
    case 0:
        *cy = n <= 32 ? ((v >> (32 - n)) & 1) : 0;
        return n < 32 ? v << n : 0;
    case 1:
        *cy = n <= 32 ? ((v >> (n - 1)) & 1) : 0;
        return n < 32 ? v >> n : 0;
    case 2:
        *cy = n < 32 ? ((v >> (n - 1)) & 1) : v >> 31;
        if (n >= 32) return (v & ARM_N) ? UINT32_MAX : 0;
        return (v >> n) | ((v & ARM_N) ? UINT32_MAX << (32 - n) : 0);
    default:
        v = ror(v, n); *cy = v >> 31; return v;
    }
}
static uint32_t add_flags(Arm7 *c, uint32_t a, uint32_t b, unsigned ci, int flags)
{
    uint64_t sum = (uint64_t)a + b + ci;
    uint32_t v = (uint32_t)sum;
    if (flags) {
        nz(c, v); carry(c, (unsigned)(sum >> 32));
        c->cpsr = (c->cpsr & ~ARM_V) | ((~(a ^ b) & (a ^ v) & ARM_N) ? ARM_V : 0);
    }
    return v;
}
/* Bits index NZCV directly; no runtime condition decoder or flag snapshots. */
static const uint16_t condition_mask[16]={
    0xf0f0,0x0f0f,0xcccc,0x3333,0xff00,0x00ff,0xaaaa,0x5555,
    0x0c0c,0xf3f3,0xaa55,0x55aa,0x0a05,0xf5fa,0xffff,0x0000
};
static int cond(const Arm7 *c,unsigned cc)
{return (condition_mask[cc]>>(c->cpsr>>28))&1;}
static void pc_write(Arm7 *c, uint32_t v)
{
    c->r[15] = v & ((c->cpsr & ARM_T) ? ~1u : ~3u);
}
static void bx(Arm7 *c, uint32_t v)
{
    c->cpsr = (c->cpsr & ~ARM_T) | ((v & 1) ? ARM_T : 0); pc_write(c, v);
}
static int restore(Arm7 *c)
{
    unsigned bank = bank_index(c->cpsr);
    if (!bank) return fail(c, "exception return without SPSR");
    return arm7_set_cpsr(c, c->spsr[bank]);
}
static void exception(Arm7 *c, uint32_t mode, uint32_t vector, uint32_t lr, int fiq)
{
    uint32_t saved = c->cpsr;
    arm7_set_cpsr(c, (saved & ~(ARM_T | 31u)) | mode | 0x80u | (fiq ? 0x40u : 0));
    c->spsr[bank_index(mode)] = saved; c->r[14] = lr; c->r[15] = vector;
}
static uint32_t load_word(Arm7 *c, uint32_t a)
{
    return ror(bus_read(c->bus, a & ~3u, 4), (a & 3) * 8);
}
static uint32_t load_half(Arm7 *c, uint32_t a, int sign)
{
    uint32_t v;
    if (sign && (a & 1)) return (uint32_t)(int32_t)(int8_t)bus_read(c->bus, a, 1);
    v = bus_read(c->bus, a & ~1u, 2);
    if (sign) return (uint32_t)(int32_t)(int16_t)v;
    return ror(v, (a & 1) * 8);
}
/* True for ARM data-processing encodings that cannot be mistaken for any of
   the special forms sharing opcode space with them (BX, MRS, MSR, multiplies,
   SWP, halfword transfers). Those all either have bits 7 and 4 set with
   bit 25 clear, or sit in the "TST/TEQ/CMP/CMN without S" hole (bits 24:23 =
   10, bit 20 = 0). Anything else keeps taking the original decode ladder. */
static int plain_data_processing(uint32_t op)
{
    return (op & 0x0c000000u) == 0 && (op & 0x01900000u) != 0x01000000u &&
        ((op & 0x02000000u) || (op & 0x90u) != 0x90u);
}
#include "arm7_decode.inc"
static int arm(Arm7 *c, uint32_t op,const Decoded *d)
{
    unsigned rn = d->rn, rd = d->rd;
    unsigned rm = d->rm, rs = d->rs, cy = !!(c->cpsr & ARM_C);
    uint32_t a, b, v, address;
    switch(d->kind){
    case A_DP:goto data_processing;case A_SINGLE:goto single_transfer;
    case A_BRANCH:goto branch;case A_BX:goto a_bx;case A_MRS:goto a_mrs;
    case A_MSR:goto a_msr;case A_LONG_MUL:goto a_long_mul;case A_MUL:goto a_mul;
    case A_SWP:goto a_swp;case A_HALF:goto a_half;case A_BLOCK:goto a_block;
    case A_SWI:goto a_swi;default:goto a_reserved;
    }
    a_bx: { bx(c, reg(c, rm)); return 1; }
    a_mrs: {
        if (rd == 15) return fail(c, "unpredictable MRS pc");
        if ((op & 0x00400000u) && !bank_index(c->cpsr)) return fail(c, "MRS SPSR in user/system mode");
        c->r[rd] = (op & 0x00400000u) ? c->spsr[bank_index(c->cpsr)] : c->cpsr; return 1;
    }
    a_msr: {
        uint32_t mask = 0;
        unsigned i, bank = bank_index(c->cpsr);
        b = (op & 0x02000000u) ? d->immediate : reg(c, rm);
        for (i = 0; i < 4; ++i) if (op & (1u << (16 + i))) mask |= 255u << (8 * i);
        /* ARMv4T: flags and control; reserved bits are preserved. T cannot change via MSR. */
        mask &= (op & 0x00400000u) ? 0xf00000ffu : 0xf00000dfu;
        if (op & 0x00400000u) {
            if (!bank) return fail(c, "MSR SPSR in user/system mode");
            c->spsr[bank] = (c->spsr[bank] & ~mask) | (b & mask); return 1;
        }
        if ((c->cpsr & 31) == 0x10) mask &= 0xf0000000u;
        return arm7_set_cpsr(c, (c->cpsr & ~mask) | (b & mask));
    }
    a_long_mul: {
        uint64_t product;
        unsigned hi = rn, lo = rd;
        if (hi == 15 || lo == 15 || rm == 15 || rs == 15 || hi == lo) return fail(c, "unpredictable long multiply");
        if (op & 0x00400000u) product = (uint64_t)((int64_t)(int32_t)c->r[rm] * (int64_t)(int32_t)c->r[rs]);
        else product = (uint64_t)c->r[rm] * c->r[rs];
        if (op & 0x00200000u) product += (uint64_t)c->r[hi] << 32 | c->r[lo];
        c->r[lo] = (uint32_t)product; c->r[hi] = (uint32_t)(product >> 32);
        if (op & 0x00100000u) c->cpsr = (c->cpsr & ~(ARM_N | ARM_Z)) | (c->r[hi] & ARM_N) | (!product ? ARM_Z : 0);
        return 1;
    }
    a_mul: {
        if (rn == 15 || rs == 15 || rm == 15 || ((op & 0x00200000u) && rd == 15)) return fail(c, "unpredictable multiply");
        v = c->r[rm] * c->r[rs];
        if (op & 0x00200000u) v += c->r[rd];
        c->r[rn] = v; if (op & 0x00100000u) nz(c, v); return 1;
    }
    a_swp: {
        unsigned width = (op & 0x00400000u) ? 1u : 4u;
        if (rn == 15 || rd == 15 || rm == 15) return fail(c, "unpredictable SWP");
        address = c->r[rn]; b = c->r[rm];
        v = width == 4 ? load_word(c, address) : bus_read(c->bus, address, 1);
        bus_write(c->bus, width == 4 ? address & ~3u : address, width, b); c->r[rd] = v; return 1;
    }
    a_half: {
        unsigned type = (op >> 5) & 3, load = (op >> 20) & 1;
        if (rd == 15 || (rn == 15 && (!(op & 0x01000000u) || (op & 0x00200000u)))) return fail(c, "unpredictable halfword transfer");
        a = reg(c, rn); b = (op & 0x00400000u) ? d->immediate : reg(c, rm);
        v = (op & 0x00800000u) ? a + b : a - b; address = (op & 0x01000000u) ? v : a;
        if (load) {
            if (type == 1) c->r[rd] = load_half(c, address, 0);
            else if (type == 2) c->r[rd] = (uint32_t)(int32_t)(int8_t)bus_read(c->bus, address, 1);
            else c->r[rd] = load_half(c, address, 1);
        } else {
            if (type != 1) return fail(c, "reserved signed store");
            bus_write(c->bus, address & ~1u, 2, reg(c, rd));
        }
        if (!(op & 0x01000000u) || (op & 0x00200000u)) {
            if (load && rn == rd) return fail(c, "unpredictable load/writeback overlap");
            c->r[rn] = v;
        }
        return 1;
    }
    data_processing:
    {
        unsigned code = d->code, flags = (op >> 20) & 1;
        int write = code < 8 || code > 11;
        if (op & 0x02000000u) {
            unsigned rotation = ((op >> 8) & 15) * 2;
            b = d->immediate; if (rotation) cy = b >> 31;
        } else {
            unsigned amount = (op >> 7) & 31;
            b = reg(c, rm);
            if (op & 0x10u) {
                if (op & 0x80u || rs == 15) return fail(c, "reserved/unpredictable register shift");
                amount = reg(c, rs) & 255;
                if (rm == 15) b += 4;
            }
            b = shift(b, (op >> 5) & 3, amount, !(op & 0x10u), &cy);
        }
        a = reg(c, rn);
        if ((op & 0x10u) && !(op & 0x02000000u) && rn == 15) a += 4;
        if (!write && !flags) return fail(c, "reserved data-processing instruction");
        if (flags && rd == 15 && write) flags = 0;
        switch (code) {
        case 0: case 8: v = a & b; break;
        case 1: case 9: v = a ^ b; break;
        case 2: case 10: v = add_flags(c, a, ~b, 1, (int)flags); break;
        case 3: v = add_flags(c, b, ~a, 1, (int)flags); break;
        case 4: case 11: v = add_flags(c, a, b, 0, (int)flags); break;
        case 5: v = add_flags(c, a, b, !!(c->cpsr & ARM_C), (int)flags); break;
        case 6: v = add_flags(c, a, ~b, !!(c->cpsr & ARM_C), (int)flags); break;
        case 7: v = add_flags(c, b, ~a, !!(c->cpsr & ARM_C), (int)flags); break;
        case 12: v = a | b; break;
        case 13: v = b; break;
        case 14: v = a & ~b; break;
        default: v = ~b; break;
        }
        if (flags && (code < 2 || code == 8 || code == 9 || code >= 12)) { nz(c, v); carry(c, cy); }
        if (write) {
            if (rd == 15) { if ((op & 0x00100000u) && !restore(c)) return 0; pc_write(c, v); }
            else c->r[rd] = v;
        }
        return 1;
    }
    single_transfer:
    {
        unsigned load = (op >> 20) & 1, width = (op & 0x00400000u) ? 1u : 4u;
        a = reg(c, rn);
        if (op & 0x02000000u) {
            if (op & 0x10u) return fail(c, "reserved transfer shift");
            b = shift(reg(c, rm), (op >> 5) & 3, (op >> 7) & 31, 1, &cy);
        } else b = d->immediate;
        v = (op & 0x00800000u) ? a + b : a - b; address = (op & 0x01000000u) ? v : a;
        if (load) {
            b = width == 4 ? load_word(c, address) : bus_read(c->bus, address, width);
            if (rd == 15) { if (width == 1) return fail(c, "unpredictable LDRB pc"); pc_write(c, b); }
            else c->r[rd] = b;
        } else bus_write(c->bus, width == 4 ? address & ~3u : address, width, rd == 15 ? c->last_pc + 12 : reg(c, rd));
        if (!(op & 0x01000000u) || (op & 0x00200000u)) {
            if (rn == 15 || (load && rn == rd)) return fail(c, "unpredictable transfer writeback");
            c->r[rn] = v;
        }
        return 1;
    }
    a_block: {
        unsigned list = op & 0xffffu, count = d->count, i, first = d->code;
        unsigned load = (op >> 20) & 1, wb = (op >> 21) & 1, s = (op >> 22) & 1;
        int user = s && (!load || !(list & 0x8000u));
        uint32_t final, loaded_pc = 0;
        int has_pc = 0;
        if (rn == 15) return fail(c, "unpredictable block-transfer base");
        a = c->r[rn];
        if (!count) { list = 0x8000; count = 16; first = 15; }
        final = (op & 0x00800000u) ? a + 4 * count : a - 4 * count;
        address = (op & 0x00800000u) ? a + ((op & 0x01000000u) ? 4u : 0u) : a - 4 * count + ((op & 0x01000000u) ? 0u : 4u);
        for (i = 0; i < 16; ++i) if (list & (1u << i)) {
            if (load) {
                v = bus_read(c->bus, address & ~3u, 4);
                if (i == 15) { loaded_pc = v; has_pc = 1; }
                else if (user) set_user_reg(c, i, v); else c->r[i] = v;
            } else {
                v = i == 15 ? c->last_pc + 12 : user ? user_reg(c, i) : c->r[i];
                if (wb && i == rn && i != first) v = final;
                bus_write(c->bus, address & ~3u, 4, v);
            }
            address += 4;
        }
        if (wb && !(load && (list & (1u << rn)))) c->r[rn] = final;
        /* Writeback belongs to the old mode's bank, before SPSR restoration. */
        if (has_pc) { if (s && !restore(c)) return 0; pc_write(c, loaded_pc); }
        return 1;
    }
    branch:
    {
        uint32_t offset = d->immediate;
        if (op & 0x01000000u) c->r[14] = c->last_pc + 4;
        pc_write(c, c->last_pc + 8 + offset); return 1;
    }
    a_swi: { exception(c, 0x13, 8, c->last_pc + 4, 0); return 1; }
    a_reserved:return fail(c, "unimplemented coprocessor/reserved ARM instruction");
}
static unsigned multiply_cycles(uint32_t value,int signed_value);
static ARM7_HOT_INLINE int thumb(Arm7 *c, uint32_t op,unsigned *clocks,const Decoded *d)
{
    unsigned rd = d->rd, rs = d->rs, rn = d->rn;
    unsigned code, cy = !!(c->cpsr & ARM_C), i;
    uint32_t a, b, v, address;
    *clocks=d->clocks;
    switch(d->kind){
    case T_SHIFT:goto t_shift;
    case T_ADDSUB:goto t_addsub;
    case T_MOV_IMM:goto t_mov_imm;case T_CMP_IMM:goto t_cmp_imm;
    case T_ADD_IMM:goto t_add_imm;case T_SUB_IMM:goto t_sub_imm;
    case T_HIGH_ADD:goto t_high_add;case T_HIGH_CMP:goto t_high_cmp;
    case T_HIGH_MOV:goto t_high_mov;case T_HIGH_BX:goto t_high_bx;
    case T_NOP:return 1;
    case T_ALU_AND:goto t_alu_and;
    case T_ALU_EOR:goto t_alu_eor;
    case T_ALU_LSL:goto t_alu_lsl;
    case T_ALU_LSR:goto t_alu_lsr;
    case T_ALU_ASR:goto t_alu_asr;
    case T_ALU_ADC:goto t_alu_adc;
    case T_ALU_SBC:goto t_alu_sbc;
    case T_ALU_ROR:goto t_alu_ror;
    case T_ALU_TST:goto t_alu_tst;
    case T_ALU_NEG:goto t_alu_neg;
    case T_ALU_CMP:goto t_alu_cmp;
    case T_ALU_CMN:goto t_alu_cmn;
    case T_ALU_ORR:goto t_alu_orr;
    case T_ALU_MUL:goto t_alu_mul;
    case T_ALU_BIC:goto t_alu_bic;
    case T_ALU_MVN:goto t_alu_mvn;
    case T_LITERAL:goto t_literal;
    case T_REGISTER_MEMORY:goto t_register_memory;
    case T_IMMEDIATE_MEMORY:goto t_immediate_memory;
    case T_HALF:goto t_half;
    case T_SP_MEMORY:goto t_sp_memory;
    case T_ADDRESS:goto t_address;
    case T_SP_ADD:goto t_sp_add;
    case T_PUSHPOP:goto t_pushpop;
    case T_MULTIPLE:goto t_multiple;
    case T_CONDITIONAL:goto t_conditional;
    case T_BRANCH:goto t_branch;
    case T_BL_PREFIX:goto t_bl_prefix;
    case T_BL_SUFFIX:goto t_bl_suffix;
    default:goto t_reserved;
    }
    t_addsub: {
        a = c->r[rs]; b = (op & 0x400u) ? rn : c->r[rn];
        c->r[rd] = (op & 0x200u) ? add_flags(c, a, ~b, 1, 1) : add_flags(c, a, b, 0, 1); return 1;
    }
    t_shift: {
        v = shift(c->r[rs], d->code, d->immediate, 1, &cy);
        c->r[rd] = v; nz(c, v); carry(c, cy); return 1;
    }
    t_mov_imm:c->r[rd]=d->immediate;nz(c,d->immediate);return 1;
    t_cmp_imm:(void)add_flags(c,c->r[rd],~d->immediate,1,1);return 1;
    t_add_imm:c->r[rd]=add_flags(c,c->r[rd],d->immediate,0,1);return 1;
    t_sub_imm:c->r[rd]=add_flags(c,c->r[rd],~d->immediate,1,1);return 1;
    t_alu_and:v=c->r[rd]&c->r[rs];goto t_alu_result;
    t_alu_eor:v=c->r[rd]^c->r[rs];goto t_alu_result;
    t_alu_lsl:v=shift(c->r[rd],0,c->r[rs]&255,0,&cy);carry(c,cy);goto t_alu_result;
    t_alu_lsr:v=shift(c->r[rd],1,c->r[rs]&255,0,&cy);carry(c,cy);goto t_alu_result;
    t_alu_asr:v=shift(c->r[rd],2,c->r[rs]&255,0,&cy);carry(c,cy);goto t_alu_result;
    t_alu_adc:c->r[rd]=add_flags(c,c->r[rd],c->r[rs],cy,1);return 1;
    t_alu_sbc:c->r[rd]=add_flags(c,c->r[rd],~c->r[rs],cy,1);return 1;
    t_alu_ror:v=shift(c->r[rd],3,c->r[rs]&255,0,&cy);carry(c,cy);goto t_alu_result;
    t_alu_tst:nz(c,c->r[rd]&c->r[rs]);return 1;
    t_alu_neg:c->r[rd]=add_flags(c,0,~c->r[rs],1,1);return 1;
    t_alu_cmp:(void)add_flags(c,c->r[rd],~c->r[rs],1,1);return 1;
    t_alu_cmn:(void)add_flags(c,c->r[rd],c->r[rs],0,1);return 1;
    t_alu_orr:v=c->r[rd]|c->r[rs];goto t_alu_result;
    t_alu_mul:*clocks=1+multiply_cycles(c->r[rs],1);v=c->r[rd]*c->r[rs];goto t_alu_result;
    t_alu_bic:v=c->r[rd]&~c->r[rs];goto t_alu_result;
    t_alu_mvn:v=~c->r[rs];
    t_alu_result:c->r[rd]=v;nz(c,v);return 1;
    t_high_add:v=reg(c,rd)+reg(c,rs);if(rd==15)pc_write(c,v);else c->r[rd]=v;return 1;
    t_high_cmp:(void)add_flags(c,reg(c,rd),~reg(c,rs),1,1);return 1;
    t_high_mov:v=reg(c,rs);if(rd==15)pc_write(c,v);else c->r[rd]=v;return 1;
    t_high_bx:if(op&0x80u)return fail(c,"BLX is not ARMv4T");bx(c,reg(c,rs));return 1;
    t_literal: {
        c->r[rd] = load_word(c, ((c->last_pc + 4) & ~3u) + d->immediate); return 1;
    }
    t_register_memory: {
        code = d->code; address = c->r[rs] + c->r[rn];
        switch (code) {
        case 0: bus_write(c->bus, address & ~3u, 4, c->r[rd]); break;
        case 1: bus_write(c->bus, address & ~1u, 2, c->r[rd]); break;
        case 2: bus_write(c->bus, address, 1, c->r[rd]); break;
        case 3: c->r[rd] = (uint32_t)(int32_t)(int8_t)bus_read(c->bus, address, 1); break;
        case 4: c->r[rd] = load_word(c, address); break;
        case 5: c->r[rd] = load_half(c, address, 0); break;
        case 6: c->r[rd] = bus_read(c->bus, address, 1); break;
        default: c->r[rd] = load_half(c, address, 1); break;
        }
        return 1;
    }
    t_immediate_memory: {
        unsigned width = d->code;
        address = c->r[rs] + d->immediate;
        if (op & 0x800u) c->r[rd] = width == 4 ? load_word(c, address) : bus_read(c->bus, address, 1);
        else bus_write(c->bus, width == 4 ? address & ~3u : address, width, c->r[rd]); return 1;
    }
    t_half: {
        address = c->r[rs] + d->immediate;
        if (op & 0x800u) c->r[rd] = load_half(c, address, 0); else bus_write(c->bus, address & ~1u, 2, c->r[rd]); return 1;
    }
    t_sp_memory: {
        address = c->r[13] + d->immediate;
        if (op & 0x800u) c->r[rd] = load_word(c, address); else bus_write(c->bus, address & ~3u, 4, c->r[rd]); return 1;
    }
    t_address: {
        c->r[rd] = ((op & 0x800u) ? c->r[13] : (c->last_pc + 4) & ~3u) + d->immediate; return 1;
    }
    t_sp_add: {
        b = d->immediate; c->r[13] = (op & 0x80u) ? c->r[13] - b : c->r[13] + b; return 1;
    }
    t_pushpop: {
        unsigned list = op & 255, count = d->count, pop = (op >> 11) & 1;
        if (!count) return fail(c, "empty Thumb PUSH/POP");
        address = pop ? c->r[13] : c->r[13] - count * 4;
        for (i = 0; i < 8; ++i) if (list & (1u << i)) {
            if (pop) c->r[i] = bus_read(c->bus, address & ~3u, 4); else bus_write(c->bus, address & ~3u, 4, c->r[i]); address += 4;
        }
        if (op & 0x100u) {
            if (pop) pc_write(c, bus_read(c->bus, address & ~3u, 4)); else bus_write(c->bus, address & ~3u, 4, c->r[14]);
        }
        c->r[13] = pop ? c->r[13] + count * 4 : c->r[13] - count * 4; return 1;
    }
    t_multiple: {
        unsigned list = op & 255, load = (op >> 11) & 1, first = d->code, count = d->count;
        a = c->r[rn]; address = a;
        if (!list) {
            if (load) pc_write(c, bus_read(c->bus, address & ~3u, 4));
            else bus_write(c->bus, address & ~3u, 4, c->last_pc + 4);
            c->r[rn] += 64; return 1;
        }
        for (i = 0; i < 8; ++i) if (list & (1u << i)) {
            if (load) c->r[i] = bus_read(c->bus, address & ~3u, 4);
            else bus_write(c->bus, address & ~3u, 4, i == rn && i != first ? a + count * 4 : c->r[i]);
            address += 4;
        }
        if (!load || !(list & (1u << rn))) c->r[rn] = address; return 1;
    }
    t_conditional: {
        int taken;code = d->code;
        taken=code==15||((d->opcode>>(c->cpsr>>28))&1);*clocks=taken?3u:1u;
        if (code == 15) { exception(c, 0x13, 8, c->last_pc + 2, 0); return 1; }
        if (code == 14) return fail(c, "undefined Thumb instruction");
        if (taken) pc_write(c, c->last_pc + 4 + d->immediate); return 1;
    }
    t_branch: {
        b = d->immediate; pc_write(c, c->last_pc + 4 + b); return 1;
    }
    t_bl_prefix: {
        b = d->immediate; c->r[14] = c->last_pc + 4 + b; return 1;
    }
    t_bl_suffix: {
        address = c->r[14] + d->immediate; c->r[14] = (c->last_pc + 2) | 1u; pc_write(c, address); return 1;
    }
    t_reserved:return fail(c, "unimplemented/reserved Thumb instruction");
}
static unsigned register_count(uint32_t mask)
{
    unsigned n=0;while(mask){n+=mask&1;mask>>=1;}return n;
}
static unsigned multiply_cycles(uint32_t value,int signed_value)
{
    unsigned n;
    for(n=1;n<4;++n){uint32_t upper=value>>(n*8);
        if(!upper||(signed_value&&upper==(UINT32_MAX>>(n*8))))return n;}
    return 4;
}
int arm7_step(Arm7 *c)
{
    int ok;Decoded scratch;const Decoded *d;
    unsigned clocks=1;
    if (c->halted) return 0;
    c->last_pc = c->r[15]; c->bus->pc = c->last_pc; c->bus->ticks = c->cycles;
    if (c->fiq && !(c->cpsr & 0x40u)) { exception(c, 0x11, 0x1c, c->last_pc + 4, 1); ++c->cycles;c->clock_cycles+=3; return 1; }
    if (c->irq && !(c->cpsr & 0x80u)) { exception(c, 0x12, 0x18, c->last_pc + 4, 0); ++c->cycles;c->clock_cycles+=3; return 1; }
    c->last_opcode = c->cache?bus_fetch(c->bus,c->last_pc,(c->cpsr&ARM_T)?2:4,&c->cache->fetch):
        bus_read(c->bus, c->last_pc, (c->cpsr & ARM_T) ? 2 : 4);
    if (c->bus->fault) return fail(c, "instruction fetch/bus fault pending");
    if(c->cpsr&ARM_T){
        if(c->cache&&c->last_opcode<=65535){d=&c->cache->thumb[c->last_opcode];if(!d->kind)decode_thumb((Decoded *)d,c->last_opcode);}
        else {decode_thumb(&scratch,c->last_opcode);d=&scratch;}
        c->r[15]+=2;ok=thumb(c,c->last_opcode,&clocks,d);
    }
    else {
        unsigned condition=c->last_opcode>>28;
        /* Evaluate the ARM condition once, not independently for timing and
           execution. NV remains a reserved-instruction fault, not a skip. */
        if(condition==15){c->r[15]+=4;ok=fail(c,"ARMv4T NV/reserved instruction");}
        else if(condition==14||cond(c,condition)){
            if(c->cache){d=&c->cache->arm[(c->last_opcode*2654435761u)>>22];
                if(!d->kind||d->opcode!=c->last_opcode)decode_arm((Decoded *)d,c->last_opcode);}
            else {decode_arm(&scratch,c->last_opcode);d=&scratch;}
            clocks=d->clocks;
            if(d->kind==A_LONG_MUL)clocks+=multiply_cycles(c->r[d->rs],!!(c->last_opcode&(1u<<22)));
            else if(d->kind==A_MUL)clocks+=multiply_cycles(c->r[d->rs],1);
            c->r[15]+=4;ok=arm(c,c->last_opcode,d);
        }else {c->r[15]+=4;ok=1;}
    }
    ++c->instructions; ++c->cycles;c->clock_cycles+=clocks; return ok;
}
