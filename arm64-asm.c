/*************************************************************/
/*
 *  ARM64 (AArch64) assembler for TCC
 *
 *  Based on ARM64 Architecture Reference Manual
 *  Supports AArch64 assembler parsing plus basic inline asm strings.
 *  Extended inline asm with operands or clobbers is not yet implemented.
 */

#ifdef TARGET_DEFS_ONLY

#define CONFIG_TCC_ASM
/* 32 general purpose + 32 SIMD/FP registers */
#define NB_ASM_REGS 64

ST_FUNC void g(int c);
ST_FUNC void gen_le16(int c);
ST_FUNC void gen_le32(int c);

/*************************************************************/
#else
/*************************************************************/

#define USING_GLOBALS
#include "tcc.h"

/* Register type flags */
#define REG_X     0x01  /* 64-bit general purpose */
#define REG_W     0x02  /* 32-bit general purpose */
#define REG_V     0x04  /* 128-bit SIMD */
#define REG_D     0x08  /* 64-bit FP */
#define REG_S     0x10  /* 32-bit FP */
#define REG_H     0x20  /* 16-bit FP */
#define REG_B     0x40  /* 8-bit SIMD */

/* Operand types */
enum {
    OPT_REG,
    OPT_IM,
    OPT_ADDR,
    OPT_COND,
};

#define OP_REG     (1 << OPT_REG)
#define OP_IM      (1 << OPT_IM)
#define OP_ADDR    (1 << OPT_ADDR)
#define OP_COND    (1 << OPT_COND)

typedef struct Operand {
    uint32_t type;
    int8_t reg;
    int8_t reg2;
    uint8_t reg_type;
    uint8_t shift;
    uint8_t addr_mode;
    int reg_tok;
    ExprValue e;
} Operand;

enum {
    ADDR_OFF,
    ADDR_PRE,
    ADDR_POST,
};

/* Forward declaration */
static void parse_addr_operand(TCCState *s1, Operand *op);

/* XXX: make it faster ? */
ST_FUNC void g(int c)
{
    int ind1;
    if (nocode_wanted)
        return;
    ind1 = ind + 1;
    if (ind1 > cur_text_section->data_allocated)
        section_realloc(cur_text_section, ind1);
    cur_text_section->data[ind] = c;
    ind = ind1;
}

ST_FUNC void gen_le16(int c)
{
    g(c);
    g(c >> 8);
}

ST_FUNC void gen_le32(int c)
{
    gen_le16(c);
    gen_le16(c >> 16);
}

ST_FUNC void gen_expr32(ExprValue *pe)
{
    gen_le32(pe->v);
}

/* Emit 32-bit instruction */
static void emit_instr32(uint32_t val)
{
    if (nocode_wanted)
        return;
    if (ind + 4 > cur_text_section->data_allocated)
        section_realloc(cur_text_section, ind + 4);
    write32le(cur_text_section->data + ind, val);
    ind += 4;
}

/* Parse ARM64 register from token */
static int arm64_parse_regvar(int t)
{
    /* X registers (64-bit) */
    if (t >= TOK_ASM_x0 && t <= TOK_ASM_x30)
        return t - TOK_ASM_x0;
    /* W registers (32-bit) */
    if (t >= TOK_ASM_w0 && t <= TOK_ASM_w30)
        return t - TOK_ASM_w0;
    /* V registers (128-bit SIMD) */
    if (t >= TOK_ASM_v0 && t <= TOK_ASM_v31)
        return (t - TOK_ASM_v0) + 32;
    /* D registers (64-bit FP) */
    if (t >= TOK_ASM_d0 && t <= TOK_ASM_d31)
        return (t - TOK_ASM_d0) + 32;
    /* S registers (32-bit FP) */
    if (t >= TOK_ASM_s0 && t <= TOK_ASM_s31)
        return (t - TOK_ASM_s0) + 32;
    /* H registers (16-bit FP) */
    if (t >= TOK_ASM_h0 && t <= TOK_ASM_h31)
        return (t - TOK_ASM_h0) + 32;
    /* B registers (8-bit SIMD) */
    if (t >= TOK_ASM_b0 && t <= TOK_ASM_b31)
        return (t - TOK_ASM_b0) + 32;
    /* Special registers */
    if (t == TOK_ASM_sp || t == TOK_ASM_xzr || t == TOK_ASM_wzr)
        return 31;  /* SP/ZR encoded as 31 */
    return -1;
}

/* Get register type from token */
static uint8_t get_reg_type(int t)
{
    if (t >= TOK_ASM_x0 && t <= TOK_ASM_x30)
        return REG_X;
    if (t >= TOK_ASM_w0 && t <= TOK_ASM_w30)
        return REG_W;
    if (t >= TOK_ASM_v0 && t <= TOK_ASM_v31)
        return REG_V;
    if (t >= TOK_ASM_d0 && t <= TOK_ASM_d31)
        return REG_D;
    if (t >= TOK_ASM_s0 && t <= TOK_ASM_s31)
        return REG_S;
    if (t >= TOK_ASM_h0 && t <= TOK_ASM_h31)
        return REG_H;
    if (t >= TOK_ASM_b0 && t <= TOK_ASM_b31)
        return REG_B;
    /* Special registers - sp is 64-bit, xzr is 64-bit, wzr is 32-bit */
    if (t == TOK_ASM_sp || t == TOK_ASM_xzr)
        return REG_X;
    if (t == TOK_ASM_wzr)
        return REG_W;
    return REG_X;
}

/* Parse condition code */
static int parse_condition(int t)
{
    switch (t) {
        case TOK_ASM_eq: return 0;
        case TOK_ASM_ne: return 1;
        case TOK_ASM_cs:
        case TOK_ASM_hs: return 2;
        case TOK_ASM_cc:
        case TOK_ASM_lo: return 3;
        case TOK_ASM_mi: return 4;
        case TOK_ASM_pl: return 5;
        case TOK_ASM_vs: return 6;
        case TOK_ASM_vc: return 7;
        case TOK_ASM_hi: return 8;
        case TOK_ASM_ls: return 9;
        case TOK_ASM_ge: return 10;
        case TOK_ASM_lt: return 11;
        case TOK_ASM_gt: return 12;
        case TOK_ASM_le: return 13;
        case TOK_ASM_al: return 14;
        default: return -1;
    }
}

static int parse_barrier_option_name(int t)
{
    const char *name;

    if (t < TOK_IDENT)
        return -1;
    name = get_tok_str(t, NULL);
    if (!strcmp(name, "oshld")) return 0x1;
    if (!strcmp(name, "oshst")) return 0x2;
    if (!strcmp(name, "osh"))   return 0x3;
    if (!strcmp(name, "nshld")) return 0x5;
    if (!strcmp(name, "nshst")) return 0x6;
    if (!strcmp(name, "nsh"))   return 0x7;
    if (!strcmp(name, "ishld")) return 0x9;
    if (!strcmp(name, "ishst")) return 0xA;
    if (!strcmp(name, "ish"))   return 0xB;
    if (!strcmp(name, "ld"))    return 0xD;
    if (!strcmp(name, "st"))    return 0xE;
    if (!strcmp(name, "sy"))    return 0xF;
    return -1;
}

/* Parse a single operand */
static void parse_operand(TCCState *s1, Operand *op)
{
    int reg;

    op->type = 0;
    op->reg = -1;
    op->reg2 = -1;
    op->reg_type = 0;
    op->shift = 0;
    op->addr_mode = ADDR_OFF;
    op->reg_tok = 0;

    /* Address operand in brackets [xn, ...] */
    if (tok == '[') {
        parse_addr_operand(s1, op);
        return;
    }

    /* Register */
    reg = arm64_parse_regvar(tok);
    if (reg >= 0) {
        op->type = OP_REG;
        op->reg = reg;
        op->reg_type = get_reg_type(tok);
        op->reg_tok = tok;
        next();
        return;
    }

    /* Condition code */
    reg = parse_condition(tok);
    if (reg >= 0) {
        op->type = OP_COND;
        op->reg = reg;
        next();
        return;
    }

    /* Immediate or address expression */
    if (tok == '#' || tok == ':' || tok == '@' || tok == '$') {
        next();
        asm_expr(s1, &op->e);
        op->type = OP_IM;
    } else if (tok >= TOK_IDENT) {
        tcc_error("invalid operand '%s'", get_tok_str(tok, &tokc));
        op->type = OP_IM;
    } else {
        asm_expr(s1, &op->e);
        op->type = OP_IM;
    }
}

/* Parse a symbolic/immediate expression operand used by branch instructions. */
static void parse_expr_operand(TCCState *s1, Operand *op)
{
    op->type = OP_IM;
    op->reg = -1;
    op->reg2 = -1;
    op->reg_type = 0;
    op->shift = 0;
    op->addr_mode = ADDR_OFF;
    op->reg_tok = 0;

    if (tok == '#' || tok == ':' || tok == '@' || tok == '$')
        next();
    asm_expr(s1, &op->e);
}

/* Parse address operand in brackets [xn, ...] */
static void parse_addr_operand(TCCState *s1, Operand *op)
{
    int reg;

    op->type = OP_ADDR;
    op->reg = -1;
    op->reg2 = -1;
    op->e.v = 0;
    op->e.sym = NULL;
    op->addr_mode = ADDR_OFF;
    op->reg_tok = 0;

    skip('[');
    reg = arm64_parse_regvar(tok);
    if (reg < 0 || reg >= 32) {
        tcc_error("invalid register in address operand");
        return;
    }
    op->reg = reg;
    op->reg_tok = tok;
    next();
    /* Check for offset */
    if (tok == ',') {
        next();
        if (tok == '#' || tok == '@' || tok == '$')
            next();
        asm_expr(s1, &op->e);
    }
    skip(']');
    if (tok == '!') {
        op->addr_mode = ADDR_PRE;
        next();
    } else if (tok == ',') {
        op->addr_mode = ADDR_POST;
        next();
        if (tok == '#' || tok == '@' || tok == '$')
            next();
        asm_expr(s1, &op->e);
    }
}

/* Generate MOVZ instruction */
/* Generate MOVZ/MOVN/MOVK with base opcode */
static void gen_mov_with_base(int rd, uint16_t imm, int shift, 
                              int is_64bit, uint32_t base_opcode)
{
    uint32_t instr = base_opcode;
    if (is_64bit) instr |= (1 << 31);
    /* shift is halfword index (0-3), encode as LSL #0/16/32/48 */
    instr |= ((shift & 3) << 21) & 0x00600000;
    instr |= (imm << 5) & 0x00FFFFE0;
    instr |= rd & 0x1F;
    emit_instr32(instr);
}

static void gen_movz(int rd, uint16_t imm, int shift, int is_64bit)
{
    gen_mov_with_base(rd, imm, shift, is_64bit, 0x52800000);
}

/* Generate MOVN instruction */
static void gen_movn(int rd, uint16_t imm, int shift, int is_64bit)
{
    gen_mov_with_base(rd, imm, shift, is_64bit, 0x12800000);
}

/* Generate MOVK instruction */
static void gen_movk(int rd, uint16_t imm, int shift, int is_64bit)
{
    gen_mov_with_base(rd, imm, shift, is_64bit, 0xF2800000);
}

/* Generate ADD (immediate) */
static void gen_add_imm(int rd, int rn, uint32_t imm, int is_64bit, int setflags)
{
    uint32_t instr = 0x11000000;
    uint32_t imm12;

    if (is_64bit) instr |= (1 << 31);
    if (setflags) instr |= (1 << 29);

    if (imm <= 0xFFF) {
        imm12 = imm;
    } else if (!(imm & 0xFFF) && (imm >> 12) <= 0xFFF) {
        instr |= 1 << 22;
        imm12 = imm >> 12;
    } else {
        tcc_error("add immediate out of range");
        return;
    }

    instr |= imm12 << 10;
    instr |= (rn & 0x1F) << 5;
    instr |= rd & 0x1F;
    emit_instr32(instr);
}

/* Generate SUB (immediate) */
static void gen_sub_imm(int rd, int rn, uint32_t imm, int is_64bit, int setflags)
{
    uint32_t instr = 0x51000000;
    uint32_t imm12;

    if (is_64bit) instr |= (1 << 31);
    if (setflags) instr |= (1 << 29);

    if (imm <= 0xFFF) {
        imm12 = imm;
    } else if (!(imm & 0xFFF) && (imm >> 12) <= 0xFFF) {
        instr |= 1 << 22;
        imm12 = imm >> 12;
    } else {
        tcc_error("sub immediate out of range");
        return;
    }

    instr |= imm12 << 10;
    instr |= (rn & 0x1F) << 5;
    instr |= rd & 0x1F;
    emit_instr32(instr);
}

/* Generate data processing register instruction */
static void gen_dp_reg(uint32_t opcode, int rd, int rn, int rm, int is_64bit)
{
    uint32_t instr = opcode;
    if (is_64bit) instr |= (1 << 31);
    instr |= (rm & 0x1F) << 16;
    instr |= (rn & 0x1F) << 5;
    instr |= rd & 0x1F;
    emit_instr32(instr);
}

/* Generate LDR/STR (unsigned immediate) */
static void gen_ldst_imm(uint32_t base_opcode, int rt, int rn,
                         int32_t offset, int size_log2)
{
    uint32_t instr = base_opcode;
    uint32_t imm12;

    if (offset < 0 || (offset & ((1 << size_log2) - 1)))
        tcc_error("invalid load/store offset");
    imm12 = offset >> size_log2;
    if (imm12 > 0xFFF)
        tcc_error("load/store offset out of range");
    instr |= (imm12 & 0xFFF) << 10;
    instr |= (rn & 0x1F) << 5;
    instr |= rt & 0x1F;
    emit_instr32(instr);
}

/* Generate STP/LDP (signed immediate) */
static void gen_ldst_pair(uint32_t base_opcode, int rt, int rt2, int rn,
                          int32_t offset, int size_log2)
{
    int32_t imm7;
    uint32_t instr = base_opcode;

    if (offset & ((1 << size_log2) - 1))
        tcc_error("invalid pair load/store offset");
    imm7 = offset >> size_log2;
    if (imm7 < -64 || imm7 > 63)
        tcc_error("pair load/store offset out of range");

    instr |= (imm7 & 0x7F) << 15;
    instr |= (rt2 & 0x1F) << 10;
    instr |= (rn & 0x1F) << 5;
    instr |= rt & 0x1F;
    emit_instr32(instr);
}

/* Generate B (branch) */
/* Generate B/BL with base opcode */
static void gen_b_or_bl(int32_t offset, uint32_t base_opcode)
{
    uint32_t instr = base_opcode;
    instr |= ((offset >> 2) & 0x03FFFFFF);
    emit_instr32(instr);
}

static void gen_b(int32_t offset)
{
    gen_b_or_bl(offset, 0x14000000);
}

/* Generate BL (branch with link) */
static void gen_bl(int32_t offset)
{
    gen_b_or_bl(offset, 0x94000000);
}

/* Generate BR (branch to register) */
static void gen_br(int rn)
{
    uint32_t instr = 0xD61F0000;
    instr |= (rn & 0x1F) << 5;
    emit_instr32(instr);
}

/* Generate BLR (branch with link to register) */
static void gen_blr(int rn)
{
    uint32_t instr = 0xD63F0000;
    instr |= (rn & 0x1F) << 5;
    emit_instr32(instr);
}

/* Generate RET */
static void gen_ret(int rn)
{
    uint32_t instr = 0xD65F03C0;
    instr |= (rn & 0x1F) << 5;
    emit_instr32(instr);
}

/* Generate conditional branch */
static void gen_b_cond(int cond, int32_t offset)
{
    uint32_t instr = 0x54000000;
    instr |= ((offset >> 2) & 0x7FFFF) << 5;
    instr |= cond & 0xF;
    emit_instr32(instr);
}

/* Generate CBZ */
/* Generate CBZ/CBNZ with base opcode */
static void gen_cbz_or_cbnz(int rt, int32_t offset, int is_64bit, uint32_t base_opcode)
{
    uint32_t instr = base_opcode;
    if (is_64bit) instr |= (1 << 31);
    instr |= ((offset >> 2) & 0x7FFFF) << 5;
    instr |= rt & 0x1F;
    emit_instr32(instr);
}

static void gen_cbz(int rt, int32_t offset, int is_64bit)
{
    gen_cbz_or_cbnz(rt, offset, is_64bit, 0x34000000);
}

/* Generate CBNZ */
static void gen_cbnz(int rt, int32_t offset, int is_64bit)
{
    gen_cbz_or_cbnz(rt, offset, is_64bit, 0x35000000);
}

/* Generate MOV (register) - ORR with zero register */
static void gen_mov_reg(int rd, int rm, int is_64bit)
{
    uint32_t instr = 0x2A0003E0;
    if (is_64bit) instr |= (1 << 31);
    instr |= (rm & 0x1F) << 16;
    instr |= rd & 0x1F;
    emit_instr32(instr);
}

static int operand_is_sp(const Operand *op)
{
    return op->reg_tok == TOK_ASM_sp;
}

static int parse_sysreg_name(int t)
{
    const char *name;

    if (t < TOK_IDENT)
        return -1;
    name = get_tok_str(t, NULL);
    if (!strcmp(name, "FPCR") || !strcmp(name, "fpcr"))
        return 0;
    if (!strcmp(name, "FPSR") || !strcmp(name, "fpsr"))
        return 1;
    return -1;
}

static void gen_mrs(int rt, int sysreg)
{
    uint32_t instr;

    switch (sysreg) {
        case 0:
            instr = 0xD53B4400;
            break;
        case 1:
            instr = 0xD53B4420;
            break;
        default:
            tcc_error("unsupported system register");
            return;
    }
    emit_instr32(instr | (rt & 0x1F));
}

static void gen_msr(int rt, int sysreg)
{
    uint32_t instr;

    switch (sysreg) {
        case 0:
            instr = 0xD51B4400;
            break;
        case 1:
            instr = 0xD51B4420;
            break;
        default:
            tcc_error("unsupported system register");
            return;
    }
    emit_instr32(instr | (rt & 0x1F));
}

/* Generate NOP */
static void gen_nop(void)
{
    emit_instr32(0xD503201F);
}

/* Generate shift operations (LSL, LSR, ASR, ROR) */
static void gen_shift(int rd, int rn, int rm_or_imm, int shift_type, int is_imm, int is_64bit)
{
    uint32_t instr;
    int width = is_64bit ? 64 : 32;

    if (is_imm) {
        /* Shift by immediate */
        switch (shift_type) {
            case 0: /* LSL - UBFM alias: immr = (width - shift) & 0x3F, imms = width - 1 */
                if (rm_or_imm < 0 || rm_or_imm >= width) {
                    tcc_error("shift immediate out of range");
                    return;
                }
                instr = is_64bit ? 0xD3400000 : 0x53000000;
                instr |= ((width - rm_or_imm) & 0x3F) << 16;  /* immr */
                instr |= (width - 1) << 10;                   /* imms */
                break;
            case 1: /* LSR - UBFM alias: immr = shift, imms = width - 1 */
                if (rm_or_imm < 0 || rm_or_imm >= width) {
                    tcc_error("shift immediate out of range");
                    return;
                }
                instr = is_64bit ? 0xD3400000 : 0x53000000;
                instr |= (rm_or_imm & 0x3F) << 16;            /* immr */
                instr |= (width - 1) << 10;                   /* imms */
                break;
            case 2: /* ASR - SBFM alias: immr = shift, imms = width - 1 */
                if (rm_or_imm < 0 || rm_or_imm >= width) {
                    tcc_error("shift immediate out of range");
                    return;
                }
                instr = is_64bit ? 0x93400000 : 0x13000000;
                instr |= (rm_or_imm & 0x3F) << 16;            /* immr */
                instr |= (width - 1) << 10;                   /* imms */
                break;
            case 3: /* ROR - EXTR alias: Rm = shift, Rn = source, Rd = dest */
                if (rm_or_imm < 0 || rm_or_imm >= width) {
                    tcc_error("shift immediate out of range");
                    return;
                }
                instr = is_64bit ? 0x93C00000 : 0x13800000;
                instr |= (rm_or_imm & 0x1F) << 16;            /* Rm = shift amount */
                instr |= (rn & 0x1F) << 5;                    /* Rn = source */
                instr |= rd & 0x1F;                           /* Rd = dest */
                emit_instr32(instr);
                return;
            default:
                tcc_error("unknown shift type");
                return;
        }
        instr |= (rn & 0x1F) << 5;
        instr |= rd & 0x1F;
    } else {
        /* Shift by register */
        switch (shift_type) {
            case 0: /* LSL */
                instr = 0x1AC02000;
                break;
            case 1: /* LSR */
                instr = 0x1AC02400;
                break;
            case 2: /* ASR */
                instr = 0x1AC02800;
                break;
            case 3: /* ROR */
                instr = 0x1AC02C00;
                break;
            default:
                tcc_error("unknown shift type");
                return;
        }
        if (is_64bit)
            instr |= 1U << 31;
        instr |= (rm_or_imm & 0x1F) << 16;
        instr |= (rn & 0x1F) << 5;
        instr |= rd & 0x1F;
    }
    emit_instr32(instr);
}

/* Handle shift instructions */
static void asm_shift(TCCState *s1, int token)
{
    Operand op1, op2, op3;
    int rd, rn, shift_amount;
    int shift_type;
    int is_64bit = 1;

    switch (token) {
        case TOK_ASM_lsl:
            shift_type = 0;
            break;
        case TOK_ASM_lsr:
            shift_type = 1;
            break;
        case TOK_ASM_asr:
            shift_type = 2;
            break;
        case TOK_ASM_ror:
            shift_type = 3;
            break;
        default:
            tcc_error("unknown shift instruction");
            return;
    }

    parse_operand(s1, &op1);
    if (tok == ',') next();
    parse_operand(s1, &op2);

    if (!(op1.type & OP_REG)) {
        tcc_error("expected register in first operand");
        return;
    }
    if (!(op2.type & OP_REG)) {
        tcc_error("expected register in second operand");
        return;
    }

    rd = op1.reg;
    rn = op2.reg;

    if (tok == ',') {
        next();
        /* Parse shift immediate - skip # prefix like arm-asm.c does */
        if (tok == '#' || tok == '$')
            next();
        parse_operand(s1, &op3);
        shift_amount = op3.e.v;
        is_64bit = (op1.reg_type & REG_X);
        gen_shift(rd, rn, shift_amount, shift_type, 1, is_64bit);
    } else {
        tcc_error("shift requires immediate or register operand");
        return;
    }
}

/* Generate barrier instructions (ISB, DSB, DMB) */
static void gen_barrier(int barrier_type, int option)
{
    uint32_t instr;

    switch (barrier_type) {
        case 0: /* ISB - Instruction Synchronization Barrier */
            instr = 0xD50330DF;
            break;
        case 1: /* DSB - Data Synchronization Barrier */
            instr = 0xD503309F;
            break;
        case 2: /* DMB - Data Memory Barrier */
            instr = 0xD50330BF;
            break;
        default:
            tcc_error("unknown barrier type");
            return;
    }
    instr |= (option & 0xF) << 8;
    emit_instr32(instr);
}

/* Handle barrier instructions */
static void asm_barrier(TCCState *s1, int token)
{
    int barrier_type, option;
    Operand op;

    switch (token) {
        case TOK_ASM_isb:
            barrier_type = 0;
            break;
        case TOK_ASM_dsb:
            barrier_type = 1;
            break;
        case TOK_ASM_dmb:
            barrier_type = 2;
            break;
        default:
            tcc_error("unknown barrier instruction");
            return;
    }

    /* Default option = sy/full system. */
    option = 0xF;

    /* Check for an optional named or numeric barrier scope. */
    if (tok != TOK_LINEFEED) {
        option = parse_barrier_option_name(tok);
        if (option >= 0) {
            next();
        } else {
            parse_operand(s1, &op);
            if (!(op.type & OP_IM) || op.e.sym) {
                tcc_error("barrier option must be an immediate or scope name");
                return;
            }
            if (op.e.v > 0xF) {
                tcc_error("barrier option out of range");
                return;
            }
            option = op.e.v;
        }
    }

    gen_barrier(barrier_type, option);
}

/* Generate immediate move sequence */
static void gen_mov_imm(int rd, uint64_t imm, int is_64bit)
{
    uint16_t hw;
    int i, first = 1;

    for (i = 0; i < (is_64bit ? 4 : 2); i++) {
        hw = (imm >> (i * 16)) & 0xFFFF;
        if (hw != 0 || i == 0) {
            if (first) {
                /* Pass halfword index (0-3), not bit count */
                gen_movz(rd, hw, i, is_64bit);
                first = 0;
            } else {
                gen_movk(rd, hw, i, is_64bit);
            }
        } else if (!first) {
            gen_movk(rd, hw, i, is_64bit);
        }
    }
}

/* Handle mov instruction */
static void asm_mov(TCCState *s1)
{
    Operand op1, op2;
    int rd, rn;
    int is_64bit;

    parse_operand(s1, &op1);
    if (tok == ',') next();
    parse_operand(s1, &op2);
    rd = op1.reg;
    is_64bit = (op1.reg_type & REG_X);

    if (op2.type & OP_IM) {
        /* Handle immediate: mov x0, #123 */
        if (operand_is_sp(&op1)) {
            tcc_error("cannot move an immediate into sp");
            return;
        }
        gen_mov_imm(rd, op2.e.v, is_64bit);
    } else if (op2.type & OP_REG) {
        /* Handle register: mov x0, x1 */
        rn = op2.reg;
        if (operand_is_sp(&op1) || operand_is_sp(&op2))
            gen_add_imm(rd, rn, 0, 1, 0);
        else
            gen_mov_reg(rd, rn, is_64bit);
    } else {
        tcc_error("invalid operand for mov");
    }
}

/* Handle data processing instructions */
static void asm_data_proc(TCCState *s1, int token)
{
    Operand op1, op2, op3;
    int rd, rn, rm;
    int is_64bit = 1;
    uint32_t opcode;

    switch (token) {
        case TOK_ASM_add:
        case TOK_ASM_adds:
            opcode = token == TOK_ASM_add ? 0x0B000000 : 0x2B000000;
            break;
        case TOK_ASM_sub:
        case TOK_ASM_subs:
            opcode = token == TOK_ASM_sub ? 0x4B000000 : 0x6B000000;
            break;
        case TOK_ASM_and:
        case TOK_ASM_ands:
            opcode = token == TOK_ASM_and ? 0x0A000000 : 0x2A000000;
            break;
        case TOK_ASM_orr:
            opcode = 0x2A000000;
            break;
        case TOK_ASM_eor:
            opcode = 0x4A000000;
            break;
        case TOK_ASM_mul:
        case TOK_ASM_muls:
            opcode = 0x1B007C00;
            break;
        default:
            tcc_error("unsupported data processing instruction");
            return;
    }

    parse_operand(s1, &op1);
    if (tok == ',') next();
    parse_operand(s1, &op2);

    if (!(op1.type & OP_REG)) {
        tcc_error("expected register in first operand");
        return;
    }
    if (!(op2.type & OP_REG)) {
        tcc_error("expected register in second operand");
        return;
    }

    rd = op1.reg;
    rn = op2.reg;

    if (tok == ',') {
        next();
        parse_operand(s1, &op3);
        if (op3.type & OP_IM) {
            is_64bit = (op1.reg_type & REG_X);
            if (token == TOK_ASM_add || token == TOK_ASM_adds)
                gen_add_imm(rd, rn, op3.e.v, is_64bit,
                            token == TOK_ASM_adds);
            else if (token == TOK_ASM_sub || token == TOK_ASM_subs)
                gen_sub_imm(rd, rn, op3.e.v, is_64bit,
                            token == TOK_ASM_subs);
            else
                tcc_error("immediate operand not valid for this instruction");
        } else {
            if (!(op3.type & OP_REG)) {
                tcc_error("expected register in third operand");
                return;
            }
            rm = op3.reg;
            is_64bit = (op1.reg_type & REG_X);
            if (is_64bit != !!(op2.reg_type & REG_X) || is_64bit != !!(op3.reg_type & REG_X))
                tcc_error("mismatched register widths");
            gen_dp_reg(opcode, rd, rn, rm, is_64bit);
        }
    } else if (op2.type & OP_IM) {
        tcc_error("missing source register for immediate form");
    } else {
        is_64bit = (op1.reg_type & REG_X);
        gen_mov_reg(rd, rn, is_64bit);
    }
}

/* Handle load/store instructions */
static void asm_ldst(TCCState *s1, int token)
{
    Operand op1, op2;
    int rt, rn;
    int32_t offset = 0;
    int size_log2 = 3;
    uint32_t base_opcode;

    parse_operand(s1, &op1);
    if (tok == ',') next();
    parse_operand(s1, &op2);

    if (!(op1.type & OP_REG)) {
        tcc_error("expected register in first operand");
        return;
    }
    if (op2.type != OP_ADDR) {
        tcc_error("expected address operand in second operand");
        return;
    }

    rt = op1.reg;
    rn = op2.reg;
    offset = op2.e.v;

    switch (token) {
        case TOK_ASM_ldr:
            if (op1.reg_type & REG_X) {
                base_opcode = 0xF9400000;
                size_log2 = 3;
            } else if (op1.reg_type & REG_W) {
                base_opcode = 0xB9400000;
                size_log2 = 2;
            } else if (op1.reg_type & REG_D) {
                base_opcode = 0xFD400000;
                size_log2 = 3;
            } else {
                tcc_error("ldr requires a w, x, or d register");
                return;
            }
            break;
        case TOK_ASM_ldrb:
            base_opcode = 0x39400000;
            size_log2 = 0;
            break;
        case TOK_ASM_ldrh:
            base_opcode = 0x79400000;
            size_log2 = 1;
            break;
        case TOK_ASM_str:
            if (op1.reg_type & REG_X) {
                base_opcode = 0xF9000000;
                size_log2 = 3;
            } else if (op1.reg_type & REG_W) {
                base_opcode = 0xB9000000;
                size_log2 = 2;
            } else if (op1.reg_type & REG_D) {
                base_opcode = 0xFD000000;
                size_log2 = 3;
            } else {
                tcc_error("str requires a w, x, or d register");
                return;
            }
            break;
        case TOK_ASM_strb:
            base_opcode = 0x39000000;
            size_log2 = 0;
            break;
        case TOK_ASM_strh:
            base_opcode = 0x79000000;
            size_log2 = 1;
            break;
        default:
            tcc_error("unsupported load/store instruction");
            return;
    }

    if (op2.addr_mode != ADDR_OFF)
        tcc_error("only offset addressing is implemented for ldr/str");
    gen_ldst_imm(base_opcode, rt, rn, offset, size_log2);
}

static void asm_ldst_pair(TCCState *s1, int token)
{
    Operand op1, op2, op3;
    uint32_t base_opcode;
    int size_log2 = 3;

    parse_operand(s1, &op1);
    if (tok == ',')
        next();
    parse_operand(s1, &op2);
    if (tok == ',')
        next();
    parse_operand(s1, &op3);

    if (!(op1.type & OP_REG)) {
        tcc_error("expected register in first operand");
        return;
    }
    if (!(op2.type & OP_REG)) {
        tcc_error("expected register in second operand");
        return;
    }
    if (!(op3.type & OP_ADDR))
        tcc_error("pair load/store requires an address operand");

    if ((op1.reg_type & REG_X) && (op2.reg_type & REG_X)) {
        if (token == TOK_ASM_stp) {
            base_opcode = op3.addr_mode == ADDR_PRE ? 0xA9800000 :
                          op3.addr_mode == ADDR_POST ? 0xA8800000 :
                          0xA9000000;
        } else {
            base_opcode = op3.addr_mode == ADDR_PRE ? 0xA9C00000 :
                          op3.addr_mode == ADDR_POST ? 0xA8C00000 :
                          0xA9400000;
        }
    } else if ((op1.reg_type & REG_D) && (op2.reg_type & REG_D)) {
        if (token == TOK_ASM_stp) {
            base_opcode = op3.addr_mode == ADDR_PRE ? 0x6D800000 :
                          op3.addr_mode == ADDR_POST ? 0x6C800000 :
                          0x6D000000;
        } else {
            base_opcode = op3.addr_mode == ADDR_PRE ? 0x6DC00000 :
                          op3.addr_mode == ADDR_POST ? 0x6CC00000 :
                          0x6D400000;
        }
    } else {
        tcc_error("stp/ldp requires matching x or d registers");
        return;
    }

    gen_ldst_pair(base_opcode, op1.reg, op2.reg, op3.reg, op3.e.v, size_log2);
}

static void asm_sysreg(TCCState *s1, int token)
{
    Operand op;
    int sysreg;

    if (token == TOK_ASM_mrs) {
        parse_operand(s1, &op);
        if (tok == ',')
            next();
        sysreg = parse_sysreg_name(tok);
        if (sysreg < 0)
            tcc_error("unsupported system register");
        next();
        gen_mrs(op.reg, sysreg);
        return;
    }

    sysreg = parse_sysreg_name(tok);
    if (sysreg < 0)
        tcc_error("unsupported system register");
    next();
    if (tok == ',')
        next();
    parse_operand(s1, &op);
    gen_msr(op.reg, sysreg);
}

/* Get condition code from branch instruction token */
static int get_branch_condition(int branch_token)
{
    int cond_token;
    
    /* Map branch token to condition token (strip 'b' prefix) */
    switch (branch_token) {
        case TOK_ASM_beq: cond_token = TOK_ASM_eq; break;
        case TOK_ASM_bne: cond_token = TOK_ASM_ne; break;
        case TOK_ASM_bcs:
        case TOK_ASM_bhs: cond_token = TOK_ASM_cs; break;
        case TOK_ASM_bcc:
        case TOK_ASM_blo: cond_token = TOK_ASM_cc; break;
        case TOK_ASM_bmi: cond_token = TOK_ASM_mi; break;
        case TOK_ASM_bpl: cond_token = TOK_ASM_pl; break;
        case TOK_ASM_bvs: cond_token = TOK_ASM_vs; break;
        case TOK_ASM_bvc: cond_token = TOK_ASM_vc; break;
        case TOK_ASM_bhi: cond_token = TOK_ASM_hi; break;
        case TOK_ASM_bls: cond_token = TOK_ASM_ls; break;
        case TOK_ASM_bge: cond_token = TOK_ASM_ge; break;
        case TOK_ASM_blt: cond_token = TOK_ASM_lt; break;
        case TOK_ASM_bgt: cond_token = TOK_ASM_gt; break;
        case TOK_ASM_ble: cond_token = TOK_ASM_le; break;
        default: return -1;
    }
    
    return parse_condition(cond_token);
}

/* Handle branch instructions */
static void asm_branch(TCCState *s1, int token)
{
    Operand op;
    int cond;
    Sym *sym;
    int32_t offset;

    /* ret can be used without operand */
    if (token == TOK_ASM_ret && (tok == TOK_LINEFEED || tok == ';' || tok == TOK_EOF)) {
        gen_ret(30);  /* x30 is the link register */
        return;
    }

    if (token == TOK_ASM_br || token == TOK_ASM_blr || token == TOK_ASM_ret)
        parse_operand(s1, &op);
    else
        parse_expr_operand(s1, &op);

    if (op.type & OP_IM) {
        sym = op.e.sym;
        if (sym) {
            /* Symbolic address - emit relocation */
            offset = 0;

            cond = get_branch_condition(token);
            if (cond >= 0) {
                /* Conditional branch - use CONDBR19 relocation */
                gen_b_cond(cond, 0);
                greloca(cur_text_section, sym, ind - 4, R_AARCH64_CONDBR19, 0);
            } else {
                switch (token) {
                    case TOK_ASM_b:
                        gen_b(0);
                        greloca(cur_text_section, sym, ind - 4, R_AARCH64_JUMP26, 0);
                        break;
                    case TOK_ASM_bl:
                        gen_bl(0);
                        greloca(cur_text_section, sym, ind - 4, R_AARCH64_CALL26, 0);
                        break;
                    default:
                        tcc_error("unsupported branch");
                }
            }
        } else {
            offset = (int32_t)op.e.v - ind;

            cond = get_branch_condition(token);
            if (cond >= 0) {
                gen_b_cond(cond, offset);
            } else {
                switch (token) {
                    case TOK_ASM_b:
                        gen_b(offset);
                        break;
                    case TOK_ASM_bl:
                        gen_bl(offset);
                        break;
                    default:
                        tcc_error("unsupported branch");
                }
            }
        }
    } else if (op.type & OP_REG) {
        switch (token) {
            case TOK_ASM_br:
                gen_br(op.reg);
                break;
            case TOK_ASM_blr:
                gen_blr(op.reg);
                break;
            case TOK_ASM_ret:
                gen_ret(op.reg);
                break;
            default:
                tcc_error("register branch not valid");
        }
    }
}

/* Handle CBZ/CBNZ */
static void asm_cb(TCCState *s1, int token)
{
    Operand op1, op2;
    int rt, is_64bit;
    int32_t offset;
    Sym *sym;

    parse_operand(s1, &op1);
    if (tok == ',') next();
    parse_expr_operand(s1, &op2);

    rt = op1.reg;
    is_64bit = (op1.reg_type & REG_X);
    sym = op2.e.sym;

    if (sym) {
        /* Symbolic address - emit relocation */
        offset = 0;
        if (token == TOK_ASM_cbz) {
            gen_cbz(rt, offset, is_64bit);
            greloca(cur_text_section, sym, ind - 4, R_AARCH64_CONDBR19, 0);
        } else {
            gen_cbnz(rt, offset, is_64bit);
            greloca(cur_text_section, sym, ind - 4, R_AARCH64_CONDBR19, 0);
        }
    } else {
        offset = (int32_t)op2.e.v - ind;
        if (token == TOK_ASM_cbz)
            gen_cbz(rt, offset, is_64bit);
        else
            gen_cbnz(rt, offset, is_64bit);
    }
}

/* Handle MOVZ/MOVN/MOVK */
static void asm_move_wide(TCCState *s1, int token)
{
    Operand op1, op2;
    int rd, is_64bit = 1;
    uint16_t imm;
    int shift = 0;

    parse_operand(s1, &op1);
    if (tok == ',') next();
    parse_operand(s1, &op2);

    rd = op1.reg;
    is_64bit = (op1.reg_type & REG_X);
    imm = op2.e.v & 0xFFFF;

    if (tok == ',') {
        next();
        if (tok == TOK_ASM_lsl) {
            next();
            if (tok == '#') next();
            asm_expr(s1, &op2.e);
            shift = (int)op2.e.v / 16;
        }
    }

    switch (token) {
        case TOK_ASM_movz:
            gen_movz(rd, imm, shift, is_64bit);
            break;
        case TOK_ASM_movn:
            gen_movn(rd, imm, shift, is_64bit);
            break;
        case TOK_ASM_movk:
            gen_movk(rd, imm, shift, is_64bit);
            break;
        default:
            tcc_error("unknown move wide instruction");
    }
}

/* Main assembler opcode dispatcher */
ST_FUNC void asm_opcode(TCCState *s1, int opcode)
{
    switch (opcode) {
        case TOK_ASM_add:
        case TOK_ASM_adds:
        case TOK_ASM_sub:
        case TOK_ASM_subs:
        case TOK_ASM_and:
        case TOK_ASM_ands:
        case TOK_ASM_orr:
        case TOK_ASM_eor:
        case TOK_ASM_mul:
        case TOK_ASM_muls:
            asm_data_proc(s1, opcode);
            break;

        case TOK_ASM_mov:
            /* mov is handled separately - it's ORR with zero register */
            asm_mov(s1);
            break;

        case TOK_ASM_lsl:
        case TOK_ASM_lsr:
        case TOK_ASM_asr:
        case TOK_ASM_ror:
            asm_shift(s1, opcode);
            break;

        case TOK_ASM_ldr:
        case TOK_ASM_ldrb:
        case TOK_ASM_ldrh:
        case TOK_ASM_str:
        case TOK_ASM_strb:
        case TOK_ASM_strh:
            asm_ldst(s1, opcode);
            break;

        case TOK_ASM_ldp:
        case TOK_ASM_stp:
            asm_ldst_pair(s1, opcode);
            break;

        case TOK_ASM_b:
        case TOK_ASM_bl:
        case TOK_ASM_br:
        case TOK_ASM_blr:
        case TOK_ASM_ret:
        case TOK_ASM_beq:
        case TOK_ASM_bne:
        case TOK_ASM_bcs:
        case TOK_ASM_bhs:
        case TOK_ASM_bcc:
        case TOK_ASM_blo:
        case TOK_ASM_bmi:
        case TOK_ASM_bpl:
        case TOK_ASM_bvs:
        case TOK_ASM_bvc:
        case TOK_ASM_bhi:
        case TOK_ASM_bls:
        case TOK_ASM_bge:
        case TOK_ASM_blt:
        case TOK_ASM_bgt:
        case TOK_ASM_ble:
            asm_branch(s1, opcode);
            break;

        case TOK_ASM_cbz:
        case TOK_ASM_cbnz:
            asm_cb(s1, opcode);
            break;

        case TOK_ASM_movz:
        case TOK_ASM_movn:
        case TOK_ASM_movk:
            asm_move_wide(s1, opcode);
            break;

        case TOK_ASM_mrs:
        case TOK_ASM_msr:
            asm_sysreg(s1, opcode);
            break;

        case TOK_ASM_isb:
        case TOK_ASM_dsb:
        case TOK_ASM_dmb:
            asm_barrier(s1, opcode);
            break;

        case TOK_ASM_nop:
            gen_nop();
            break;

        default:
            tcc_error("ARM64 instruction '%s' not implemented",
                     get_tok_str(opcode, NULL));
            break;
    }
}

/* Substitute assembler operand */
ST_FUNC void subst_asm_operand(CString *add_str, SValue *sv, int modifier)
{
    int r, reg, size, val;

    r = sv->r;
    if ((r & VT_VALMASK) == VT_CONST) {
        if (!(r & VT_LVAL) && modifier != 'c' && modifier != 'n' &&
            modifier != 'P')
            cstr_ccat(add_str, '#');
        if (r & VT_SYM) {
            const char *name = get_tok_str(sv->sym->v, NULL);
            if (sv->sym->v >= SYM_FIRST_ANOM) {
                get_asm_sym(tok_alloc(name, strlen(name))->tok, sv->sym);
            }
            if (tcc_state->leading_underscore)
                cstr_ccat(add_str, '_');
            cstr_cat(add_str, name, -1);
            if ((uint32_t) sv->c.i == 0)
                goto no_offset;
            cstr_ccat(add_str, '+');
        }
        val = sv->c.i;
        if (modifier == 'n')
            val = -val;
        cstr_printf(add_str, "%d", (int) sv->c.i);
      no_offset:;
    } else if ((r & VT_VALMASK) == VT_LOCAL) {
        cstr_printf(add_str, "[x29,#%d]", (int) sv->c.i);
    } else if (r & VT_LVAL) {
        reg = r & VT_VALMASK;
        if (reg >= VT_CONST)
            tcc_internal_error("");
        cstr_printf(add_str, "[x%d]", reg);
    } else {
        /* register case */
        reg = r & VT_VALMASK;
        if (reg >= VT_CONST)
            tcc_internal_error("");

        /* choose register operand size */
        if ((sv->type.t & VT_BTYPE) == VT_BYTE ||
            (sv->type.t & VT_BTYPE) == VT_BOOL)
            size = 1;
        else if ((sv->type.t & VT_BTYPE) == VT_SHORT)
            size = 2;
        else
            size = 8;

        if (modifier == 'b') {
            size = 1;
        } else if (modifier == 'w') {
            size = 2;
        } else if (modifier == 'k') {
            size = 4;
        } else if (modifier == 'q') {
            size = 8;
        }

        if (size <= 4) {
            cstr_printf(add_str, "w%d", reg);
        } else {
            cstr_printf(add_str, "x%d", reg);
        }
    }
}

static int asm_has_clobbers(const uint8_t *clobber_regs)
{
    int i;
    for (i = 0; i < NB_ASM_REGS; ++i)
        if (clobber_regs[i])
            return 1;
    return 0;
}

/* Basic inline asm strings are assembled directly by tccasm.c.
   asm goto labels also work through operand substitution there.
   Operand allocation and clobber handling are still unsupported here. */
ST_FUNC void asm_gen_code(ASMOperand *operands, int nb_operands,
                          int nb_outputs, int is_output,
                          uint8_t *clobber_regs,
                          int out_reg)
{
    (void)operands;
    (void)nb_outputs;
    (void)is_output;

    if (nb_operands > 0 || asm_has_clobbers(clobber_regs) || out_reg >= 0)
        tcc_error("ARM64 extended inline asm is not implemented");
}

ST_FUNC void asm_compute_constraints(ASMOperand *operands,
                                     int nb_operands, int nb_outputs,
                                     const uint8_t *clobber_regs,
                                     int *pout_reg)
{
    (void)operands;
    (void)nb_outputs;

    if (pout_reg)
        *pout_reg = -1;
    if (nb_operands > 0 || asm_has_clobbers(clobber_regs))
        tcc_error("ARM64 extended inline asm is not implemented");
}

/* Handle clobber list */
ST_FUNC void asm_clobber(uint8_t *clobber_regs, const char *str)
{
    int reg;
    TokenSym *ts;

    if (!strcmp(str, "memory") || !strcmp(str, "cc") || !strcmp(str, "flags"))
        return;
    ts = tok_alloc(str, strlen(str));
    reg = arm64_parse_regvar(ts->tok);
    if (reg == -1) {
        tcc_error("invalid clobber register '%s'", str);
    }
    clobber_regs[reg] = 1;
}

/* Parse register variable - this is the ST_FUNC that tcc.h expects */
ST_FUNC int asm_parse_regvar(int t)
{
    return arm64_parse_regvar(t);
}

/*************************************************************/
#endif /* ndef TARGET_DEFS_ONLY */
