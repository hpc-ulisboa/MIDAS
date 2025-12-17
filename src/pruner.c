#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <stdint.h>
#include <omp.h>
#include <math.h>
#include <time.h>
#include "cgra.h"
#include "dfg.h"
#include "files.h"
#include "pqueue.h"
#include "ops.h"
#include "parson.h"

#define MAX_IO_SIDES 4
#define MAX_INTERCONNECTS 16

typedef struct
{
    int n_output_registers;
    int fu_inputs;
    char operations[MAX_OPS][16];

    int rf_size;
    int rf_ports_to_fumuxins;
    int rf_ports_to_outputregs;

    int io_directions[MAX_IO_SIDES]; // top, left, bottom, right; 0 = None, 1 = Input, 2 = Output, 3 = InputOutput
    int has_io_specs;

    int interconnects[MAX_INTERCONNECTS]; // binary array

    int ld_bw;
    int st_bw;
    int dw;
} PEParsedConfig;

int *find_square_like_shape(int N, int max_val, int N_IOs)
{
    int M;
    int best_r = -1, best_c = -1;
    int *shape = (int *)calloc(2, sizeof(int));

    float penalty_exp = 1.8;
    float best_score = 999999.99;

    int isIOBound = N_IOs / 2 - 1 >= N;

    // ------------------------------
    // 1) Check IO-bound
    // ------------------------------
    if (isIOBound)
    {
        // 1xN always features the most IOs of any shape with N PEs
        shape[0] = 1;
        shape[1] = ceil((float)N_IOs / 2 - 1); // min #PEs for which the 1xN features enough IOs
        // printf("Array is IO bound!\n");
        return shape;
    }

    // ------------------------------
    // 2) Not IO-bound: search for best shape
    // (most square-like) that respects both #PEs and #IOs
    // ------------------------------
    for (M = N; M <= max_val; ++M)
    {
        int sq = sqrt(M);

        for (int r = 1; r <= sq; ++r)
        {

            if (M % r != 0)
                continue;

            int c = M / r;

            // Compute available IO capacity
            int perim = 2 * r + 2 * c;
            if (perim < N_IOs)
            {
                // printf("[%d x %d] shape does not have enough IOs!\n", r, c);
                break; // not enough IOs
            }

            // Score = how square-like it is, penalty of M/N is applied for shapes that require more PEs
            float score = (float)(1 + abs(r - c)) * pow((float)M / (float)N, penalty_exp);
            if (score < best_score)
            {
                best_score = score;
                best_r = r;
                best_c = c;
            }
            // printf("score = %lf [%d x %d]\n", score, r, c);
        }
    }
    shape[0] = best_r;
    shape[1] = best_c;
    return shape;
}

void set_req_IOs(cgra *dev, int inputs, int outputs)
{
    int rows = get_cgra_L(dev), cols = get_cgra_C(dev);
    int n_io_ports = 2 * (rows + cols - 4); // -4 because of the IOs in the periphery which are counted for the rows and cols
    int i, j, ins_to_place = inputs, outs_to_place = outputs, rnd;
    printf("Inputs is %d\n", inputs);

    // Reset all ports
    for (j = 0; j < cols; j++)
    {
        rmvStreamFuncts(dev, 0, j);
        rmvStreamFuncts(dev, rows - 1, j);
        set_cgra_interconnect(dev, 0, j, 1, j, __INT_MAX__);
        set_cgra_interconnect(dev, 1, j, 0, j, __INT_MAX__);
        set_cgra_interconnect(dev, rows - 1, j, rows - 2, j, __INT_MAX__);
        set_cgra_interconnect(dev, rows - 2, j, rows - 1, j, __INT_MAX__);
    }
    for (i = 0; i < rows; i++)
    {
        rmvStreamFuncts(dev, i, 0);
        rmvStreamFuncts(dev, i, cols - 1);
        set_cgra_interconnect(dev, i, 0, i, 1, __INT_MAX__);
        set_cgra_interconnect(dev, i, 1, i, 0, __INT_MAX__);
        set_cgra_interconnect(dev, i, cols - 1, i, cols - 2, __INT_MAX__);
        set_cgra_interconnect(dev, i, cols - 2, i, cols - 1, __INT_MAX__);
    }

    // Top row
    if (ins_to_place > 0)
    {
        for (j = 0; j < cols; j++)
        {
            // IO port exists and is not yet an input
            if (get_cgra_tile_value(dev, 0, j) != -1 && !isInputStreamPort(dev, 0, j))
            {
                set_cgra_tile_funct(dev, 0, j, OP_STREAM_IN);
                ins_to_place--;
                if (ins_to_place <= 0)
                    break;
            }
        }
    }
    // Left col
    if (ins_to_place > 0)
    {
        for (i = 0; i < rows; i++)
        {
            // IO port exists
            if (get_cgra_tile_value(dev, i, 0) != -1 && !isInputStreamPort(dev, i, 0))
            {
                set_cgra_tile_funct(dev, i, 0, OP_STREAM_IN);
                ins_to_place--;
                if (ins_to_place <= 0)
                    break;
            }
        }
    }
    // Bottom row
    if (ins_to_place > 0)
    {
        for (j = 0; j < cols; j++)
        {
            // IO port exists and is not yet an input
            if (get_cgra_tile_value(dev, rows - 1, j) != -1 && !isInputStreamPort(dev, rows - 1, j))
            {
                set_cgra_tile_funct(dev, rows - 1, j, OP_STREAM_IN);
                ins_to_place--;
                if (ins_to_place <= 0)
                    break;
            }
        }
    }
    // Right col
    if (ins_to_place > 0)
    {
        for (i = 0; i < rows; i++)
        {
            // IO port exists
            if (get_cgra_tile_value(dev, i, cols - 1) != -1 && !isInputStreamPort(dev, i, cols - 1))
            {
                set_cgra_tile_funct(dev, i, cols - 1, OP_STREAM_IN);
                ins_to_place--;
                if (ins_to_place <= 0)
                    break;
            }
        }
    }

    /*** Outputs **************************************************************/
    // Right col
    if (outs_to_place > 0)
    {
        for (i = rows - 1; i > 0; i--)
        {
            // IO port exists
            if (get_cgra_tile_value(dev, i, cols - 1) != -1 && !isInputStreamPort(dev, i, cols - 1) && !isOutputStreamPort(dev, i, cols - 1))
            {
                set_cgra_tile_funct(dev, i, cols - 1, OP_STREAM_OUT);
                outs_to_place--;
                if (outs_to_place <= 0)
                    break;
            }
        }
    }

    // Bottom row
    if (outs_to_place > 0)
    {
        for (j = cols - 1; j > 0; j--)
        {
            // IO port exists and is not yet an input
            if (get_cgra_tile_value(dev, rows - 1, j) != -1 && !isInputStreamPort(dev, rows - 1, j) && !isOutputStreamPort(dev, rows - 1, j))
            {
                set_cgra_tile_funct(dev, rows - 1, j, OP_STREAM_OUT);
                outs_to_place--;
                if (outs_to_place <= 0)
                    break;
            }
        }
    }

    // Left col
    if (outs_to_place > 0)
    {
        for (i = rows - 1; i > 0; i--)
        {
            // IO port exists
            if (get_cgra_tile_value(dev, i, 0) != -1 && !isInputStreamPort(dev, i, 0) && !isOutputStreamPort(dev, i, 0))
            {
                set_cgra_tile_funct(dev, i, 0, OP_STREAM_OUT);
                outs_to_place--;
                if (outs_to_place <= 0)
                    break;
            }
        }
    }

    // Top row
    if (outs_to_place > 0)
    {
        for (j = cols - 1; j > 0; j--)
        {
            // IO port exists and is not yet an input
            if (get_cgra_tile_value(dev, 0, j) != -1 && !isInputStreamPort(dev, 0, j) && !isOutputStreamPort(dev, 0, j))
            {
                set_cgra_tile_funct(dev, 0, j, OP_STREAM_OUT);
                outs_to_place--;
                if (outs_to_place <= 0)
                    break;
            }
        }
    }

    if (n_io_ports > inputs + outputs)
    {
        printf("%d IOs left to place\n", n_io_ports - inputs - outputs);
        n_io_ports -= inputs + outputs;

        // Top row
        for (j = 0; j < cols; j++)
        {
            // IO port exists and is not yet an input
            if (get_cgra_tile_value(dev, 0, j) != -1 && !isInputStreamPort(dev, 0, j) && !isOutputStreamPort(dev, 0, j))
            {
                rnd = rand() % 2;
                if (rnd == 0)
                    set_cgra_tile_funct(dev, 0, j, OP_STREAM_IN);
                else
                    set_cgra_tile_funct(dev, 0, j, OP_STREAM_OUT);
            }
        }

        // Left col
        for (i = 0; i < rows; i++)
        {
            // IO port exists
            if (get_cgra_tile_value(dev, i, 0) != -1 && !isInputStreamPort(dev, i, 0) && !isOutputStreamPort(dev, i, 0))
            {
                rnd = rand() % 2;
                if (rnd == 0)
                    set_cgra_tile_funct(dev, i, 0, OP_STREAM_IN);
                else
                    set_cgra_tile_funct(dev, i, 0, OP_STREAM_OUT);
            }
        }

        // Bottom row
        for (j = 0; j < cols; j++)
        {
            // IO port exists and is not yet an input
            if (get_cgra_tile_value(dev, rows - 1, j) != -1 && !isInputStreamPort(dev, rows - 1, j) & !isOutputStreamPort(dev, rows - 1, j))
            {
                rnd = rand() % 2;
                if (rnd == 0)
                    set_cgra_tile_funct(dev, rows - 1, j, OP_STREAM_IN);
                else
                    set_cgra_tile_funct(dev, rows - 1, j, OP_STREAM_OUT);
            }
        }
        // Right col
        for (i = 0; i < rows; i++)
        {
            // IO port exists
            if (get_cgra_tile_value(dev, i, cols - 1) != -1 && !isInputStreamPort(dev, i, cols - 1) && !isOutputStreamPort(dev, i, cols - 1))
            {
                rnd = rand() % 2;
                if (rnd == 0)
                    set_cgra_tile_funct(dev, i, cols - 1, OP_STREAM_IN);
                else
                    set_cgra_tile_funct(dev, i, cols - 1, OP_STREAM_OUT);
            }
        }
    }

    // Reroute connections
    for (j = 0; j < cols; j++)
    {
        if (get_cgra_tile_value(dev, 0, j) != -1)
        {
            if (isInputStreamPort(dev, 0, j))
                set_cgra_interconnect(dev, 0, j, 1, j, 1);
            else
                set_cgra_interconnect(dev, 1, j, 0, j, 1);
        }
        if (get_cgra_tile_value(dev, rows - 1, j) != -1)
        {
            if (isInputStreamPort(dev, rows - 1, j))
                set_cgra_interconnect(dev, rows - 1, j, rows - 2, j, 1);
            else
                set_cgra_interconnect(dev, rows - 2, j, rows - 1, j, 1);
        }
    }
    for (i = 0; i < rows; i++)
    {
        if (get_cgra_tile_value(dev, i, 0) != -1)
        {
            if (isInputStreamPort(dev, i, 0))
                set_cgra_interconnect(dev, i, 0, i, 1, 1);
            else
                set_cgra_interconnect(dev, i, 1, i, 0, 1);
        }
        if (get_cgra_tile_value(dev, i, cols - 1) != -1)
        {
            if (isInputStreamPort(dev, i, cols - 1))
                set_cgra_interconnect(dev, i, cols - 1, i, cols - 2, 1);
            else
                set_cgra_interconnect(dev, i, cols - 2, i, cols - 1, 1);
        }
    }

    return;
}

int parseDir(const char *dir)
{
    char buffer[32]; // Make sure it's big enough
    strncpy(buffer, dir, sizeof(buffer));
    buffer[sizeof(buffer) - 1] = '\0'; // Ensure null-termination
    to_uppercase(buffer);
    if (!strcmp(buffer, "INPUT"))
        return OP_STREAM_IN;
    if (!strcmp(buffer, "OUTPUT"))
        return OP_STREAM_OUT;
    if (!strcmp(buffer, "INPUTOUTPUT"))
        return OP_STREAM_IO;
    return 0;
}

PEParsedConfig parse_pe_architecture(const char *filename)
{
    PEParsedConfig config;
    int i;
    // --- Set Defaults ---
    config.n_output_registers = 2;
    config.fu_inputs = 2;
    for (i = 0; i < MAX_OPS; i++)
        memset(config.operations[i], 0, MAX_OP_NAME_LEN);
    strcpy(config.operations[0], "ADD");
    strcpy(config.operations[1], "MUL");

    config.rf_size = 4;
    config.rf_ports_to_fumuxins = 2;
    config.rf_ports_to_outputregs = 2;

    config.io_directions[0] = OP_STREAM_IN;
    config.io_directions[1] = OP_STREAM_IN;
    config.io_directions[2] = OP_STREAM_OUT;
    config.io_directions[3] = OP_STREAM_OUT;
    config.has_io_specs = 0;

    config.ld_bw = 128;
    config.st_bw = 128;
    config.dw = 32;

    for (int i = 0; i < MAX_INTERCONNECTS; ++i)
        config.interconnects[i] = 0;

    // --- Parse JSON ---
    JSON_Value *root_val = json_parse_file(filename);
    if (!root_val)
    {
        fprintf(stderr, "Failed to parse JSON file: %s\nUsing default PE config.\n", filename);
        config.interconnects[ADJACENT] = 1;
        return config;
    }

    const JSON_Object *root_obj = json_value_get_object(root_val);
    config.ld_bw = json_object_get_number(root_obj, "maximum_se_load_bw");
    config.st_bw = json_object_get_number(root_obj, "maximum_se_store_bw");
    config.dw = json_object_get_number(root_obj, "data_width");

    const JSON_Object *pe_obj = json_object_get_object(root_obj, "PE_Architecture");
    if (!pe_obj)
    {
        json_value_free(root_val);
        config.interconnects[ADJACENT] = 1;
        return config;
    }

    // Output Registers
    if (json_object_has_value(pe_obj, "N_OutputRegisters"))
        config.n_output_registers = (int)json_object_get_number(pe_obj, "N_OutputRegisters");

    // Functional Unit
    const JSON_Object *fu_obj = json_object_get_object(pe_obj, "FunctionalUnit");
    if (fu_obj)
    {
        config.fu_inputs = (int)json_object_get_number(fu_obj, "Inputs");
        const JSON_Array *ops = json_object_get_array(fu_obj, "Operations");
        if (ops)
        {
            size_t op_count = json_array_get_count(ops);
            size_t limit = (op_count < MAX_OPS) ? op_count : MAX_OPS;
            for (size_t i = 0; i < limit; ++i)
            {
                const char *op_str = json_array_get_string(ops, i);
                if (op_str)
                {
                    strncpy(config.operations[i], op_str, MAX_OP_NAME_SIZE - 1);
                    config.operations[i][MAX_OP_NAME_SIZE - 1] = '\0';
                }
                else
                {
                    config.operations[i][0] = '\0'; // fallback: empty string
                }
            }
            for (size_t i = limit; i < MAX_OPS; ++i)
            {
                config.operations[i][0] = '\0';
            }
        }
    }

    // Register File
    const JSON_Object *rf_obj = json_object_get_object(pe_obj, "RegisterFile");
    if (rf_obj)
    {
        config.rf_size = (int)json_object_get_number(rf_obj, "RFSize");

        const JSON_Array *ports = json_object_get_array(rf_obj, "Ports");
        if (ports)
        {
            for (size_t i = 0; i < json_array_get_count(ports); ++i)
            {
                const JSON_Object *port_obj = json_array_get_object(ports, i);
                const char *dest = json_object_get_string(port_obj, "Destination");
                int port_num = (int)json_object_get_number(port_obj, "Ports");

                if (strcmp(dest, "FUMuxIns") == 0)
                    config.rf_ports_to_fumuxins = port_num;
                else if (strcmp(dest, "OutputRegisters") == 0)
                    config.rf_ports_to_outputregs = port_num;
            }
        }
    }

    // IOs
    const JSON_Object *io_obj = json_object_get_object(pe_obj, "IOs");
    if (io_obj)
    {
        const char *top = json_object_get_string(io_obj, "top");
        const char *left = json_object_get_string(io_obj, "left");
        const char *bottom = json_object_get_string(io_obj, "bottom");
        const char *right = json_object_get_string(io_obj, "right");

        if (top)
            config.io_directions[0] = parseDir(top);
        if (left)
            config.io_directions[1] = parseDir(left);
        if (bottom)
            config.io_directions[2] = parseDir(bottom);
        if (right)
            config.io_directions[3] = parseDir(right);

        config.has_io_specs = 1;
    }

    // Interconnects
    const JSON_Array *intconns = json_object_get_array(pe_obj, "interconnects");
    if (intconns)
    {
        for (size_t i = 0; i < json_array_get_count(intconns); ++i)
        {
            const char *str = json_array_get_string(intconns, i);
            if (!str)
                continue;

            if (strcmp(str, "Horizontal") == 0)
                config.interconnects[HORIZONTAL] = 1;
            else if (strcmp(str, "Vertical") == 0)
                config.interconnects[VERTICAL] = 1;
            else if (strcmp(str, "Diagonal") == 0)
                config.interconnects[DIAGONAL] = 1;
            else if (strcmp(str, "Adjacent") == 0)
                config.interconnects[ADJACENT] = 1;
            else if (strcmp(str, "LeftRight") == 0)
                config.interconnects[LEFT_TO_RIGHT] = 1;
            else if (strcmp(str, "RightLeft") == 0)
                config.interconnects[RIGHT_TO_LEFT] = 1;
            else if (strcmp(str, "UpDown") == 0)
                config.interconnects[UP_TO_DOWN] = 1;
            else if (strcmp(str, "DownUp") == 0)
                config.interconnects[DOWN_TO_UP] = 1;
            else if (strcmp(str, "DiagonalSE") == 0)
                config.interconnects[DIAGONAL_SE] = 1;
            else if (strcmp(str, "DiagonalNE") == 0)
                config.interconnects[DIAGONAL_NE] = 1;
            else if (strcmp(str, "DiagonalNW") == 0)
                config.interconnects[DIAGONAL_NW] = 1;
            else if (strcmp(str, "DiagonalSW") == 0)
                config.interconnects[DIAGONAL_SW] = 1;
            else if (strcmp(str, "Wrap_aroundLR") == 0)
                config.interconnects[WRAP_AROUND_LR] = 1;
            else if (strcmp(str, "Wrap_aroundRL") == 0)
                config.interconnects[WRAP_AROUND_RL] = 1;
            else if (strcmp(str, "Wrap_aroundUD") == 0)
                config.interconnects[WRAP_AROUND_UD] = 1;
            else if (strcmp(str, "Wrap_aroundDU") == 0)
                config.interconnects[WRAP_AROUND_DU] = 1;
        }
    }
    else
    {
        config.interconnects[ADJACENT] = 1;
    }

    json_value_free(root_val);
    return config;
}

cgra *createPETemplate(PEParsedConfig config, dfg **dfg_targets, int n_dfgs)
{
    int i, j, base_r = (config.io_directions[0] != 0), base_col = (config.io_directions[1] != 0);
    int n_rows = 1 + (config.io_directions[0] != 0) + (config.io_directions[2] != 0);
    int n_cols = 1 + (config.io_directions[1] != 0) + (config.io_directions[3] != 0);
    cgra *t = create_cgra(n_rows, n_cols, config.ld_bw, config.st_bw, config.dw);
    initOutputRegisters(t, base_r, base_col, config.n_output_registers, config.rf_ports_to_outputregs);
    initLocalRegisterFile(t, base_r, base_col, config.rf_size, config.rf_ports_to_fumuxins);

    int ops[MAX_OPS] = {0};

    dfg_instr *curr_instr;
    for (i = 0; i < n_dfgs; i++)
    {
        for (j = 0; j < get_dfg_size(dfg_targets[i]); j++)
        {
            curr_instr = get_dfg_instr(dfg_targets[i], j);
            if (strcmp(get_instr_op(curr_instr), "STREAM_IN") != 0 && strcmp(get_instr_op(curr_instr), "STREAM_OUT") != 0)
            {
                // printf("curr instr is op: %s\n",get_instr_op(curr_instr));
                ops[get_operation_index(get_instr_op(curr_instr))] = 1;
            }
        }
    }
    for (j = 0; j < MAX_OPS; j++)
    {
        ops[get_operation_index(config.operations[j])] = 1;
    }

    for (i = 0; i < MAX_OPS; i++)
    {
        if (ops[i] == 1)
            set_cgra_tile_funct(t, base_r, base_col, i);
    }

    // Remove corners
    if ((config.io_directions[0] != 0) && (config.io_directions[1] != 0))
        remove_pe_from_cgra(t, 0, 0);
    if ((config.io_directions[0] != 0) && (config.io_directions[3] != 0))
        remove_pe_from_cgra(t, 0, n_cols - 1);
    if ((config.io_directions[2] != 0) && (config.io_directions[1] != 0))
        remove_pe_from_cgra(t, n_rows - 1, 0);
    if ((config.io_directions[2] != 0) && (config.io_directions[3] != 0))
        remove_pe_from_cgra(t, n_rows - 1, n_cols - 1);

    if (config.io_directions[0] != 0)
    {
        initOutputRegisters(t, 0, base_col, 1, 0);
        set_cgra_tile_funct(t, 0, base_col, config.io_directions[0]);
    }
    if (config.io_directions[1] != 0)
    {
        initOutputRegisters(t, base_r, 0, 1, 0);
        set_cgra_tile_funct(t, base_r, 0, config.io_directions[1]);
    }
    if (config.io_directions[2] != 0)
    {
        initOutputRegisters(t, n_rows - 1, base_col, 1, 0);
        set_cgra_tile_funct(t, n_rows - 1, base_col, config.io_directions[2]);
    }
    if (config.io_directions[3] != 0)
    {
        initOutputRegisters(t, base_r, n_cols - 1, 1, 0);
        set_cgra_tile_funct(t, base_r, n_cols - 1, config.io_directions[3]);
    }

    for (i = 0; i < 16; i++)
    {
        if (config.interconnects[i] > 0)
            set_cgra_interconnects(t, i, 1);
    }

    set_cgra_interconnects(t, STREAM_CONN, 0);

    return t;
}

cgra *buildHmgCGRA(int rows, int cols, char *constraints_file, dfg **dfg_targets, int n_dfgs)
{
    PEParsedConfig config = parse_pe_architecture(constraints_file);
    cgra *single_tile = createPETemplate(config, dfg_targets, n_dfgs);
    int n_rows = rows + (config.io_directions[0] != 0) + (config.io_directions[2] != 0);
    int n_cols = cols + (config.io_directions[1] != 0) + (config.io_directions[3] != 0);

    cgra *new_dev = buildHmgCopy(single_tile, n_rows, n_cols);
    delete_cgra(single_tile);
    return new_dev;
}

/**
 * Parses the "maximum_II" array from a JSON file.
 * The returned array has size N + 1, where:
 *   - result[0] = N (the number of elements)
 *   - result[1..N] = values from the JSON array
 *
 * Caller is responsible for freeing the returned array.
 */
int *parse_II_constraints(const char *filename)
{
    JSON_Value *root_val = json_parse_file(filename);
    if (root_val == NULL)
    {
        fprintf(stderr, "Failed to parse JSON file: %s\n", filename);
        return NULL;
    }

    const JSON_Object *root_obj = json_value_get_object(root_val);
    const JSON_Array *ii_array = json_object_get_array(root_obj, "maximum_II");
    if (ii_array == NULL)
    {
        fprintf(stderr, "Could not find 'maximum_II' array in JSON.\n");
        json_value_free(root_val);
        return NULL;
    }

    size_t len = json_array_get_count(ii_array);
    int *result = malloc((len + 1) * sizeof(int));
    if (result == NULL)
    {
        perror("malloc failed");
        json_value_free(root_val);
        return NULL;
    }

    result[0] = (int)len;
    for (size_t i = 0; i < len; ++i)
    {
        result[i + 1] = (int)json_array_get_number(ii_array, i);
    }

    json_value_free(root_val);
    return result;
}

/**
 * Parses the "maximum_area" value from a JSON file.
 * Returns the area as a double.
 * Returns -1.0 on failure.
 */
double parse_area_constraint(const char *filename)
{
    JSON_Value *root_val = json_parse_file(filename);
    if (root_val == NULL)
    {
        fprintf(stderr, "Failed to parse JSON file: %s\n", filename);
        return -1.0;
    }

    const JSON_Object *root_obj = json_value_get_object(root_val);
    if (!json_object_has_value_of_type(root_obj, "maximum_area", JSONNumber))
    {
        fprintf(stderr, "Could not find 'maximum_area' in JSON.\n");
        json_value_free(root_val);
        return -1.0;
    }

    double area = json_object_get_number(root_obj, "maximum_area");
    json_value_free(root_val);
    return area;
}

/**
 * Parses the "maximum_power" value from a JSON file.
 * Returns the power as a double.
 * Returns -1.0 on failure.
 */
double parse_power_constraint(const char *filename)
{
    JSON_Value *root_val = json_parse_file(filename);
    if (root_val == NULL)
    {
        fprintf(stderr, "Failed to parse JSON file: %s\n", filename);
        return -1.0;
    }

    const JSON_Object *root_obj = json_value_get_object(root_val);
    if (!json_object_has_value_of_type(root_obj, "maximum_power", JSONNumber))
    {
        fprintf(stderr, "Could not find 'maximum_power' in JSON.\n");
        json_value_free(root_val);
        return -1.0;
    }

    double power = json_object_get_number(root_obj, "maximum_power");
    json_value_free(root_val);
    return power;
}

int get_recurrence_delay(dfg *graph, dfg_instr *start, dfg_instr *end)
{
    int N = get_dfg_size(graph);
    int *dist = calloc(N, sizeof(int));
    int *visited = calloc(N, sizeof(int));

    // Initialize distances to -1 (unreachable)
    for (int i = 0; i < N; i++)
        dist[i] = -1;

    // BFS queue
    dfg_instr **queue = malloc(N * sizeof(dfg_instr *));
    int front = 0, rear = 0;

    int start_id = get_instr_id(start) - 1;
    visited[start_id] = 1;
    dist[start_id] = 0;
    queue[rear++] = start;

    while (front < rear)
    {
        dfg_instr *curr = queue[front++];
        int curr_id = get_instr_id(curr) - 1;

        // Early exit if we reached the target
        if (curr == end)
            break;

        for (int k = 0; k < get_n_outputs(curr); k++)
        {
            dfg_instr *o = get_output(curr, k);
            int oid = get_instr_id(o) - 1;
            if (!visited[oid])
            {
                visited[oid] = 1;
                dist[oid] = dist[curr_id] + 1; // +1 cycle
                queue[rear++] = o;
            }
        }
    }

    int delay = dist[get_instr_id(end) - 1];

    free(queue);
    free(visited);
    free(dist);
    return delay;
}

int getRecMinIIPruner(dfg *d)
{
    register int i, j, r, recMinII = 1, recII;
    int delay;
    dfg_instr *curr;

    // RecMinII: Delay - II * distance <= 0 => II >= Delay / distance = [delta Scheduling] / [delta Iterations]
    for (i = 0; i < get_dfg_size(d); i++)
    {
        curr = get_dfg_instr(d, i);
        r = get_n_recurrences(curr);
        for (j = 0; j < r; j++)
        {

            delay = get_recurrence_delay(d, get_recurrence(curr, j), curr) + 1;

            recII = (delay + get_rec_dist(curr, j) - 1) / get_rec_dist(curr, j);
            // printf("recII is %d\n", recII);
            recMinII = recMinII > recII ? recMinII : recII;
        }
    }

    return recMinII;
}

int *findSizeforArea(dfg **dfg_targets, int n_dfgs, int *ii_constraints, int max_area, int max_power)
{
    int *resources = (int *)calloc(3, sizeof(int));
    int i, n_ins = -1, n_outs = -1, n_pes = -1, curr_ins = 0, curr_outs = 0, curr_ops = 0, mii;
    dfg *d;
    dfg_instr **d_arr;
    // This algorithm prioritizes minimizing the area of the CGRA, so it does not consider IIs below the constraints
    for (i = 0; i < n_dfgs; i++)
    {
        d = dfg_targets[i];

        mii = getRecMinIIPruner(d);
        if (mii > ii_constraints[i + 1])
        {
            printf("\033[1;31mERROR: Performance constraints (II <= %d) for DFG #%d are incompatible with the minimum feasible performance (MII = %d).\033[1;0m\n\n",
                   ii_constraints[i + 1], i + 1, mii);
            return resources;
        }

        d_arr = get_dfg_inputs(d);
        curr_ins = get_node_sublist_size(d_arr);
        free(d_arr);
        d_arr = get_dfg_outputs(d);
        curr_outs = get_node_sublist_size(d_arr);
        free(d_arr);
        d_arr = get_dfg_ops(d);
        curr_ops = get_node_sublist_size(d_arr);
        free(d_arr);

        curr_ins = (curr_ins + ii_constraints[i + 1] - 1) / ii_constraints[i + 1];
        curr_ops = (curr_ops + ii_constraints[i + 1] - 1) / ii_constraints[i + 1];
        curr_outs = (curr_outs + ii_constraints[i + 1] - 1) / ii_constraints[i + 1];

        if (n_ins < curr_ins)
            n_ins = curr_ins;
        if (n_outs < curr_outs)
            n_outs = curr_outs;
        if (n_pes < curr_ops)
            n_pes = curr_ops;
    }

    if ((double)(n_pes * 5000) > max_area)
    {
        printf("\033[1;31mERROR: Area constraints incompatible with the imposed performance constraints.\033[1;0m\n");
        return resources;
    }

    resources[0] = n_pes;
    resources[1] = n_ins;
    resources[2] = n_outs;
    return resources;
}

int *findSizeforPerformance(dfg **dfg_targets, int n_dfgs, int *ii_constraints, int max_area, int max_power)
{
    int *resources = (int *)calloc(3, sizeof(int));
    int i, ii, n_ins = -1, n_outs = -1, n_pes = -1, curr_ins = 0, curr_outs = 0, curr_ops = 0, mii;
    dfg *d;
    dfg_instr **d_arr;

    for (i = 0; i < n_dfgs; i++)
    {
        d = dfg_targets[i];

        mii = getRecMinIIPruner(d);
        if (mii > ii_constraints[i + 1])
        {
            printf("\033[1;31mERROR: Performance constraints (II <= %d) for DFG #%d are incompatible with the minimum feasible performance (MII = %d).\033[1;0m\n\n",
                   ii_constraints[i + 1], i + 1, mii);
            return resources;
        }

        for (ii = mii; ii <= ii_constraints[i + 1]; ii++)
        {
            d_arr = get_dfg_inputs(d);
            curr_ins = get_node_sublist_size(d_arr);
            free(d_arr);
            d_arr = get_dfg_outputs(d);
            curr_outs = get_node_sublist_size(d_arr);
            free(d_arr);
            d_arr = get_dfg_ops(d);
            curr_ops = get_node_sublist_size(d_arr);
            free(d_arr);
            curr_ins = (curr_ins + ii - 1) / ii;
            curr_ops = (curr_ops + ii - 1) / ii;
            curr_outs = (curr_outs + ii - 1) / ii;

            if ((double)(curr_ops * 5000) > max_area)
            {
                printf("II = %d on DFG #%d would require more than the available area.\n", ii, i + 1);
                // It is impossible to abide by the imposed constraints for this DFG
                if (ii == ii_constraints[i + 1])
                {
                    printf("\033[1;31mERROR: Area constraints incompatible with the imposed performance constraints.\033[1;0m\n");
                    return resources;
                }
            }
            else
            {
                break;
            }
        }
        if (n_ins < curr_ins)
            n_ins = curr_ins;
        if (n_outs < curr_outs)
            n_outs = curr_outs;
        if (n_pes < curr_ops)
            n_pes = curr_ops;
    }
    resources[0] = n_pes;
    resources[1] = n_ins;
    resources[2] = n_outs;
    return resources;
}

cgra *generateInitialDesignPoint(dfg **dfg_targets, int n_dfgs, char *constraints_file)
{
    int max_area, max_power, rows, cols;
    int *ii_constraints, *res;
    int max_pes;
    int *shape;
    float overhead = 1.0;

    PEParsedConfig config;
    int has_io_specs;

    printf("Generating an initial design point, considering %d input DFGs and the '%s' constraints file.\n", n_dfgs, constraints_file);

    ii_constraints = parse_II_constraints(constraints_file);

    if (ii_constraints == NULL)
    {
        fprintf(stderr, "Failed to parse II constraints from file: %s\n", constraints_file);
    }

    max_area = parse_area_constraint(constraints_file);
    max_power = parse_power_constraint(constraints_file);

    if (ii_constraints[0] < n_dfgs)
    {
        printf("\033[1;33mWARNING: The number of input DFGs (%d) exceeds the number of provided II constraints (%d). \
Only the first %d DFGs will be considered.\033[0;m\n",
               n_dfgs, ii_constraints[0], ii_constraints[0]);
        n_dfgs = ii_constraints[0];
    }

    config = parse_pe_architecture(constraints_file);
    has_io_specs = config.has_io_specs;

    res = findSizeforPerformance(dfg_targets, n_dfgs, ii_constraints, max_area, max_power);
    free(ii_constraints);

    if (res[0] > 0)
    {
        // printf("res is %d\n", res[0]);
        max_pes = max_area / 5000;
        overhead = 1.5;
        do
        {
            shape = find_square_like_shape((int)(res[0] * overhead), max_pes, res[1] + res[2]);
            rows = shape[0];
            cols = shape[1];
            free(shape);
            overhead -= 0.1;
        } while (rows == 0 || cols == 0);
        res[0] = rows * cols;
        // printf("\033[1;36mINFO: Generating design point with %d PEs.\033[1;0m\n\n", res[0]);
        cgra *dev = buildHmgCGRA(rows, cols, constraints_file, dfg_targets, n_dfgs);
        if (!has_io_specs)
            set_req_IOs(dev, res[1], res[2]);
        free(res);
        return dev;
    }

    free(res);
    return NULL;
}

cgra *buildIdealHmgCGRA(dfg **dfg_targets, int n_dfgs, int **res, char *opt_target, char *constraints_file)
{
    int max_area, max_power, rows, cols;
    int *ii_constraints, *shape;

    PEParsedConfig config;
    int has_io_specs;

    printf("Building Ideal Homogeneous CGRA, considering %d input DFGs.\n", n_dfgs);

    ii_constraints = parse_II_constraints(constraints_file);

    config = parse_pe_architecture(constraints_file);
    has_io_specs = config.has_io_specs;

    if (ii_constraints == NULL)
    {
        fprintf(stderr, "\033[1;31mFailed to parse II constraints from file: \033[0;m%s\n", constraints_file);
    }

    max_area = parse_area_constraint(constraints_file);
    max_power = parse_power_constraint(constraints_file);

    if (ii_constraints[0] < n_dfgs)
    {
        printf("\033[1;33mWARNING: The number of input DFGs (%d) exceeds the number of provided II constraints (%d). \
Only the first %d DFGs will be considered.\033[0;m\n",
               n_dfgs, ii_constraints[0], ii_constraints[0]);
        n_dfgs = ii_constraints[0];
    }

    to_uppercase(opt_target);
    if (!strcmp(opt_target, "AREA"))
    {
        printf("Optimization target: Area.\n");
        (*res) = findSizeforArea(dfg_targets, n_dfgs, ii_constraints, max_area, max_power);
    }
    else if (!strcmp(opt_target, "POWER"))
    {
        printf("Optimization target: Power.\n");
        (*res) = findSizeforArea(dfg_targets, n_dfgs, ii_constraints, max_area, max_power);
    }
    else if (!strcmp(opt_target, "PERFORMANCE"))
    {
        printf("Optimization target: Performance (II).\n");
        (*res) = findSizeforPerformance(dfg_targets, n_dfgs, ii_constraints, max_area, max_power);
    }
    else if (!strcmp(opt_target, "UTILIZATION"))
    {
        printf("Optimization target: Resource Utilization.\n");
        (*res) = findSizeforArea(dfg_targets, n_dfgs, ii_constraints, max_area, max_power);
    }
    // Default to Area
    else
    {
        printf("\033[1;33mWARNING: Unkown optimization target (%s). Defaulting to Area optimization.\033[0;m\n", opt_target);
        (*res) = findSizeforArea(dfg_targets, n_dfgs, ii_constraints, max_area, max_power);
    }
    free(ii_constraints);

    if ((*res)[0] > 0)
    {
        printf("Minimum number of PEs: %d, Input Streams: %d, Output Streams: %d\n", (*res)[0], (*res)[1], (*res)[2]);
        shape = find_square_like_shape((*res)[0], (*res)[0], (*res)[1] + (*res)[2]);
        rows = shape[0];
        cols = shape[1];
        free(shape);
        cgra *dev = buildHmgCGRA(rows, cols, constraints_file, dfg_targets, n_dfgs);
        if (!has_io_specs)
            set_req_IOs(dev, (*res)[1], (*res)[2]);
        return dev;
    }
    return NULL;
}

cgra *buildDUT(int n_pes, int n_ins, int n_outs, char *constraints_file, dfg **dfg_targets, int n_dfgs, int sign, int min_PEs)
{
    //int r, c, tries = 0;
    int rows = 0, cols = 0;
    //const int max_tries = 5;

    int *shape;

    PEParsedConfig config;
    int has_io_specs;

    /*     do
        {
            for (r = 1; r * r <= n_pes; r++)
            {
                if (n_pes % r == 0)
                {
                    c = n_pes / r;
                    rows = r;
                    cols = c;
                }
            }
            if (sign > 0 && n_pes > min_PEs)
                n_pes--;
            else
                n_pes++;
            tries++;
            // Avoid [1xP] arrays, where P is a prime number higher than 5 (1x5 should still be acceptable, I guess)
        } while ((rows <= 1 || cols <= 1) && n_pes > 6 && tries < max_tries);

        return buildHmgCGRA(rows, cols, constraints_file, dfg_targets, n_dfgs); */

    config = parse_pe_architecture(constraints_file);
    has_io_specs = config.has_io_specs;

    shape = find_square_like_shape(n_pes, n_pes, n_ins + n_outs);
    rows = shape[0];
    cols = shape[1];
    free(shape);

    cgra *dev = buildHmgCGRA(rows, cols, constraints_file, dfg_targets, n_dfgs);
    if (!has_io_specs)
        set_req_IOs(dev, n_ins, n_outs);
    return dev;
}

float computeAggressiveOptCostFun(cgra *c, int *ii_vals, float *utils, int N, float min_area, float min_power, int tgt_fun)
{
    float area = get_cgra_area_estimate(c);
    float power = get_cgra_power_estimate(c);

    // Default weight values
    float A; // For II values
    float B; // For Area values
    float P; // For Power values
    float U; // For utilization values

    switch (tgt_fun)
    {
    case 0: // Area
        A = 0.1;
        B = 0.9;
        P = 0;
        U = 0;
        break;
    case 1: // Power
        A = 0.1;
        B = 0;
        P = 0.9;
        U = 0;
        break;
    case 2: // Performance
        A = 0.9;
        B = 0.1;
        P = 0;
        U = 0;
        break;
    case 3: // Utilization (Area efficiency, perf/mm^2)
        A = 0;
        B = 0;
        P = 0;
        U = 1.0;
        break;
    default:
        // Default weight values: 90% for performance, 10% for area
        A = 0.1;
        B = 0.2;
        P = 0;
        U = 0.7;
        break;
    }

    float cost, norm_ii, norm_area, norm_power, norm_util;

    int ii_sum = array_sum_int(ii_vals, N);

    // Defined normalized II sum
    norm_ii = (float)ii_sum / (float)N;

    // Define normalized Area
    norm_area = area / min_area;

    // Defined normalized Power
    norm_power = power / min_power; // temporarily 0

    // Define normalized utilization
    norm_util = 2 - array_sum(utils, N) / N;

    // Compute the cost (minimum/ideal cost should be 1.0)
    cost = norm_ii * A + B * norm_area + P * norm_power + U * norm_util;
    return cost;
}

cgra *aggressiveOpt(cgra *template, cgra **final_maps, dfg **dfg_targets, int n_dfgs, char *opt_target, char *constraints_file)
{
    cgra *dev = template, *dut;
    cgra **mapped_devs = (cgra **)malloc(n_dfgs * sizeof(cgra *));
    cgra **dut_mappings = (cgra **)malloc(n_dfgs * sizeof(cgra *));
    dfg *d;
    int i, k, fm, ***placed = (int ***)calloc(1, sizeof(int **)), opt_tgt_fun = -1, min_res0, constraint_violations;
    int *mapped_ii_vals = (int *)malloc(n_dfgs * sizeof(int)), *res, step, curr_res0;
    int *ii_constraints = (int *)malloc((n_dfgs + 1) * sizeof(int)), *iis;
    float *util_ratios = (float *)malloc(n_dfgs * sizeof(float));
    float min_area, max_area, dev_area, curr_area, min_power, max_power, dev_power, curr_cost, new_cost;

    double start, end;
    double cpu_time_used;

    start = omp_get_wtime();

    to_uppercase(opt_target);
    if (!strcmp(opt_target, "AREA"))
        opt_tgt_fun = 0;
    else if (!strcmp(opt_target, "POWER"))
        opt_tgt_fun = 1;
    else if (!strcmp(opt_target, "PERFORMANCE"))
        opt_tgt_fun = 2;
    else if (!strcmp(opt_target, "UTILIZATION"))
        opt_tgt_fun = 3;

    max_area = parse_area_constraint(constraints_file);
    max_power = parse_power_constraint(constraints_file);

    iis = parse_II_constraints(constraints_file);
    for (i = 0; i < n_dfgs; i++)
    {

        if (iis && i < (iis[0] + 1))
            ii_constraints[i + 1] = iis[i + 1];
        else
            ii_constraints[i + 1] = __INT_MAX__;
    }
    if (iis)
        free(iis);

    if (!dev)
    {
        printf("\033[1;33mWARNING: No device model found. Generating initial design point.\033[1;0m\n");
        dev = generateInitialDesignPoint(dfg_targets, n_dfgs, constraints_file);
        printf("\033[1;36mINFO: Generated a design point with %d PEs.\033[1;0m\n\n", get_n_pe(dev));
    }

    dev_area = get_cgra_area_estimate(dev);
    max_area = (max_area < dev_area) ? max_area : dev_area;
    dev_power = get_cgra_power_estimate(dev);
    max_power = (max_power < dev_power) ? max_power : dev_power;

    printf("Determining initial mapping for %d DFGs.\n", n_dfgs);

    // Initial mapping of all DFGs, separately
    for (i = 0; i < n_dfgs; i++)
    {
        d = dfg_targets[i];

        *placed = (int **)calloc(get_dfg_size(d), sizeof(int *));

        for (k = 0; k < get_dfg_size(d); k++)
            (*placed)[k] = (int *)calloc(5, sizeof(int)); // [placed?, line & column, first_slice, last_slice, pipeline-rescheduled]
        fm = 1;

        printf("Mapping DFG #%d.\n", i + 1);
        mapped_devs[i] = HandOfGod(dev, d, placed, &fm, 1, __INT_MAX__, 0);
        mapped_ii_vals[i] = get_n_cgra_slices(mapped_devs[i]);
        util_ratios[i] = get_dynamic_pe_util_ratio(mapped_devs[i]);
        // To test pruning on the SDP
        // final_maps[i] = mapped_devs[i];

        /* if (mapped_devs[i] != NULL)
            printf("Mapped DFG #%d with II = %d.\n", i + 1, mapped_ii_vals[i]); */

        // display_cgra_in_time(mapped_devs[i], d);

        for (k = 0; k < get_dfg_size(d); k++)
            free(placed[0][k]);
        free(placed[0]);
    }
    // To test pruning on the SDP
    // return dev;
    printf("Determining ideal device, considering the %d provided DFGs.\n", n_dfgs);

    // Compute Ideal Device (min area for area opt => gives best case scenario)
    dut = buildIdealHmgCGRA(dfg_targets, n_dfgs, &res, opt_target, constraints_file);
    display_config_arch(dut);
    min_area = get_cgra_area_estimate(dut);
    min_power = get_cgra_power_estimate(dut);
    min_res0 = get_n_pe(dut);
    delete_cgra(dut);

    // Compute initial cost
    curr_cost = computeAggressiveOptCostFun(dev, mapped_ii_vals, util_ratios, n_dfgs, min_area, min_power, opt_tgt_fun);

    // Starting step size = #PEs[initial_dev] - #PEs[ideal_dev] (for now ignoring I/Os)
    step = get_n_pe(dev) - res[0];

    /**********************************************************************************
     * Main optimization loop: Binary search according to a cost function
     * Select the best device so far and add/subtract the size by the current step,
     *  yielding the current device under test (DUT)
     * Add/subtract is initialized as Subtract, and is switched if the current new DUT
     *  would fall outside the area bounds (below area for "ideal" or above the max area
     *  constraint)
     * Map all DFGs to the DUT, update the obtained IIs and measure the new cost
     * If new_cost < curr_cost => update the current best
     * Else => switch search direction from add to sub or sub to add (Step)
     * Step /= 2
     * If Step <= 1 (convergence) break
     **********************************************************************************/

    printf("\033[1;36mINFO: Starting optimizations.\033[0;0m\n");

    while (abs(step) >= 1)
    {
        curr_res0 = get_n_pe(dev) - step;
        if (curr_res0 < 1)
            dut = NULL;
        else
            dut = buildDUT(curr_res0, res[1], res[2], constraints_file, dfg_targets, n_dfgs, step, min_res0);
        if (!dut)
            curr_area = -1.1;
        else
            curr_area = get_cgra_area_estimate(dut);

        // If the area would be out of bounds, make an area change by 'step' in the opposite direction
        if (curr_area < min_area || curr_area > max_area)
        {
            step *= -1;
            delete_cgra(dut);
            curr_res0 = get_n_pe(dev) - step;
            dut = buildDUT(curr_res0, res[1], res[2], constraints_file, dfg_targets, n_dfgs, step, min_res0);
        }
        // display_config_arch(dut);

        printf("Generated a DUT with %d PEs.\n", get_n_pe(dut));
        printf("Determining the correspondent mapping for %d DFGs.\n", n_dfgs);

        constraint_violations = 0;
        // Map all DFGs to the DUT, separately
        for (i = 0; i < n_dfgs; i++)
        {
            d = dfg_targets[i];

            *placed = (int **)calloc(get_dfg_size(d), sizeof(int *));

            for (k = 0; k < get_dfg_size(d); k++)
                (*placed)[k] = (int *)calloc(5, sizeof(int)); // [placed?, line & column, first_slice, last_slice, pipeline-rescheduled]
            fm = 1;

            printf("Mapping DFG #%d.\n", i + 1);
            dut_mappings[i] = HandOfGod(dut, d, placed, &fm, 1, ii_constraints[i + 1], 0);
            mapped_ii_vals[i] = get_n_cgra_slices(dut_mappings[i]);
            if (dut_mappings[i] == NULL)
                mapped_ii_vals[i] = ii_constraints[i + 1] + 1;
            util_ratios[i] = get_dynamic_pe_util_ratio(dut_mappings[i]);

            if (mapped_ii_vals[i] > ii_constraints[i + 1])
                constraint_violations = 1;
            // printf("constraints: %d > %d\n",ii_constraints[i + 1], mapped_ii_vals[i]);
            /* if (dut_mappings[i] != NULL)
                printf("Mapped DFG #%d with II = %d.\n", i + 1, mapped_ii_vals[i]); */

            // display_cgra_in_time(dut_mappings[i], d);

            for (k = 0; k < get_dfg_size(d); k++)
                free(placed[0][k]);
            free(placed[0]);
        }
        printf("constraints violiations: %d\n", constraint_violations);
        new_cost = computeAggressiveOptCostFun(dut, mapped_ii_vals, util_ratios, n_dfgs, min_area, min_power, opt_tgt_fun);

        // The current DUT yields better results than the previous best!
        if (new_cost < curr_cost && !constraint_violations)
        {
            printf("\033[1;36mINFO: Current DUT yielded better results than the previous best!\033[0;0m\n\n");
            delete_cgra(dev);
            curr_cost = new_cost;
            dev = dut;
            for (i = 0; i < n_dfgs; i++)
            {
                delete_cgra(mapped_devs[i]);
                mapped_devs[i] = dut_mappings[i];
                dut_mappings[i] = NULL;
            }
        }
        // Current result is worse, try switching the step direction
        else
        {
            printf("\033[1;36mINFO: Current DUT yielded worse results than the previous best.\033[0;0m\n\n");
            delete_cgra(dut);
            for (i = 0; i < n_dfgs; i++)
            {
                delete_cgra(dut_mappings[i]);
                dut_mappings[i] = NULL;
            }
            // step *= -1;
        }

        // Step is halved for the next iteration
        if (step == -1)
            step = 0;
        else
            step >>= 1;
    }

    printf("\033[1;32mThe optimizer has converged!\033[0;0m\n");
    display_config_arch(dev);

    // When the target is performance
    if (opt_tgt_fun == 2 || opt_tgt_fun == -1)
    {
        printf("\033[1;36mINFO: Attempting to parallelize the DFG mappings for better resource usage.\033[0;0m\n");
        for (i = 0; i < n_dfgs; i++)
        {
            mapped_devs[i] = parallelize_mapping(mapped_devs[i], dfg_targets[i], placed, 0);
        }
    }

    for (i = 0; i < n_dfgs; i++)
    {
        display_cgra_in_time(mapped_devs[i], dfg_targets[i]);
        final_maps[i] = mapped_devs[i];
    }

    free(res);
    free(placed);
    for (i = 0; i < n_dfgs; i++)
    {
        // delete_cgra(mapped_devs[i]);
        delete_cgra(dut_mappings[i]);
    }
    // free(mapped_devs);
    free(dut_mappings);
    free(mapped_ii_vals);
    free(ii_constraints);
    free(util_ratios);

    end = omp_get_wtime();
    cpu_time_used = ((double)(end - start)); // / CLOCKS_PER_SEC;
    printf("execution time: %lf\n", cpu_time_used);

    return dev;
}