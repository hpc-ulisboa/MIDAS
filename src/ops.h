#ifndef OPS_H
#define OPS_H

#define MAX_OPS 128  // Assume up to 128 possible operations

typedef enum {
    
    // Generic PE Types
    OP_FULL = 0, // serves both as an ALU and LSU
    OP_LSU = 1,
    // Not PEs, rather "Stream Ports"
    OP_STREAM_IN = 2,
    OP_STREAM_OUT = 3,
    OP_STREAM_IO = -4,
    OP_ALU = 4,

    // Arithmetic OPs
    OP_ADD = 5,
    OP_SUB,
    OP_MUL,
    OP_DIV,
    OP_ASHR,
    // Logic OPs
    OP_AND,
    OP_OR,
    OP_XOR,
    // FP OPs
    OP_FADD,
    OP_FMUL,
    OP_MADD3,
    OP_MSUB3,
    OP_NMADD3,
    OP_NMSUB3,
    OP_MANT,
    OP_NEGEXP,
    OP_NOP_SHF,

    OP_MADD2,
    OP_NMADD2,
    // Conditionals
    // Load/Store OPs
    OP_LOAD,
    OP_STORE,
    // Branch and Loop control OPs
    OP_ICMP,
    OP_MAX3,
    OP_MIN3,
    OP_EQ3,
    OP_NEQ3,
    OP_PHI,
    OP_BR,
    OP_CONST,
    OP_MAX  // Total number of supported operations
} OperationIndex;

char *get_operation(int index);
int get_operation_index(const char *operation);
float get_op_estimated_area_cost(int operation, int data_width);
float get_estimated_mux_area(int mux_length, int data_width);
float get_op_estimated_power_cost(int operation, int data_width);
float get_estimated_mux_power(int mux_length, int data_width);

#endif
