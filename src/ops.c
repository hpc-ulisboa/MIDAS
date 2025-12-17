#include <stdint.h>
#include <string.h>

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
    
    // Load/Store OPs
    OP_LOAD,
    OP_STORE,
    // Branch and Loop control OPs
    OP_ICMP,
    OP_PHI,

    OP_MAX3,
    OP_MIN3,
    OP_EQ3,
    OP_NEQ3,

    OP_BR,
    OP_CONST,
    OP_MAX  // Total number of supported operations
} OperationIndex;

const char *operation_names[] = {
    [OP_STREAM_IN] = "STREAM_IN",
    [OP_STREAM_OUT] = "STREAM_OUT",
    [OP_ADD] = "ADD",      
    [OP_SUB] = "SUB",      
    [OP_MUL] = "MUL",      
    [OP_DIV] = "DIV",
    [OP_ASHR] = "ASHR",      
    [OP_AND] = "AND",
    [OP_OR] = "OR",    
    [OP_XOR] = "XOR",

    [OP_FADD] = "FADD",
    [OP_FMUL] = "FMUL",
    [OP_MADD3] = "MADD3",
    [OP_MSUB3] = "MSUB3",
    [OP_NMADD3] = "NMADD3",
    [OP_NMSUB3] = "NMSUB3",
    [OP_MANT] = "MANT",
    [OP_NEGEXP] = "NEGEXP",
    [OP_NOP_SHF] = "NOP_SHF",
    [OP_MADD2] = "MADD2",
    [OP_NMADD2] = "NMADD2",

    [OP_LOAD] = "LOAD",
    [OP_STORE] = "STORE",
    [OP_ICMP] = "ICMP",

    [OP_MAX3] = "MAX3",
    [OP_MIN3] = "MIN3",
    [OP_EQ3] = "EQ3",
    [OP_NEQ3] = "NEQ3",

    [OP_PHI] = "PHI",
    [OP_BR] = "BR",
    [OP_CONST] = "CONST"
};

char *get_operation(int index) {
    if (index >= 0 && index < OP_MAX) {
        return (char *)operation_names[index];
    }
    return NULL;
}

int get_operation_index(const char *operation) {
    
    // Loop through the array and check for a match
    for (int i = 0; i < sizeof(operation_names) / sizeof(operation_names[0]); i++) {
        if (operation_names[i] && strcmp(operation, operation_names[i]) == 0) {
            return i;
        }
    }
    return -1;  // Return -1 if not found
}

/***************
 * "Dictionary"-like array with the areas, considered to be in squared microns
 */
const float functional_unit_areas[] = {
    [OP_STREAM_IN] = 0,
    [OP_LSU] = 6000,
    [OP_ALU] = 51000,
    [OP_ADD] = 300,      
    [OP_SUB] = 300,      
    [OP_MUL] = 2000,      
    [OP_DIV] = 15000,
    [OP_ASHR] = 200,      
    [OP_AND] = 300,
    [OP_OR] = 300,    
    [OP_XOR] = 300,
    [OP_LOAD] = 3000,
    [OP_STORE] = 3000,
    [OP_ICMP] = 300,
    [OP_PHI] = 600,
    [OP_BR] = 500,
    [OP_CONST] = 0
};

/**********************************************************
 * Estimates the area of a target FU operation [um^2]
 * Based off of a linear regression
 *********************************************************/
float get_op_estimated_area_cost(int index, int data_width){
    switch(index)
    {
        case OP_ADD:
        return 6.0517 * data_width - 40.001;
        break;
        case OP_SUB:
        return 6.0404 * data_width - 37.301;
        break;
        case OP_MUL: // Computed functions to estimate the #cells of different types; weighted addition of all, considering gate areas
        return 5.4692 * data_width * data_width - 119.29 * data_width + 803.9;		
        break;
        case OP_ASHR: // similar to OP_MUL
        return 5.3537 * data_width - 26.518;		
        break;
        case OP_AND:
        case OP_OR:
        return 0.378 * data_width; 
        break;
        case OP_XOR:
        return 0.6324 * data_width + 0.0828;
        break;
        case OP_ICMP:
        return 2.1853 * data_width - 7.1676;		
        break;
    }
    if (index >= 0 && index < OP_MAX) {
        return functional_unit_areas[index];
    }
    return -1.0;
}

float get_estimated_mux_area(int mux_length, int data_width)
{
    if (mux_length <= 0)
        return 0;
    return 1.215 * (mux_length - 1) * data_width; // assume a mux tree
}

/**********************************************************
 * Estimates the power of a target FU operation [uW]
 * Based off of a linear regression
 *********************************************************/
float get_op_estimated_power_cost(int index, int data_width){
    switch(index)
    {
        case OP_ADD:
        return 4.413 * data_width - 27.275;
        break;
        case OP_SUB:
        return 4.57 * data_width - 27.625;
        break;
        case OP_MUL: // Computed functions to estimate the #cells of different types; weighted addition of all, considering gate areas
        return 4.3762 * data_width * data_width - 90.39 * data_width + 575;		
        break;
        case OP_ASHR: // similar to OP_MUL
        return 3.682 * data_width - 22.236;		
        break;
        case OP_AND:
        return 0.4329 * data_width + 0.5209;
        break;
        case OP_OR:
        return 0.6499 * data_width + 0.7873; 
        break;
        case OP_XOR:
        return 0.7826 * data_width + 1.0474;
        break;
        case OP_ICMP:
        return 2.259 * data_width - 4.6708;		
        break;
    }
    if (index >= 0 && index < OP_MAX) {
        return 0;
    }
    return -1.0;
}

float get_estimated_mux_power(int mux_length, int data_width)
{
    if (mux_length <= 0)
        return 0;
    return 0.505 * (mux_length - 1) * data_width; // assume a mux tree
}