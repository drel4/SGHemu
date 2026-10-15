/* Frozen pre-optimization decoder, used only by differential tests.
   Behavioral equivalence is not a substitute for architecture test programs. */
#define arm7_reset arm7_reference_reset
#define arm7_step arm7_reference_step
#define arm7_set_cpsr arm7_reference_set_cpsr
#include "arm7.h"
#include <string.h>
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
static int cond(const Arm7 *c, unsigned cc)
{
    int n = !!(c->cpsr & ARM_N), z = !!(c->cpsr & ARM_Z);
    int cy = !!(c->cpsr & ARM_C), v = !!(c->cpsr & ARM_V);
    switch (cc) {
    case 0: return z; case 1: return !z;
    case 2: return cy; case 3: return !cy;
    case 4: return n; case 5: return !n;
    case 6: return v; case 7: return !v;
    case 8: return cy && !z; case 9: return !cy || z;
    case 10: return n == v; case 11: return n != v;
    case 12: return !z && n == v; case 13: return z || n != v;
    case 14: return 1; default: return 0;
    }
}
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
static int arm(Arm7 *c, uint32_t op)
{
    unsigned rn = (op >> 16) & 15, rd = (op >> 12) & 15;
    unsigned rm = op & 15, rs = (op >> 8) & 15, cy = !!(c->cpsr & ARM_C);
    uint32_t a, b, v, address;
    if ((op >> 28) == 15) return fail(c, "ARMv4T NV/reserved instruction");
    if (!cond(c, op >> 28)) return 1;
    if (plain_data_processing(op)) goto data_processing; /* the common case: skip seven failed mask tests */
    /* Bits 27:26 = 01 (LDR/STR) and bits 27:25 = 101 (B/BL) can only be those
       instructions; every special form tested below lives in the 00x space. */
    if ((op & 0x0c000000u) == 0x04000000u) goto single_transfer;
    if ((op & 0x0e000000u) == 0x0a000000u) goto branch;
    if ((op & 0x0ffffff0u) == 0x012fff10u) { bx(c, reg(c, rm)); return 1; }
    if ((op & 0x0fbf0fffu) == 0x010f0000u) {
        if (rd == 15) return fail(c, "unpredictable MRS pc");
        if ((op & 0x00400000u) && !bank_index(c->cpsr)) return fail(c, "MRS SPSR in user/system mode");
        c->r[rd] = (op & 0x00400000u) ? c->spsr[bank_index(c->cpsr)] : c->cpsr; return 1;
    }
    if ((op & 0x0fb0fff0u) == 0x0120f000u || (op & 0x0fb0f000u) == 0x0320f000u) {
        uint32_t mask = 0;
        unsigned i, bank = bank_index(c->cpsr);
        b = (op & 0x02000000u) ? ror(op & 255, ((op >> 8) & 15) * 2) : reg(c, rm);
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
    if ((op & 0x0f8000f0u) == 0x00800090u) {
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
    if ((op & 0x0fc000f0u) == 0x00000090u) {
        if (rn == 15 || rs == 15 || rm == 15 || ((op & 0x00200000u) && rd == 15)) return fail(c, "unpredictable multiply");
        v = c->r[rm] * c->r[rs];
        if (op & 0x00200000u) v += c->r[rd];
        c->r[rn] = v; if (op & 0x00100000u) nz(c, v); return 1;
    }
    if ((op & 0x0fb00ff0u) == 0x01000090u) {
        unsigned width = (op & 0x00400000u) ? 1u : 4u;
        if (rn == 15 || rd == 15 || rm == 15) return fail(c, "unpredictable SWP");
        address = c->r[rn]; b = c->r[rm];
        v = width == 4 ? load_word(c, address) : bus_read(c->bus, address, 1);
        bus_write(c->bus, width == 4 ? address & ~3u : address, width, b); c->r[rd] = v; return 1;
    }
    if ((op & 0x0e000090u) == 0x00000090u && (op & 0x60u)) {
        unsigned type = (op >> 5) & 3, load = (op >> 20) & 1;
        if (rd == 15 || (rn == 15 && (!(op & 0x01000000u) || (op & 0x00200000u)))) return fail(c, "unpredictable halfword transfer");
        a = reg(c, rn); b = (op & 0x00400000u) ? ((op >> 4) & 0xf0u) | (op & 15) : reg(c, rm);
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
    if ((op & 0x0c000000u) == 0) {
        unsigned code = (op >> 21) & 15, flags = (op >> 20) & 1;
        int write = code < 8 || code > 11;
        if (op & 0x02000000u) {
            unsigned rotation = ((op >> 8) & 15) * 2;
            b = ror(op & 255, rotation); if (rotation) cy = b >> 31;
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
    if ((op & 0x0c000000u) == 0x04000000u) {
        unsigned load = (op >> 20) & 1, width = (op & 0x00400000u) ? 1u : 4u;
        a = reg(c, rn);
        if (op & 0x02000000u) {
            if (op & 0x10u) return fail(c, "reserved transfer shift");
            b = shift(reg(c, rm), (op >> 5) & 3, (op >> 7) & 31, 1, &cy);
        } else b = op & 0xfffu;
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
    if ((op & 0x0e000000u) == 0x08000000u) {
        unsigned list = op & 0xffffu, count = 0, i, first = 16;
        unsigned load = (op >> 20) & 1, wb = (op >> 21) & 1, s = (op >> 22) & 1;
        int user = s && (!load || !(list & 0x8000u));
        uint32_t final, loaded_pc = 0;
        int has_pc = 0;
        if (rn == 15) return fail(c, "unpredictable block-transfer base");
        for (i = 0; i < 16; ++i) if (list & (1u << i)) { ++count; if (first == 16) first = i; }
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
    if ((op & 0x0e000000u) == 0x0a000000u) {
        uint32_t offset = (op & 0xffffffu) << 2;
        if (offset & 0x2000000u) offset |= 0xfc000000u;
        if (op & 0x01000000u) c->r[14] = c->last_pc + 4;
        pc_write(c, c->last_pc + 8 + offset); return 1;
    }
    if ((op & 0x0f000000u) == 0x0f000000u) { exception(c, 0x13, 8, c->last_pc + 4, 0); return 1; }
    return fail(c, "unimplemented coprocessor/reserved ARM instruction");
}
static int thumb(Arm7 *c, uint32_t op)
{
    unsigned rd = op & 7, rs = (op >> 3) & 7, rn = (op >> 6) & 7;
    unsigned code, cy = !!(c->cpsr & ARM_C), i;
    uint32_t a, b, v, address;
    if ((op & 0xf800u) == 0x1800u) {
        a = c->r[rs]; b = (op & 0x400u) ? rn : c->r[rn];
        c->r[rd] = (op & 0x200u) ? add_flags(c, a, ~b, 1, 1) : add_flags(c, a, b, 0, 1); return 1;
    }
    if ((op & 0xe000u) == 0) {
        v = shift(c->r[rs], (op >> 11) & 3, (op >> 6) & 31, 1, &cy);
        c->r[rd] = v; nz(c, v); carry(c, cy); return 1;
    }
    if ((op & 0xe000u) == 0x2000u) {
        rd = (op >> 8) & 7; b = op & 255; a = c->r[rd]; code = (op >> 11) & 3;
        if (!code) { c->r[rd] = b; nz(c, b); }
        else if (code == 1) (void)add_flags(c, a, ~b, 1, 1);
        else c->r[rd] = code == 2 ? add_flags(c, a, b, 0, 1) : add_flags(c, a, ~b, 1, 1);
        return 1;
    }
    if ((op & 0xfc00u) == 0x4000u) {
        code = (op >> 6) & 15; a = c->r[rd]; b = c->r[rs];
        switch (code) {
        case 0: v = a & b; break; case 1: v = a ^ b; break;
        case 2: case 3: case 4: v = shift(a, code - 2, b & 255, 0, &cy); carry(c, cy); break;
        case 5: v = add_flags(c, a, b, cy, 1); c->r[rd] = v; return 1;
        case 6: v = add_flags(c, a, ~b, cy, 1); c->r[rd] = v; return 1;
        case 7: v = shift(a, 3, b & 255, 0, &cy); carry(c, cy); break;
        case 8: nz(c, a & b); return 1;
        case 9: c->r[rd] = add_flags(c, 0, ~b, 1, 1); return 1;
        case 10: (void)add_flags(c, a, ~b, 1, 1); return 1;
        case 11: (void)add_flags(c, a, b, 0, 1); return 1;
        case 12: v = a | b; break; case 13: v = a * b; break;
        case 14: v = a & ~b; break; default: v = ~b; break;
        }
        c->r[rd] = v; nz(c, v); return 1;
    }
    if ((op & 0xfc00u) == 0x4400u) {
        code = (op >> 8) & 3; rd |= (op >> 4) & 8; rs = (op >> 3) & 15;
        a = reg(c, rd); b = reg(c, rs);
        if (code == 3) { if (op & 0x80u) return fail(c, "BLX is not ARMv4T"); bx(c, b); }
        else if (code == 1) (void)add_flags(c, a, ~b, 1, 1);
        else { v = code ? b : a + b; if (rd == 15) pc_write(c, v); else c->r[rd] = v; }
        return 1;
    }
    if ((op & 0xf800u) == 0x4800u) {
        rd = (op >> 8) & 7; c->r[rd] = load_word(c, ((c->last_pc + 4) & ~3u) + (op & 255) * 4); return 1;
    }
    if ((op & 0xf000u) == 0x5000u) {
        code = (op >> 9) & 7; address = c->r[rs] + c->r[rn];
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
    if ((op & 0xe000u) == 0x6000u) {
        unsigned width = (op & 0x1000u) ? 1u : 4u;
        address = c->r[rs] + ((op >> 6) & 31) * width;
        if (op & 0x800u) c->r[rd] = width == 4 ? load_word(c, address) : bus_read(c->bus, address, 1);
        else bus_write(c->bus, width == 4 ? address & ~3u : address, width, c->r[rd]); return 1;
    }
    if ((op & 0xf000u) == 0x8000u) {
        address = c->r[rs] + ((op >> 6) & 31) * 2;
        if (op & 0x800u) c->r[rd] = load_half(c, address, 0); else bus_write(c->bus, address & ~1u, 2, c->r[rd]); return 1;
    }
    if ((op & 0xf000u) == 0x9000u) {
        rd = (op >> 8) & 7; address = c->r[13] + (op & 255) * 4;
        if (op & 0x800u) c->r[rd] = load_word(c, address); else bus_write(c->bus, address & ~3u, 4, c->r[rd]); return 1;
    }
    if ((op & 0xf000u) == 0xa000u) {
        c->r[(op >> 8) & 7] = ((op & 0x800u) ? c->r[13] : (c->last_pc + 4) & ~3u) + (op & 255) * 4; return 1;
    }
    if ((op & 0xff00u) == 0xb000u) {
        b = (op & 127) * 4; c->r[13] = (op & 0x80u) ? c->r[13] - b : c->r[13] + b; return 1;
    }
    if ((op & 0xf600u) == 0xb400u) {
        unsigned list = op & 255, count = !!(op & 0x100u), pop = (op >> 11) & 1;
        for (i = 0; i < 8; ++i) count += !!(list & (1u << i));
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
    if ((op & 0xf000u) == 0xc000u) {
        unsigned list = op & 255, load = (op >> 11) & 1, first = 8, count = 0;
        rn = (op >> 8) & 7; a = c->r[rn]; address = a;
        if (!list) {
            if (load) pc_write(c, bus_read(c->bus, address & ~3u, 4));
            else bus_write(c->bus, address & ~3u, 4, c->last_pc + 4);
            c->r[rn] += 64; return 1;
        }
        for (i = 0; i < 8; ++i) if (list & (1u << i)) { ++count; if (first == 8) first = i; }
        for (i = 0; i < 8; ++i) if (list & (1u << i)) {
            if (load) c->r[i] = bus_read(c->bus, address & ~3u, 4);
            else bus_write(c->bus, address & ~3u, 4, i == rn && i != first ? a + count * 4 : c->r[i]);
            address += 4;
        }
        if (!load || !(list & (1u << rn))) c->r[rn] = address; return 1;
    }
    if ((op & 0xf000u) == 0xd000u) {
        code = (op >> 8) & 15;
        if (code == 15) { exception(c, 0x13, 8, c->last_pc + 2, 0); return 1; }
        if (code == 14) return fail(c, "undefined Thumb instruction");
        if (cond(c, code)) pc_write(c, c->last_pc + 4 + (uint32_t)((int32_t)(int8_t)op * 2)); return 1;
    }
    if ((op & 0xf800u) == 0xe000u) {
        b = (op & 0x7ffu) << 1; if (b & 0x800u) b |= 0xfffff000u; pc_write(c, c->last_pc + 4 + b); return 1;
    }
    if ((op & 0xf800u) == 0xf000u) {
        b = (op & 0x7ffu) << 12; if (b & 0x400000u) b |= 0xff800000u; c->r[14] = c->last_pc + 4 + b; return 1;
    }
    if ((op & 0xf800u) == 0xf800u) {
        address = c->r[14] + (op & 0x7ffu) * 2; c->r[14] = (c->last_pc + 2) | 1u; pc_write(c, address); return 1;
    }
    return fail(c, "unimplemented/reserved Thumb instruction");
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
static unsigned instruction_cycles(const Arm7 *c,uint32_t op)
{
    /* ARM DDI 0210 instruction speed summary, S=N=I=one core cycle.
       Memory wait states and bus-width penalties belong to the bus/model. */
    unsigned n,load,rd;
    if(!(c->cpsr&ARM_T)) {
        if(!cond(c,op>>28))return 1;
        if(plain_data_processing(op)) {
            /* Same result as the final data-processing branch below. */
            n=(!(op&(1u<<25))&&!!(op&16))?2:1;rd=(op>>12)&15;
            return n+((rd==15&&((op>>21)&15)!=8&&((op>>21)&15)!=9&&((op>>21)&15)!=10&&((op>>21)&15)!=11)?2:0);
        }
        if((op&0x0c000000u)==0x04000000u){load=(op>>20)&1;rd=(op>>12)&15;return load?3+(rd==15?2:0):2;} /* single LDR/STR */
        if((op&0x0e000000u)==0x0a000000u)return 3; /* B/BL */
        if((op&0x0fbf0fffu)==0x010f0000u||(op&0x0fb0fff0u)==0x0120f000u||(op&0x0fb0f000u)==0x0320f000u)return 1;
        if((op&0x0ffffff0u)==0x012fff10u||(op&0x0e000000u)==0x0a000000u||(op&0x0f000000u)==0x0f000000u)return 3;
        if((op&0x0f8000f0u)==0x00800090u)return 2+multiply_cycles(c->r[(op>>8)&15],!!(op&(1u<<22)))+!!(op&(1u<<21));
        if((op&0x0fc000f0u)==0x00000090u)return 1+multiply_cycles(c->r[(op>>8)&15],1)+!!(op&(1u<<21));
        if((op&0x0fb00ff0u)==0x01000090u)return 4;
        load=(op>>20)&1;rd=(op>>12)&15;
        if((op&0x0e000000u)==0x08000000u){n=register_count(op&65535);if(!n)n=16;return n+1+load+(load&&!!(op&0x8000)?2:0);}
        if((op&0x0c000000u)==0x04000000u||((op&0x0e000090u)==0x00000090u&&(op&0x60u)))return load?3+(rd==15?2:0):2;
        if((op&0x0c000000u)==0){n=(!(op&(1u<<25))&&!!(op&16))?2:1;
            return n+((rd==15&&((op>>21)&15)!=8&&((op>>21)&15)!=9&&((op>>21)&15)!=10&&((op>>21)&15)!=11)?2:0);}
        return 1;
    }
    if(op<0x4000)return 1; /* shifts, add/sub, immediate ALU: one cycle, none of the tests below can match */
    if((op&0xfc00)==0x4000){n=(op>>6)&15;if(n==13)return 1+multiply_cycles(c->r[(op>>3)&7],1);return ((n>=2&&n<=4)||n==7)?2:1;}
    if((op&0xfc00)==0x4400){n=(op>>8)&3;rd=(op&7)|((op>>4)&8);return n==3||((n==0||n==2)&&rd==15)?3:1;}
    if((op&0xf800)==0x4800)return 3;
    if((op&0xf000)==0x5000){n=(op>>9)&7;return n>=3?3:2;}
    if((op&0xe000)==0x6000||(op&0xf000)==0x8000||(op&0xf000)==0x9000)return op&0x0800?3:2;
    if((op&0xf600)==0xb400){load=!!(op&0x0800);n=register_count(op&255)+!!(op&256);return n+1+load+(load&&!!(op&256)?2:0);}
    if((op&0xf000)==0xc000){n=register_count(op&255);if(!n)n=16;load=!!(op&0x0800);return n+1+load+(load&&!(op&255)?2:0);}
    if((op&0xf000)==0xd000){n=(op>>8)&15;return n==15||cond(c,n)?3:1;}
    if((op&0xf800)==0xe000||(op&0xf800)==0xf800)return 3;
    return 1;
}
int arm7_step(Arm7 *c)
{
    int ok;
    unsigned clocks;
    if (c->halted) return 0;
    c->last_pc = c->r[15]; c->bus->pc = c->last_pc; c->bus->ticks = c->cycles;
    if (c->fiq && !(c->cpsr & 0x40u)) { exception(c, 0x11, 0x1c, c->last_pc + 4, 1); ++c->cycles;c->clock_cycles+=3; return 1; }
    if (c->irq && !(c->cpsr & 0x80u)) { exception(c, 0x12, 0x18, c->last_pc + 4, 0); ++c->cycles;c->clock_cycles+=3; return 1; }
    c->last_opcode = bus_read(c->bus, c->last_pc, (c->cpsr & ARM_T) ? 2 : 4);
    if (c->bus->fault) return fail(c, "instruction fetch/bus fault pending");
    clocks=instruction_cycles(c,c->last_opcode);
    c->r[15] += (c->cpsr & ARM_T) ? 2u : 4u;
    ok = (c->cpsr & ARM_T) ? thumb(c, c->last_opcode) : arm(c, c->last_opcode);
    ++c->instructions; ++c->cycles;c->clock_cycles+=clocks; return ok;
}
