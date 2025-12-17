#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include "dfg.h"
#include "cgra.h"
#include "files.h"
#include "parson.h"

char directionStrings[7][7] = {"None", "North", "West", "South", "East", "FU_out", "LRF"};

/**************************************************************************************
 * Inputs: Device Slice, target PE coordinates, dfg and a binary array for directions
 * Directions array: [North ? 1:0, West ? 1:0, South ? 1:0, East ? 1:0]
 * Sets to 1 all directions that are active according to the PE's configuration on the
 * target slice
 * return values: success ? 1 : 0 ; changes the directions array directly
 *************************************************************************************/
int getInputDirection(cgra *c, int i, int j, int k, int *directions, int **placed)
{
    int /* k, */ ii, jj, pos, id;
    dfg_instr *target = get_cgra_tile(c, i, j);
    id = get_instr_id(target);
    // for (k = 0; k < get_n_inputs(target); k++)
    //{
    // pos = isConnectedToPE(c, i, j, id, get_instr_id(get_input(target, k)));
    pos = inputConnectedToPE(c, i, j, id, get_instr_id(get_input(target, k)), placed[id - 1][2]);
    ii = pos / get_cgra_C(c);
    jj = pos % get_cgra_C(c);

    // Inputs linking from the north (neighbour above or wrap around down-up) are marked as North Inputs
    if ((ii == i - 1 && jj == j) || (hasInterconnects(getFirstSlice(c), WRAP_AROUND_DU) && ii >= get_cgra_L(c) - 2 && jj == j))
        directions[0] = get_instr_id(get_input(target, k));
    // Inputs linking from the west (neighbour to the west or wrap around right-left) are marked as West Inputs
    else if ((ii == i && jj == j - 1) || (hasInterconnects(getFirstSlice(c), WRAP_AROUND_LR) && ii == i && jj >= get_cgra_C(c) - 2))
        directions[1] = get_instr_id(get_input(target, k));
    // Inputs linking from the south (neighbour below or wrap around up-down) are marked as South Inputs
    else if ((ii == i + 1 && jj == j) || (hasInterconnects(getFirstSlice(c), WRAP_AROUND_UD) && ii <= 1 && jj == j))
        directions[2] = get_instr_id(get_input(target, k));
    // Inputs linking from the east (neighbour to the east or wrap around left-right) are marked as East Inputs
    else if ((ii == i && jj == j + 1) || (hasInterconnects(getFirstSlice(c), WRAP_AROUND_LR) && ii == i && jj <= 1))
        directions[3] = get_instr_id(get_input(target, k));
    // Check if it is in the register file
    else
    {
        if (hasLRFEntry(getPrevModuloSlice(c), i, j, placed[id - 1][2] - 1, get_instr_id(get_input(target, k))))
            directions[5] = get_instr_id(get_input(target, k));
    }
    //}

    return 1;
}

int getRecDirection(cgra *c, int i, int j, int rid, int recDist, int *directions, int **placed)
{
    int ii, jj, pos, id, offset;
    dfg_instr *target = get_cgra_tile(c, i, j);
    id = get_instr_id(target);
    offset = get_n_cgra_slices(getFirstSlice(c)) * recDist;

    pos = inputConnectedToPE(c, i, j, id, rid, placed[id - 1][2] + offset);
    if (pos == -1)
    {
        ii = -1;
        jj = -1;
    }
    else
    {
        ii = pos / get_cgra_C(c);
        jj = pos % get_cgra_C(c);
    }

    if (ii < i && jj == j)
        directions[0] = rid;
    else if (ii == i && jj < j)
        directions[1] = rid;
    else if (ii > i && jj == j)
        directions[2] = rid;
    else if (ii == i && jj > j)
        directions[3] = rid;
    // Check if it is in the register file
    else
    {
        if (hasLRFEntry(getPrevModuloSlice(c), i, j, placed[id - 1][2] - 1 + offset, rid))
            directions[5] = rid;
    }

    return 1;
}

int getOutputDirection(cgra *c, int i, int j, int **placed, int idx)
{
    int directions[6] = {0};

    if (getOutputRegister(c, i, j, idx) == 0)
    {
        return 0;
    }
    dfg_instr *t = get_cgra_tile(c, i, j);
    int sched = -1;

    if (t != NULL)
        sched = placed[get_instr_id(t) - 1][2] + getPEPipelineStages(c, i, j) - 1;

    // This PE outputs the result that it has just computed
    if (t != NULL && idx == hasOutputRegister(c, i, j, get_instr_id(t), sched))
    {
        return 5;
    }

    setDirectionOpIDs(c, i, j, directions, idx);
    for (int k = 0; k < 6; k++)
    {
        if (directions[k] == getOutputRegister(c, i, j, idx) && getOutputRegister(c, i, j, idx) != 0)
        {
            return k + 1;
        }
    }

    return -1;
}

int getOutPortSels(cgra *c, int i, int j, int **placed, int *outPortSel, int *or_lut)
{
    // North
    if (getConnVal(getNextModuloSlice(c), i - 1, j, i, j) > 0)
        outPortSel[0] = hasOutputRegister(c, i, j,
                                          getConnVal(getNextModuloSlice(c), i - 1, j, i, j), getConnTime(getNextModuloSlice(c), i - 1, j, i, j));
    else
        outPortSel[0] = -1;

    // West
    if (getConnVal(getNextModuloSlice(c), i, j - 1, i, j))
        outPortSel[1] = hasOutputRegister(c, i, j,
                                          getConnVal(getNextModuloSlice(c), i, j - 1, i, j), getConnTime(getNextModuloSlice(c), i, j - 1, i, j));
    else
        outPortSel[1] = -1;

    // South
    if (getConnVal(getNextModuloSlice(c), i + 1, j, i, j))
        outPortSel[2] = hasOutputRegister(c, i, j,
                                          getConnVal(getNextModuloSlice(c), i + 1, j, i, j), getConnTime(getNextModuloSlice(c), i + 1, j, i, j));
    else
        outPortSel[2] = -1;

    // East
    if (getConnVal(getNextModuloSlice(c), i, j + 1, i, j))
        outPortSel[3] = hasOutputRegister(c, i, j,
                                          getConnVal(getNextModuloSlice(c), i, j + 1, i, j), getConnTime(getNextModuloSlice(c), i, j + 1, i, j));
    else
        outPortSel[3] = -1;
    
    // Perform renaming
    for (int k = 0; k < 4; k++)
    {
        if (outPortSel[k] >= 0)
            outPortSel[k] = or_lut[outPortSel[k]];
    }

    return 1;
}

int getLRFAccessDirection(cgra *c, int i, int j)
{
    int directions[6] = {0};

    if (getRFAccess(c, i, j) == 0)
    {
        return 0;
    }
    dfg_instr *t = get_cgra_tile(c, i, j);

    if (t != NULL && getRFAccess(c, i, j) == get_instr_id(t))
    {
        return 5;
    }

    setDirectionOpIDs(c, i, j, directions, -1);
    for (int k = 0; k < 6; k++)
    {
        if (directions[k] == getRFAccess(c, i, j) && getRFAccess(c, i, j) != 0)
        {
            return k + 1;
        }
    }

    return -1;
}

int *getInputRecArr(cgra *c, dfg *d, dfg_instr *target)
{
    int N = get_dfg_size(d), n, recCount = 0, r, id, rid;
    int *recArr = (int *)malloc(sizeof(int) * (N + 1));
    dfg_instr *rec;

    id = get_instr_id(target);

    for (n = 0; n < N; n++)
    {
        rec = get_dfg_instr(d, n);
        rid = get_instr_id(rec);
        for (r = 0; r < get_n_recurrences(rec); r++)
        {
            if (get_instr_id(get_recurrence(rec, r)) == id)
            {
                recArr[++recCount] = rid;
                break;
            }
        }
    }
    recArr[0] = recCount;
    return recArr;
}

JSON_Value *create_pe_object(cgra *c, dfg *d, int i, int j, int **placed)
{

    dfg_instr *target = get_cgra_tile(c, i, j);
    char fu_out[MAX_OP_NAME_LEN] = {0}, op_name[MAX_OP_NAME_LEN] = {0};
    char inputs[4][MAX_OP_NAME_LEN] = {"", "", "", ""}, output_register[MAX_OP_NAME_LEN] = {0};
    int input_count = 0, const_count = 0, out, n_i = 0, input_ids[4] = {0}, addr;
    int inputDirections[6] = {0}, input_addrs[4] = {-2, -2, -2, -2};

    // Create: { "PE": { ... } }
    JSON_Value *outer_val = json_value_init_object();
    JSON_Object *outer_obj = json_value_get_object(outer_val);

    JSON_Value *pe_val = json_value_init_object();
    JSON_Object *pe_obj = json_value_get_object(pe_val);

    // Define FU output
    if (target == NULL)
    {
        strcpy(fu_out, "None\0");
        strcpy(op_name, "None\0");
    }
    else
    {
        strncpy(fu_out, get_instr_op(target), strlen(get_instr_op(target)));
        strncpy(op_name, get_instr_name(target), strlen(get_instr_name(target)));
        input_count = get_n_inputs(target);
        // getInputDirections(c, i, j, inputDirections, placed);
        for (int n = 0; n < get_n_inputs(target); n++)
        {

            getInputDirection(c, i, j, n, inputDirections, placed);
            for (int k = 0; k < 6; k++)
            {
                if (inputDirections[k] > 0)
                {
                    input_ids[n_i] = inputDirections[k];
                    if (k == 5)
                        input_addrs[n_i] = getAddress(getPrevModuloSlice(c), i, j, placed[get_instr_id(target) - 1][2] - 1, get_instr_id(get_input(target, n)));
                    strncpy(inputs[n_i++], directionStrings[k + 1], strlen(directionStrings[k + 1]));
                    inputDirections[k] = 0;
                    break;
                }
            }
        }

        // Recurrences
        int *recArr = getInputRecArr(c, d, target);
        input_count += recArr[0];
        for (int n = 0; n < recArr[0]; n++)
        {
            getRecDirection(c, i, j, recArr[n + 1], get_rec_dist_from_instr(get_instr_by_op_id(d, recArr[n + 1]), target), inputDirections, placed);
            for (int k = 0; k < 6; k++)
            {
                if (inputDirections[k] > 0)
                {
                    input_ids[n_i] = inputDirections[k];
                    if (k == 5)
                    {
                        int addr_time = placed[get_instr_id(target) - 1][2] - 1;
                        addr_time += get_n_cgra_slices(getFirstSlice(c)) * get_rec_dist_from_instr(get_instr_by_op_id(d, recArr[n + 1]), target);
                        input_addrs[n_i] = getAddress(getPrevModuloSlice(c), i, j, addr_time, recArr[n + 1]);
                    }
                    strncpy(inputs[n_i++], directionStrings[k + 1], strlen(directionStrings[k + 1]));
                    inputDirections[k] = 0;
                    break;
                }
            }
        }
        free(recArr);

        const_count = get_n_consts(target);
    }

    // Fill inner PE object
    json_object_set_number(pe_obj, "row", i);
    json_object_set_number(pe_obj, "col", j);
    if (get_pe_power_mode(c, i, j) == POWER_ON)
        json_object_set_string(pe_obj, "power", "on");
    else
        json_object_set_string(pe_obj, "power", "off");
    // json_object_set_number(pe_obj, "RFSize", getRFSize(c, i, j));
    // json_object_set_number(pe_obj, "N_OutputRegisters", getNumOutputRegisters(c, i, j));
    json_object_set_string(pe_obj, "fu_out", fu_out);
    json_object_set_string(pe_obj, "op_name", op_name);

    JSON_Value *or_arr_val = json_value_init_array();
    JSON_Array *or_arr = json_value_get_array(or_arr_val);
    JSON_Value *or_lrf_arr_val = json_value_init_array();
    JSON_Array *or_lrf_arr = json_value_get_array(or_lrf_arr_val);
    JSON_Value *or_other_arr_val = json_value_init_array();
    JSON_Array *or_other_arr = json_value_get_array(or_other_arr_val);


    // Look up table for the output registers (essentially output register renaming)
    int *or_lut = (int *)malloc(getNumOutputRegisters(c, i, j) * sizeof(int));
    int cnt_id = 0;

    for (int k = 0; k < getNumOutputRegisters(c, i, j); k++)
    {
        JSON_Value *or_val = json_value_init_object();
        JSON_Object *or_obj = json_value_get_object(or_val);

        or_lut[k] = -1; // Initialize LUT entry with -1 (to be filled later)

        // Define output register
        memset(output_register, 0, sizeof(output_register));
        out = getOutputDirection(c, i, j, placed, k);
        strncpy(output_register, directionStrings[out], strlen(directionStrings[out]));

        json_object_set_string(or_obj, "value", output_register);

        // These will take priority
        if (!strcmp(output_register, "LRF"))
        {
            json_object_set_number(or_obj, "address", getAddressNoTime(getPrevModuloSlice(c), i, j, getOutputRegister(c, i, j, k)));
            json_array_append_value(or_lrf_arr, or_val);
            or_lut[k] = cnt_id++; // cnt gives priority to outputs that use LRF read ports
        }
        else // These do not use a LRF Read Port
        {
            json_array_append_value(or_other_arr, or_val);
        }
    }

    // Fill the remaining empty entries in order, starting from the current cnt_id value
    for (int k = 0; k < getNumOutputRegisters(c, i, j); k++)
    {
        if (or_lut[k] == -1)
            or_lut[k] = cnt_id++;
    }

    // transfer priority items
    for (size_t i = 0; i < json_array_get_count(or_lrf_arr); i++)
    {
        JSON_Value *item = json_array_get_value(or_lrf_arr, i);
        json_array_append_value(or_arr, json_value_deep_copy(item));
    }

    // transfer other items
    for (size_t i = 0; i < json_array_get_count(or_other_arr); i++)
    {
        JSON_Value *item = json_array_get_value(or_other_arr, i);
        json_array_append_value(or_arr, json_value_deep_copy(item));
    }

    json_object_set_value(pe_obj, "output_registers", or_arr_val);
    json_value_free(or_lrf_arr_val);
    json_value_free(or_other_arr_val);

    

    // Output Ports Mux Selects
    JSON_Value *outportsel_val = json_value_init_object();
    JSON_Object *outportsel_obj = json_value_get_object(outportsel_val);

    int outPortSel[4] = {0};
    getOutPortSels(c, i, j, placed, outPortSel, or_lut);
    free(or_lut);
    json_object_set_number(outportsel_obj, "north", outPortSel[0]);
    json_object_set_number(outportsel_obj, "west", outPortSel[1]);
    json_object_set_number(outportsel_obj, "south", outPortSel[2]);
    json_object_set_number(outportsel_obj, "east", outPortSel[3]);
    json_object_set_value(pe_obj, "outPortSel", outportsel_val);

    // RF Access
    JSON_Value *rfac_obj_val = json_value_init_object();
    JSON_Object *rfac_obj = json_value_get_object(rfac_obj_val);
    if (getRFAccess(c, i, j) > 0)
    {
        out = getLRFAccessDirection(c, i, j);
        json_object_set_string(rfac_obj, "port", directionStrings[out]);
        json_object_set_string(rfac_obj, "source_op", get_instr_name(get_instr_by_op_id(d, getRFAccess(c, i, j))));
        json_object_set_number(rfac_obj, "address", getAddressNoTime(c, i, j, getRFAccess(c, i, j)));
    }
    else
    {
        json_object_set_string(rfac_obj, "port", "None\0");
        json_object_set_string(rfac_obj, "source_op", "None\0");
    }
    json_object_set_value(pe_obj, "written_to_LRF", rfac_obj_val);

    // Inputs array
    JSON_Value *inputs_val = json_value_init_array();
    JSON_Value *rf_inputs_val = json_value_init_array();
    JSON_Value *other_inputs_val = json_value_init_array();
    JSON_Array *inputs_arr = json_value_get_array(inputs_val);
    JSON_Array *rf_inputs_arr = json_value_get_array(rf_inputs_val);
    JSON_Array *other_inputs_arr = json_value_get_array(other_inputs_val);

    for (size_t k = 0; k < input_count; k++)
    {
        JSON_Value *input_obj_val = json_value_init_object();
        JSON_Object *input_obj = json_value_get_object(input_obj_val);

        if (strlen(inputs[k]) > 0)
        {
            json_object_set_string(input_obj, "port", inputs[k]);
            json_object_set_string(input_obj, "source_op", get_instr_name(get_instr_by_op_id(d, input_ids[k])));
            if (!strcmp(inputs[k], "LRF"))
            {
                addr = input_addrs[k];
                if (addr < 0)
                    printf("WARNING: Could not determine LRF address for PE [%d,%d] @ time %d\n", i, j, placed[get_instr_id(target) - 1][2] + 1);
                json_object_set_number(input_obj, "address", addr);
                json_object_set_string(input_obj, "entered_through_port", directionStrings[getEnteredPort(c, i, j, addr)]);
                // json_array_append_value(inputs_arr, input_obj_val);
                json_array_append_value(rf_inputs_arr, input_obj_val);
            }
        }
        else
        {
            json_object_set_string(input_obj, "port", "None\0");
            json_object_set_string(input_obj, "source_op", "None\0");
        }

        // json_array_append_value(inputs_arr, input_obj_val);
        json_array_append_value(other_inputs_arr, input_obj_val);
    }

    for (size_t k = 0; k < const_count; k++)
    {
        JSON_Value *input_obj_val = json_value_init_object();
        JSON_Object *input_obj = json_value_get_object(input_obj_val);

        json_object_set_string(input_obj, "port", "Const");
        json_object_set_string(input_obj, "source_op", get_instr_name(get_const(target, k)));
        json_object_set_number(input_obj, "value", get_const_val(get_const(target, k)));
        if (getCUsize(c, i, j) > 0)
            addr = getCnstAddress(c, i, j, get_instr_id(get_const(target, k)));
        else // constants are always stored in the RF, so the RFTime is irrelevant
            addr = getAddress(c, i, j, 0, get_instr_id(get_const(target, k)));
        json_object_set_number(input_obj, "address", addr);
        // json_array_append_value(inputs_arr, input_obj_val);
        json_array_append_value(rf_inputs_arr, input_obj_val);
    }

    // transfer priority items
    for (size_t i = 0; i < json_array_get_count(rf_inputs_arr); i++)
    {
        JSON_Value *item = json_array_get_value(rf_inputs_arr, i);
        json_array_append_value(inputs_arr, json_value_deep_copy(item));
    }

    // transfer other items
    for (size_t i = 0; i < json_array_get_count(other_inputs_arr); i++)
    {
        JSON_Value *item = json_array_get_value(other_inputs_arr, i);
        json_array_append_value(inputs_arr, json_value_deep_copy(item));
    }

    json_object_set_value(pe_obj, "inputs", inputs_val);
    json_value_free(rf_inputs_val);
    json_value_free(other_inputs_val);

    // Attach PE object under "PE" key
    json_object_set_value(outer_obj, "PE", pe_val);
    return outer_val;
}

JSON_Value *create_io_object(cgra *c, dfg *d, int i, int j, int **placed)
{
    dfg_instr *target = get_cgra_tile(c, i, j);
    int type = 0, conn = -1, ii, jj;

    JSON_Value *outer_val = json_value_init_object();
    JSON_Object *outer_obj = json_value_get_object(outer_val);

    JSON_Value *io_val = json_value_init_object();
    JSON_Object *io_obj = json_value_get_object(io_val);

    if (target == NULL || !isIO(target))
        type = 0;
    else
    {
        if (!strcmp(get_instr_op(target), "STREAM_IN"))
        {
            if (!strcmp(get_instr_op(target), "STREAM_OUT"))
                type = 3; // IO
            else
                type = 1; // Input
        }
        else if (!strcmp(get_instr_op(target), "STREAM_OUT"))
            type = 2; // Output
        else
            type = 0;
    }

    if (target == NULL)
    {
        json_value_free(outer_val);
        json_value_free(io_val);
        return NULL;
    }

    if (type == 1)
        json_object_set_string(io_obj, "type", "Input\0");
    else if (type == 2)
        json_object_set_string(io_obj, "type", "Output\0");
    else if (type == 3)
        json_object_set_string(io_obj, "type", "InputOutput\0");
    else
        json_object_set_string(io_obj, "type", "None\0");

    json_object_set_number(io_obj, "row", i);
    json_object_set_number(io_obj, "col", j);

    json_object_set_string(io_obj, "source_io", get_instr_name(target));

    JSON_Value *connects_to_val = json_value_init_object();
    JSON_Object *connects_to_obj = json_value_get_object(connects_to_val);

    if (type == 1 || type == 3)
        conn = hasConnectedPEs(c, i, j);
    if (type == 2 || (type == 3 && conn == -1))
        conn = ioConnectsToPE(c, i, j);
    if (conn > -1)
    {
        ii = conn / get_cgra_C(c);
        jj = conn % get_cgra_C(c);
        json_object_set_number(connects_to_obj, "row", ii);
        json_object_set_number(connects_to_obj, "col", jj);
        json_object_set_value(io_obj, "connects_to", connects_to_val);
    }

    if (type == 1 || type == 3)
        json_object_set_number(io_obj, "cycle_start", placed[get_instr_id(target) - 1][2]);

    json_object_set_value(outer_obj, "IO", io_val);
    return outer_val;
}

int exportMapping(cgra *fs, dfg *d, int ***placed, char *filename, int vectorWidth)
{

    /***************************************************************************************************************
     * Export, for each PE, the relevant information regarding its configuration:
     * --> Operation that it computes (FU)
     * --> Inputs of the operation (e.g. [North, West, RF] if the inputs are routed from the North, West and the RF)
     * --> Output (which value is being forwarded to the output register: north, west, RF, FU output, etc.)
     * --> Number of constants stored in the RF (and which)
     * --> Number of variable values stored in the RF (for now just this, maybe?)
     **************************************************************************************************************/

    int i, j, s, II = get_n_cgra_slices(fs), effR = 0, effC = 0, currC;
    cgra *c;

    // Create root
    JSON_Value *root_val = json_value_init_object();
    JSON_Object *root_obj = json_value_get_object(root_val);

    // Create "Mapping Results" object
    JSON_Value *map_val = json_value_init_object();
    JSON_Object *map_obj = json_value_get_object(map_val);

    // Create "Configuration Words" array
    JSON_Value *cw_val = json_value_init_array();
    JSON_Array *cw_arr = json_value_get_array(cw_val);

    JSON_Value *pe, *io;

    c = fs;
    for (s = 0; s < II; s++)
    {
        // Each configuration word is an array of PE objects
        JSON_Value *pe_list_val = json_value_init_array();
        JSON_Array *pe_list = json_value_get_array(pe_list_val);

        // IO Mapping Information
        JSON_Value *io_list_val = json_value_init_array();
        JSON_Array *io_list = json_value_get_array(io_list_val);

        for (i = 0; i < get_cgra_L(fs); i++)
        {
            currC = 0;
            for (j = 0; j < get_cgra_C(fs) - 0; j++)
            {
                if (!isPE(fs, i, j) && !isStreamPort(fs, i, j))
                    continue;

                if (!isPE(fs, i, j))
                {

                    io = create_io_object(c, d, i, j, *placed);
                    json_array_append_value(io_list, io);
                    continue;
                }
                pe = create_pe_object(c, d, i, j, *placed);
                json_array_append_value(pe_list, pe);
                currC++;
            }
            effC = effC > currC ? effC : currC;
            if (currC > 0)
                effR++;
        }

        // PE section header
        JSON_Value *config_word_val = json_value_init_object();
        JSON_Object *config_word_obj = json_value_get_object(config_word_val);

        json_object_set_value(config_word_obj, "PEs", pe_list_val);
        json_object_set_value(config_word_obj, "IOs", io_list_val);

        // Append this configuration word to CW array
        json_array_append_value(cw_arr, config_word_val);
        c = getNextModuloSlice(c);
    }

    json_object_set_number(map_obj, "II", II);
    json_object_set_number(map_obj, "Rows", effR);
    json_object_set_number(map_obj, "Cols", effC);
    json_object_set_number(map_obj, "VectorWidth", vectorWidth);

    // Assemble the structure
    json_object_set_value(map_obj, "Configuration Words", cw_val);
    json_object_set_value(root_obj, "Mapping Results", map_val);

    // Serialize to file
    char *jsonFilename = (char *)calloc(strlen(filename) + 6, sizeof(char));
    strncpy(jsonFilename, filename, strlen(filename));
    jsonFilename[strlen(filename)] = '.';
    jsonFilename[strlen(filename) + 1] = 'j';
    jsonFilename[strlen(filename) + 2] = 's';
    jsonFilename[strlen(filename) + 3] = 'o';
    jsonFilename[strlen(filename) + 4] = 'n';
    json_serialize_to_file_pretty(root_val, jsonFilename);

    // Clean up
    free(jsonFilename);
    json_value_free(root_val);

    printf("JSON file generated!\n");
    return 0;
}

JSON_Value *create_pe_arch_object(cgra *c, int i, int j, int II)
{

    JSON_Value *outer_val = json_value_init_object();
    JSON_Object *outer_obj = json_value_get_object(outer_val);

    JSON_Value *pe_val = json_value_init_object();
    JSON_Object *pe_obj = json_value_get_object(pe_val);

    json_object_set_number(pe_obj, "row", i);
    json_object_set_number(pe_obj, "col", j);
    /* json_object_set_number(pe_obj, "DataWidth", getDataWidth(c)); */
    json_object_set_number(pe_obj, "N_OutputRegisters", getNumOutputRegisters(c, i, j));
    json_object_set_number(pe_obj, "ConfigMemSize", II);

    // FU Operation list
    JSON_Value *fu_val = json_value_init_object();
    JSON_Object *fu_obj = json_value_get_object(fu_val);
    JSON_Value *fu_ops_val = json_value_init_array();
    JSON_Array *fu_ops = json_value_get_array(fu_ops_val);

    for (int k = OP_ADD; k < OP_MAX; k++)
    {
        if (peHasFunct(c, i, j, k))
        {
            json_array_append_string(fu_ops, get_operation(k));
        }
    }
    json_object_set_number(fu_obj, "Inputs", getNFUInputs(c, i, j));
    json_object_set_value(fu_obj, "Operations", fu_ops_val);
    json_object_set_value(pe_obj, "FunctionalUnit", fu_val);

    // Register File
    JSON_Value *rf_val = json_value_init_object(), *rfportarr = json_value_init_array(), *rfport = json_value_init_object();
    JSON_Object *rf_obj = json_value_get_object(rf_val), *rfport_obj = json_value_get_object(rfport);
    JSON_Array *rfport_arr = json_value_get_array(rfportarr);

    json_object_set_string(rfport_obj, "Destination", "FUMuxIns");
    json_object_set_number(rfport_obj, "Ports", getNRFRPMuxIn(c, i, j));
    json_array_append_value(rfport_arr, rfport);

    rfport = json_value_init_object();
    rfport_obj = json_value_get_object(rfport);

    json_object_set_string(rfport_obj, "Destination", "OutputRegisters");
    json_object_set_number(rfport_obj, "Ports", getNRFRPOR(c, i, j));
    json_array_append_value(rfport_arr, rfport);

    json_object_set_number(rf_obj, "RFSize", getRFSize(c, i, j));
    json_object_set_value(rf_obj, "Ports", rfportarr);

    json_object_set_value(pe_obj, "RegisterFile", rf_val);

    // Input Ports
    int lat;
    JSON_Value *connects_from_val, *inputs_val = json_value_init_array(), *from_pe;
    JSON_Object *connects_from_obj, *from_pe_obj;
    JSON_Array *inputs_arr = json_value_get_array(inputs_val);

    if ((lat = get_cgra_interconnect(c, i - 1, j, i, j)) < INFINITY)
    {
        if (lat > 0)
        {
            connects_from_val = json_value_init_object();
            connects_from_obj = json_value_get_object(connects_from_val);
            from_pe = json_value_init_object();
            from_pe_obj = json_value_get_object(from_pe);
            if (isPE(c, i - 1, j))
                json_object_set_string(from_pe_obj, "type", "PE");
            else if (!isOutputStreamPort(c, i - 1, j)) // no PE and no Stream Out => Stream In (or Stream I/O)
                json_object_set_string(from_pe_obj, "type", "Input");

            json_object_set_number(from_pe_obj, "row", i - 1);
            json_object_set_number(from_pe_obj, "col", j);
            json_object_set_value(connects_from_obj, "from", from_pe);
            json_object_set_string(connects_from_obj, "port", "North");
            json_array_append_value(inputs_arr, connects_from_val);
        }
    }
    if ((lat = get_cgra_interconnect(c, i, j - 1, i, j)) < INFINITY)
    {
        if (lat > 0)
        {
            connects_from_val = json_value_init_object();
            connects_from_obj = json_value_get_object(connects_from_val);
            from_pe = json_value_init_object();
            from_pe_obj = json_value_get_object(from_pe);
            if (isPE(c, i, j - 1))
                json_object_set_string(from_pe_obj, "type", "PE");
            else if (!isOutputStreamPort(c, i, j - 1)) // no PE and no Stream Out => Stream In (or Stream I/O)
                json_object_set_string(from_pe_obj, "type", "Input");

            json_object_set_number(from_pe_obj, "row", i);
            json_object_set_number(from_pe_obj, "col", j - 1);
            json_object_set_value(connects_from_obj, "from", from_pe);
            json_object_set_string(connects_from_obj, "port", "West");
            json_array_append_value(inputs_arr, connects_from_val);
        }
    }
    if ((lat = get_cgra_interconnect(c, i + 1, j, i, j)) < INFINITY)
    {
        if (lat > 0)
        {
            connects_from_val = json_value_init_object();
            connects_from_obj = json_value_get_object(connects_from_val);
            from_pe = json_value_init_object();
            from_pe_obj = json_value_get_object(from_pe);
            if (isPE(c, i + 1, j))
                json_object_set_string(from_pe_obj, "type", "PE");
            else if (!isOutputStreamPort(c, i + 1, j)) // no PE and no Stream Out => Stream In (or Stream I/O)
                json_object_set_string(from_pe_obj, "type", "Input");

            json_object_set_number(from_pe_obj, "row", i + 1);
            json_object_set_number(from_pe_obj, "col", j);
            json_object_set_value(connects_from_obj, "from", from_pe);
            json_object_set_string(connects_from_obj, "port", "South");
            json_array_append_value(inputs_arr, connects_from_val);
        }
    }
    if ((lat = get_cgra_interconnect(c, i, j + 1, i, j)) < INFINITY)
    {
        if (lat > 0)
        {
            connects_from_val = json_value_init_object();
            connects_from_obj = json_value_get_object(connects_from_val);
            from_pe = json_value_init_object();
            from_pe_obj = json_value_get_object(from_pe);
            if (isPE(c, i, j + 1))
                json_object_set_string(from_pe_obj, "type", "PE");
            else if (!isOutputStreamPort(c, i, j + 1)) // no PE and no Stream Out => Stream In (or Stream I/O)
                json_object_set_string(from_pe_obj, "type", "Input");

            json_object_set_number(from_pe_obj, "row", i);
            json_object_set_number(from_pe_obj, "col", j + 1);
            json_object_set_value(connects_from_obj, "from", from_pe);
            json_object_set_string(connects_from_obj, "port", "East");
            json_array_append_value(inputs_arr, connects_from_val);
        }
    }
    json_object_set_value(pe_obj, "Input Ports", inputs_val);

    // Output Ports
    JSON_Value *connects_to_val, *outputs_val = json_value_init_array(), *to_pe;
    JSON_Object *connects_to_obj, *to_pe_obj;
    JSON_Array *outputs_arr = json_value_get_array(outputs_val);

    if ((lat = get_cgra_interconnect(c, i, j, i - 1, j)) < INFINITY)
    {
        if (lat > 0)
        {
            connects_to_val = json_value_init_object();
            connects_to_obj = json_value_get_object(connects_to_val);
            to_pe = json_value_init_object();
            to_pe_obj = json_value_get_object(to_pe);
            if (isPE(c, i - 1, j))
                json_object_set_string(to_pe_obj, "type", "PE");
            else if (isOutputStreamPort(c, i - 1, j))
                json_object_set_string(to_pe_obj, "type", "Output");

            json_object_set_number(to_pe_obj, "row", i - 1);
            json_object_set_number(to_pe_obj, "col", j);
            json_object_set_value(connects_to_obj, "to", to_pe);
            json_object_set_string(connects_to_obj, "port", "North");
            json_array_append_value(outputs_arr, connects_to_val);
        }
    }
    if ((lat = get_cgra_interconnect(c, i, j, i, j - 1)) < INFINITY)
    {
        if (lat > 0)
        {
            connects_to_val = json_value_init_object();
            connects_to_obj = json_value_get_object(connects_to_val);
            to_pe = json_value_init_object();
            to_pe_obj = json_value_get_object(to_pe);
            if (isPE(c, i, j - 1))
                json_object_set_string(to_pe_obj, "type", "PE");
            else if (isOutputStreamPort(c, i, j - 1))
                json_object_set_string(to_pe_obj, "type", "Output");

            json_object_set_number(to_pe_obj, "row", i);
            json_object_set_number(to_pe_obj, "col", j - 1);
            json_object_set_value(connects_to_obj, "to", to_pe);
            json_object_set_string(connects_to_obj, "port", "West");
            json_array_append_value(outputs_arr, connects_to_val);
        }
    }
    if ((lat = get_cgra_interconnect(c, i, j, i + 1, j)) < INFINITY)
    {
        if (lat > 0)
        {
            connects_to_val = json_value_init_object();
            connects_to_obj = json_value_get_object(connects_to_val);
            to_pe = json_value_init_object();
            to_pe_obj = json_value_get_object(to_pe);
            if (isPE(c, i + 1, j))
                json_object_set_string(to_pe_obj, "type", "PE");
            else if (isOutputStreamPort(c, i + 1, j))
                json_object_set_string(to_pe_obj, "type", "Output");

            json_object_set_number(to_pe_obj, "row", i + 1);
            json_object_set_number(to_pe_obj, "col", j);
            json_object_set_value(connects_to_obj, "to", to_pe);
            json_object_set_string(connects_to_obj, "port", "South");
            json_array_append_value(outputs_arr, connects_to_val);
        }
    }
    if ((lat = get_cgra_interconnect(c, i, j, i, j + 1)) < INFINITY)
    {
        if (lat > 0)
        {
            connects_to_val = json_value_init_object();
            connects_to_obj = json_value_get_object(connects_to_val);
            to_pe = json_value_init_object();
            to_pe_obj = json_value_get_object(to_pe);
            if (isPE(c, i, j + 1))
                json_object_set_string(to_pe_obj, "type", "PE");
            else if (isOutputStreamPort(c, i, j + 1))
                json_object_set_string(to_pe_obj, "type", "Output");

            json_object_set_number(to_pe_obj, "row", i);
            json_object_set_number(to_pe_obj, "col", j + 1);
            json_object_set_value(connects_to_obj, "to", to_pe);
            json_object_set_string(connects_to_obj, "port", "East");
            json_array_append_value(outputs_arr, connects_to_val);
        }
    }
    json_object_set_value(pe_obj, "Output Ports", outputs_val);

    json_object_set_value(outer_obj, "PE", pe_val);

    return outer_val;
}

JSON_Value *create_io_arch_object(cgra *c, int i, int j)
{

    int type = 0, conn = -1, ii, jj;

    JSON_Value *outer_val = json_value_init_object();
    JSON_Object *outer_obj = json_value_get_object(outer_val);

    JSON_Value *io_val = json_value_init_object();
    JSON_Object *io_obj = json_value_get_object(io_val);

    type = isOutputStreamPort(c, i, j) ? 2 : 1;

    if (type == 1)
        json_object_set_string(io_obj, "type", "Input\0");
    else if (type == 2)
        json_object_set_string(io_obj, "type", "Output\0");

    json_object_set_number(io_obj, "row", i);
    json_object_set_number(io_obj, "col", j);

    JSON_Value *connects_to_val = json_value_init_object();
    JSON_Object *connects_to_obj = json_value_get_object(connects_to_val);

    if (type == 1)
    {
        ii = i + 1;
        jj = j;
        conn = getconnLat(c, i + 1, j, i, j);
        if (conn < 0 || conn == INFINITY)
        {
            conn = getconnLat(c, i, j + 1, i, j);
            ii = i;
            jj = j + 1;
        }
        if (conn < 0 || conn == INFINITY)
        {
            conn = getconnLat(c, i, j - 1, i, j);
            ii = i;
            jj = j - 1;
        }
        if (conn < 0 || conn == INFINITY)
        {
            conn = getconnLat(c, i - 1, j, i, j);
            ii = i - 1;
            jj = j;
        }
    }
    else if (type == 2)
    {
        ii = i + 1;
        jj = j;
        conn = getconnLat(c, i, j, i + 1, j);
        if (conn < 0 || conn == INFINITY)
        {
            conn = getconnLat(c, i, j, i, j + 1);
            ii = i;
            jj = j + 1;
        }
        if (conn < 0 || conn == INFINITY)
        {
            conn = getconnLat(c, i, j, i, j - 1);
            ii = i;
            jj = j - 1;
        }
        if (conn < 0 || conn == INFINITY)
        {
            conn = getconnLat(c, i, j, i - 1, j);
            ii = i - 1;
            jj = j;
        }
    }
    if (conn > -1 && conn < INFINITY)
    {
        json_object_set_number(connects_to_obj, "row", ii);
        json_object_set_number(connects_to_obj, "col", jj);
        if (type == 2)
            json_object_set_value(io_obj, "from", connects_to_val);
        else if (type == 1)
            json_object_set_value(io_obj, "to", connects_to_val);
    }
    if (conn == -1)
        json_object_set_string(io_obj, "ERROR", "NO CONN");

    json_object_set_value(outer_obj, "IO", io_val);

    return outer_val;
}

int exportArch(cgra *fs, char *filename, int II, int vectorWidth)
{

    int i, j, effR = 0, effC = 0, currC;

    // Create "Architecture" object
    JSON_Value *arch_val = json_value_init_object();
    JSON_Object *arch = json_value_get_object(arch_val);

    JSON_Value *pe /* , *io */;

    JSON_Value *pe_list_val = json_value_init_array(), *io_list_val = json_value_init_array();
    JSON_Array *pe_list = json_value_get_array(pe_list_val), *io_list = json_value_get_array(io_list_val);
    for (i = 0; i < get_cgra_L(fs); i++)
    {
        currC = 0;
        for (j = 0; j < get_cgra_C(fs); j++)
        {
            if (!isPE(fs, i, j) && !isStreamPort(fs, i, j))
                continue;

            if (!isPE(fs, i, j))
            {
                pe = create_io_arch_object(fs, i, j);
                json_array_append_value(io_list, pe);
                continue;
            }
            pe = create_pe_arch_object(fs, i, j, II);
            json_array_append_value(pe_list, pe);
            currC++;
        }
        effC = effC > currC ? effC : currC;
        if (currC > 0)
            effR++;
    }

    json_object_set_number(arch, "Rows", effR);
    json_object_set_number(arch, "Cols", effC);
    json_object_set_number(arch, "DataWidth", getDataWidth(fs));
    json_object_set_number(arch, "VectorWidth", vectorWidth);
    json_object_set_value(arch, "Architecture", pe_list_val);
    json_object_set_value(arch, "IOs", io_list_val);
    // Serialize to file
    char *jsonFilename = (char *)calloc(strlen(filename) + 6, sizeof(char));
    strncpy(jsonFilename, filename, strlen(filename));
    jsonFilename[strlen(filename)] = '.';
    jsonFilename[strlen(filename) + 1] = 'j';
    jsonFilename[strlen(filename) + 2] = 's';
    jsonFilename[strlen(filename) + 3] = 'o';
    jsonFilename[strlen(filename) + 4] = 'n';
    json_serialize_to_file_pretty(arch_val, jsonFilename);

    // Clean up
    free(jsonFilename);
    json_value_free(arch_val);

    printf("JSON file generated!\n");
    return 0;
}