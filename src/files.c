#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include "dfg.h"
#include "cgra.h"
#include "ops.h"

#define MAX_OP_NAME_SIZE 15
#define MAX_INSTR_NAME_LEN 20

dfg *import_dfg(char *filename)
{
    FILE *fp = fopen(filename, "r+");
    if (fp == NULL)
        return NULL;

    int i, j, N, NConsts = 0, lat, n_inputs, n_outputs, n_recurrences, n_consts, const_val, dep, total_recs = 0;
    char op[MAX_OP_NAME_SIZE], name[MAX_INSTR_NAME_LEN];
    dfg_instr **dfgi, **dfgc;
    dfg *d;

    if (fscanf(fp, "%d", &N) == 0)
    {
        return NULL;
    }

    dfgi = (dfg_instr **)calloc(N, sizeof(dfg_instr *));
    dfgc = (dfg_instr **)calloc(N, sizeof(dfg_instr *));
    int *isConst = (int *)calloc(N, sizeof(int));

    // Create Instructions
    for (i = 0; i < N; i++)
    {
        memset(name, 0, MAX_INSTR_NAME_LEN);
        memset(op, 0, MAX_OP_NAME_SIZE);

        // This is probably unsafe code, and there is a bug with storing names!
        fscanf(fp, "%s %s %d %d %d %d %d %d", name, op, &lat, &n_inputs, &n_outputs, &n_recurrences, &n_consts, &const_val);
        //printf("line: %s | %s | %d | %d | %d | %d | %d\n", name, op, lat, n_inputs, n_outputs, n_recurrences, n_consts);

        // OP is a constant
        if (!strcmp(op, "CONST")){
            dfgc[i] = create_instr(name, op, lat, n_inputs - n_consts, n_outputs, n_recurrences, n_consts, i == 0);
            set_const_val(dfgc[i], const_val);
            NConsts++;
            isConst[i] = 1;
        }
        else
            dfgi[i] = create_instr(name, op, lat, n_inputs - n_consts, n_outputs, n_recurrences, n_consts, i == 0);
        total_recs += n_recurrences;
    }

    // Set Dependencies
    for (i = 0; i < N; i++)
    {
        if (dfgi[i] == NULL)
            continue;
        // Set input dependencies
        for (j = 0, n_consts = 0, n_inputs = 0; j < get_n_inputs(dfgi[i]) + get_n_consts(dfgi[i]); j++)
        {
            fscanf(fp, "%d", &dep);
            if (isConst[dep - 1])
                set_const(dfgi[i], dfgc[dep - 1], n_consts++);
            else
                set_input(dfgi[i], dfgi[dep - 1], n_inputs++);
        }
        // Set output dependencies
        for (j = 0; j < get_n_outputs(dfgi[i]); j++)
        {
            fscanf(fp, "%d", &dep);
            set_output(dfgi[i], dfgi[dep - 1], j);
        }
    }
    for (i = N - NConsts; i < N; i++){
        for (j = 0; j < get_n_outputs(dfgc[i]); j++){
            fscanf(fp, "%d", &dep);
            set_output(dfgc[i], dfgi[dep - 1], j);        
        }
    }

    free(isConst);

    // Set Recurrences
    for (i = 0; i < total_recs; i++)
    {
        int idx, dist;
        fscanf(fp, "%d %d %d", &idx, &dep, &dist);
        if (set_recurrence(dfgi[idx - 1], dfgi[dep - 1], i, dist) == 0)
        {
            return NULL;
        }
    }
    fclose(fp);

    dfg_instr **instrs = (dfg_instr **)malloc((N - NConsts) * sizeof(dfg_instr *));
    dfg_instr **constants = (dfg_instr **)malloc(NConsts * sizeof(dfg_instr *));

    int ni = 0, nc = 0;
    for (i = 0; i < N; i++){
        if (dfgi[i] != NULL)
            instrs[ni++] = dfgi[i];
        else // constant
            constants[nc++] = dfgc[i];
    }
    free(dfgi);
    free(dfgc);

    d = create_dfg(instrs, ni, constants, nc);

    return d;
}

int get_config_type(char *type)
{

    if (!strcmp(type, "HORIZONTAL"))
        return HORIZONTAL;
    else if (!strcmp(type, "VERTICAL"))
        return VERTICAL;
    else if (!strcmp(type, "DIAGONAL"))
        return DIAGONAL;
    else if (!strcmp(type, "ADJACENT"))
        return ADJACENT;
    else if (!strcmp(type, "LEFT_TO_RIGHT"))
        return LEFT_TO_RIGHT;
    else if (!strcmp(type, "RIGHT_TO_LEFT"))
        return RIGHT_TO_LEFT;
    else if (!strcmp(type, "UP_TO_DOWN"))
        return UP_TO_DOWN;
    else if (!strcmp(type, "DOWN_TO_UP"))
        return DOWN_TO_UP;
    else if (!strcmp(type, "DIAGONAL_SE"))
        return DIAGONAL_SE;
    else if (!strcmp(type, "DIAGONAL_NE"))
        return DIAGONAL_NE;
    else if (!strcmp(type, "DIAGONAL_NW"))
        return DIAGONAL_NW;
    else if (!strcmp(type, "DIAGONAL_SW"))
        return DIAGONAL_SW;
    else if (!strcmp(type, "WRAP_AROUND_LR"))
        return WRAP_AROUND_LR;
    else if (!strcmp(type, "WRAP_AROUND_RL"))
        return WRAP_AROUND_RL;
    else if (!strcmp(type, "WRAP_AROUND_UD"))
        return WRAP_AROUND_UD;
    else if (!strcmp(type, "WRAP_AROUND_DU"))
        return WRAP_AROUND_DU;
    else if (!strcmp(type, "STREAM_CONN"))
        return STREAM_CONN;

    return -1;
}

/**
 * Imports the cgra file, this time with the formatting from the frontend
 */
cgra *new_import_cgra(char *filename)
{

    if (filename == NULL || strlen(filename) < 6)
    {
        return NULL;
    }

    size_t len = strlen(filename);

    // Check if the filename ends with ".mpa"
    if (strcmp(&filename[len - 5], ".cmpa") != 0)
    {
        return NULL;
    }

    FILE *fp = fopen(filename, "r+");

    if (fp == NULL)
        return NULL;

    int i, j, l, c, val, ops, pes = 0, n_or, rf, cu, pplnStages, se_ld, se_st, dw, rfrpMuxIn, rfrpOR;
    char op[MAX_OP_NAME_SIZE];
    cgra *new;

    fscanf(fp, "%d %d %d %d %d", &l, &c, &se_ld, &se_st, &dw);

    new = create_cgra(l, c, se_ld, se_st, dw);

    ops = 0;
    for (i = 0; i < l; i++)
        for (j = 0; j < c; j++)
        {
            fscanf(fp, "%d", &val);
            if (val == -2 || val == -3 || val == -4)
            { // (streaming port)
                if (val != -4)
                    val *= -1;
                // set as unmapped tile
                set_cgra_value(new, 0, i, j);
                set_cgra_tile_funct(new, i, j, val);
                if (val != 3)
                    initOutputRegisters(new, i, j, 1, 0);
                else
                    initOutputRegisters(new, i, j, 0, 0); // do not add output registers to output streaming ports
            }
            else if (val > -1) // PE with some defined functions
            {
                // set as unmapped tile
                set_cgra_value(new, 0, i, j);
                ops += val;
                pes++;
            }
            else
                remove_pe_from_cgra(new, i, j);
        }

    for (i = 0; i < pes; i++)
    {
        fscanf(fp, "%d %d %d %d %d %d %d %d", &l, &c, &n_or, &rf, &rfrpMuxIn, &rfrpOR, &cu, &pplnStages);
        initOutputRegisters(new, l, c, n_or, rfrpOR);
        initLocalRegisterFile(new, l, c, rf, rfrpMuxIn);
        initConstantUnits(new, l, c, cu);
        setPEPipelineStages(new, l, c, pplnStages);
    }

    for (i = 0; i < ops; i++)
    {
        fscanf(fp, "%d %d %s", &l, &c, op);
        set_cgra_tile_funct(new, l, c, get_operation_index(op));
    }

    int scan, lat, x1, y1, x2, y2;
    char type[20];
    memset(type, 0, 20 * sizeof(char));

    while ((scan = fscanf(fp, "%s %d %d %d %d %d", type, &y1, &x1, &y2, &x2, &lat)) == 2 || scan == 6)
    {
        if (scan == 2)
        {
            lat = y1; // latency value will have been initially read to variable y1
            set_cgra_interconnects(new, get_config_type(type), lat);
        }
        else if (scan == 6 && !strcmp(type, "CONN"))
        {
            set_cgra_interconnect(new, y1, x1, y2, x2, lat);
        }
    }

    fclose(fp);

    return new;
}

int import_cgra_config(char *filename, cgra *c)
{

    FILE *fp = fopen(filename, "r+");

    if (fp == NULL)
        return 0;

    int lat;
    char type[20];
    memset(type, 0, 20 * sizeof(char));

    while (fscanf(fp, "%s %d", type, &lat) == 2)
    {
        set_cgra_interconnects(c, get_config_type(type), lat);
    }

    fclose(fp);
    return 1;
}

// Function to trim leading and trailing whitespace
void trim_whitespace(char *str)
{
    // Trim leading whitespace
    char *start = str;
    while (isspace((unsigned char)*start))
    {
        start++;
    }

    // Trim trailing whitespace
    char *end = start + strlen(start) - 1;
    while (end > start && isspace((unsigned char)*end))
    {
        *end = '\0';
        end--;
    }

    // Shift the trimmed string back to the beginning
    if (start != str)
    {
        memmove(str, start, strlen(start) + 1);
    }
}

void to_uppercase(char *str)
{
    if (str == NULL)
        return;
    while (*str)
    {
        *str = toupper((unsigned char)*str);
        str++;
    }
}

int max_array(int arr[], size_t size)
{
    int max_val = arr[0];
    for (size_t i = 1; i < size; i++)
    {
        if (arr[i] > max_val)
        {
            max_val = arr[i];
        }
    }
    return max_val;
}

float max_array_flt(float arr[], size_t size)
{
    float max_val = arr[0];
    for (size_t i = 1; i < size; i++)
    {
        if (arr[i] > max_val)
        {
            max_val = arr[i];
        }
    }
    return max_val;
}

int max_array_idx(int arr[], size_t size)
{
    int max_val = arr[0], idx = -1;
    for (size_t i = 1; i < size; i++)
    {
        if (arr[i] > max_val)
        {
            max_val = arr[i];
            idx = i;
        }
    }
    return idx;
}

float array_sum(float arr[], size_t size){
    float val = arr[0];
    for (size_t i = 1; i < size; i++)
    {
        val += arr[i];
    }
    return val;
}

int array_sum_int(int arr[], size_t size){
    int val = arr[0];
    for (size_t i = 1; i < size; i++)
    {
        val += arr[i];
    }
    return val;
}

float array_avg(float arr[], size_t size){
    return array_sum(arr, size) / size;
}

float array_variance(float arr[], size_t size){

    float mean = 0.0, m2 = 0.0;
    for (int i = 0; i < size; i++) {
        double delta = arr[i] - mean;
        mean += delta / (i + 1);
        m2 += delta * (arr[i] - mean);
    }
    
    return m2 / size;  // Population standard deviation
}

float array_std_dev(float arr[], size_t size){
    return sqrt(array_variance(arr, size));

}

void copyArray(int copy[], int target[], size_t size){
    
    int i;
    for (i = 0; i < size; i++)
        copy[i] = target[i];
}
