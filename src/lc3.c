#include <stdio.h>
#include <stdint.h>
#include <signal.h>
/* unix only */
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/termios.h>
#include <sys/mman.h>

#define MEM_MAX (1 << 16)

enum
{
    R_R0 = 0,
    R_R1,
    R_R2,
    R_R3,
    R_R4,
    R_R5,
    R_R6,
    R_R7,
    R_PC,
    R_COND,
    R_COUNT
};

enum
{
    FL_POS = 1 << 0, /* P */
    FL_ZRO = 1 << 1, /* Z */
    FL_NEG = 1 << 2, /* N */
};

enum
{
    OP_BR = 0, /* branch */
    OP_ADD,    /* add  */
    OP_LD,     /* load */
    OP_ST,     /* store */
    OP_JSR,    /* jump register */
    OP_AND,    /* bitwise and */
    OP_LDR,    /* load register */
    OP_STR,    /* store register */
    OP_RTI,    /* unused */
    OP_NOT,    /* bitwise not */
    OP_LDI,    /* load indirect */
    OP_STI,    /* store indirect */
    OP_JMP,    /* jump */
    OP_RES,    /* reserved (unused) */
    OP_LEA,    /* load effective address */
    OP_TRAP    /* execute trap */
};

uint16_t memory[MEM_MAX];
uint16_t reg[R_COUNT];

/* HELPER */

// puts everything on 16 bits and keeps the sign
uint16_t sign_extend(uint16_t x, int bit_count) {

    if ((x >> (bit_count - 1)) & 1) {
        x |= (0xFFFF << bit_count);
    }

    return x;
}

// update condition flag to show last updated register's sign
void update_flags(uint16_t r) {
    if (reg[r] == 0) {
        reg[R_COND] = FL_ZRO;

    } else if (reg[r] >> 15) { // a 1 in the left-most bit indicates negative
        reg[R_COND] = FL_NEG;

    } else {
        reg[R_COND] = FL_POS;
    }
}


/* OPERATIONS */

void branch(uint16_t instr) {
    uint16_t pc_offset = sign_extend(instr & 0x1ff, 9);
    uint16_t cond_flag = (instr >> 9) & 0x7;

    if (cond_flag & reg[R_COND]) {
        reg[R_PC] += pc_offset;
    }
}

void add(uint16_t instr) {
    uint16_t dr = (instr >> 9) & 0x7;
    uint16_t sr1 = (instr >> 6) & 0x7;
    uint16_t imm_flag = (instr >> 5) & 0x1;

    if (imm_flag) {
        uint16_t imm5 = sign_extend(instr & 0x1f, 5);
        reg[dr] = reg[sr1] + imm5;
    } else {
        uint16_t sr2 = instr & 0x7;
        reg[dr] = reg[sr1] + reg[sr2];
    }

    update_flags(dr);
}

void load(uint16_t instr) {
    uint16_t dr = (instr >> 9) & 0x7;
    uint16_t pc_offset = sign_extend(instr & 0x1ff, 9);
    reg[dr] = mem_read(reg[R_PC] + pc_offset);
    update_flags(dr);
}

void store(uint16_t instr) {
    uint16_t sr = (instr >> 9) & 0x7;
    uint16_t pc_offset = sign_extend(instr & 0x1ff, 9);
    mem_write(reg[R_PC] + pc_offset, reg[sr]);
}

void jump_register(uint16_t instr) {
    uint16_t long_bit = (instr >> 11) & 0x1;

    reg[R_R7] = reg[R_PC];

    if (long_bit) {
        uint16_t pc_offset = sign_extend(instr & 0x7ff, 11);
        reg[R_PC] += pc_offset; // JSR
    } else {
        uint16_t base_r = (instr >> 6) & 0x7;
        reg[R_PC] = reg[base_r]; // JSRR
    }
}

void bitwise_and(uint16_t instr) {
    uint16_t dr = (instr >> 9) & 0x7;
    uint16_t sr1 = (instr >> 6) & 0x7;
    uint16_t imm_flag = (instr >> 5) & 0x1;

    if (imm_flag) {
        uint16_t imm5 = sign_extend(instr & 0x1f, 5);
        reg[dr] = reg[sr1] & imm5;
    } else {
        uint16_t sr2 = instr & 0x7;
        reg[dr] = reg[sr1] & reg[sr2];
    }

    update_flags(dr);
}

void load_register(uint16_t instr) {
    uint16_t dr = (instr >> 9) & 0x7;
    uint16_t base_r = (instr >> 6) & 0x7;
    uint16_t offset = sign_extend(instr & 0x3f, 6);

    reg[dr] = mem_read(reg[base_r] + offset);
    update_flags(dr);
}

void store_register(uint16_t instr) {
    uint16_t sr = (instr >> 9) & 0x7;
    uint16_t base_r = (instr >> 6) & 0x7;
    uint16_t offset = sign_extend(instr & 0x3f, 6);

    mem_write(reg[base_r] + offset, reg[sr]);
}

void bitwise_not(uint16_t instr) {
    uint16_t dr = (instr >> 9) & 0x7;
    uint16_t sr = (instr >> 6) & 0x7;

    reg[dr] = ~reg[sr];

    update_flags(dr);
}

void load_indirect(uint16_t instr) {
    uint16_t dr = (instr >> 9) & 0x7;
    uint16_t pc_offset = sign_extend(instr & 0x1ff, 9);
    reg[dr] = mem_read(mem_read(reg[R_PC] + pc_offset));
    update_flags(dr);
}

void store_indirect(uint16_t instr) {
    uint16_t sr = (instr >> 9) & 0x7;
    uint16_t pc_offset = sign_extend(instr & 0x1ff, 9);

    mem_write(mem_read(reg[R_PC] + pc_offset), reg[sr]);
}

void jump(uint16_t instr) {
    uint16_t base_r = (instr >> 6) & 0x7; // RET handled as well
    reg[R_PC] = base_r;
}

void load_effective_address(uint16_t instr) {
    uint16_t dr = (instr >> 9) & 0x7;
    uint16_t pc_offset = sign_extend(instr & 0x1ff, 9);

    reg[dr] = reg[R_PC] + pc_offset;
    update_flags(dr);
}

/* MAIN */

int main(int argc, const char* argv[]) {

    if (argc < 2) {
        printf("lc3 [image-file1] ...\n");
        exit(2);
    }

    for (int j = 1; j < argc; ++j) {
        if (!read_image(argv[j])) {
            printf("failed to load image: %s\n", argv[j]);
            exit(1);
        }
    }

    /* since exactly one condition flag should be set at any given time, set the Z flag */
    reg[R_COND] = FL_ZRO;

    enum { PC_START = 0x3000 };
    reg[R_PC] = PC_START;

    int running = 1;
    while (running) {
        uint16_t instr = mem_read(reg[R_PC]++);
        uint16_t op = instr >> 12;

        switch (op) {
            case OP_ADD:
                add(instr);
                break;
            case OP_AND:
                bitwise_and(instr);
                break;
            case OP_NOT:
                bitwise_not(instr);
                break;
            case OP_BR:
                branch(instr);
                break;
            case OP_JMP:
                jump(instr);
                break;
            case OP_JSR:
                jump_register(instr);
                break;
            case OP_LD:
                load(instr);
                break;
            case OP_LDI:
                load_indirect(instr);
                break;
            case OP_LDR:
                load_register(instr);
                break;
            case OP_LEA:
                load_effective_address(instr);
                break;
            case OP_ST:
                store(instr);
                break;
            case OP_STI:
                store_indirect(instr);
                break;
            case OP_STR:
                store_register(instr);
                break;
            case OP_TRAP:
                break;
            case OP_RES:
            case OP_RTI:
                abort();
                break;
            default:
                break;
        }
    }
}