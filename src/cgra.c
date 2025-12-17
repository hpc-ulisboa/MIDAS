#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <stdint.h>
#include <time.h>
#include "dfg.h"
#include "pqueue.h"
#include "ops.h"

#define FUNCTS ((MAX_OPS + 63) / 64) // number of possible PE functions (different "PE types")
#define SET_FUNCT(fu, op_index) ((fu)->functs[(op_index) / 64] |= (1ULL << ((op_index) % 64)))
#define RMV_FUNCT(fu, op_index) ((fu)->functs[(op_index) / 64] &= ~(1ULL << ((op_index) % 64)))
#define HAS_FUNCT(fu, op_index) (((fu)->functs[(op_index) / 64] & (1ULL << ((op_index) % 64))) != 0)

#define FREE 0
#define NOT_YET_COMMITTED -1
#define IN_USE 1

#define POWER_OFF 0
#define POWER_ON 1

#define PATIENCE 1

#define INFINITY __INT_MAX__

// space-time point
typedef struct _stp
{
    int val;
    int t;
} stp;

// defines a number of read ports from the LRF to a given structure
typedef struct _rfReadPorts
{
    int limit;
    int counter;
    int *val;
    int *t;
} rfReadPorts;

typedef struct _pe
{
    int tile;
    uint64_t functs[FUNCTS]; // {FULL, LSU, Stream Port, ALU, ADD, MUL, SUB, DIV}
    int fu_NInputs;          // Number of FU Inputs (FOR NOW DEFAULTED TO 2)
    dfg_instr *instr;        // DFG instruction that maps to it
    int powerOn;             // 1 if PE is powered on, 0 if powered off
    int *registerFile;       // Register File reservation table: for each position, 0 if free, or id > 0 if reserved by an instruction with a given id
    int RFsize;
    int *constantUnits; // Constant Units reservation table: for each position, 0 if free, or id > 0 if reserved by a constant with a given id
    int CUsize;
    int NumOutputRegisters; // number of output registers
    int *outputRegisters;
    int *outputRegisterTimes;
    int pipelineStages;

    int registerFileAccess;         // auxiliary variable for routing (RF is accessed @ this cycle for storing a new value? 1 : 0)
    int *registerFileTime;          // auxiliary variable for routing (stores clock cycles associated with the LRF entries)
    int **registerFileReservations; // auxiliary variable for routing (stores the opIDs that made an LRF reservation)
    int **constantUnitReservations; // auxiliary variable for constant reservations (stores the opIDs that made a CU reservation)

    rfReadPorts rfPortsToOutputRegisters; // defines the RF output ports linked to output registers
    rfReadPorts rfPortsToInputMuxes;      // defines the RF output ports linked to the FU (FU's input muxes)
} pe;

typedef struct _cgra
{
    pe ***grid; // PE Tile grid

    // Interconnects
    int **lats;        // PE interconnect adjacency matrix
    int ***new_states; // states matrix. stores the operations that are using the connection (can only be used to route 1 value at a time)

    // PE shell multiplexers, essentially
    stp **state_src; // auxiliary states matrix. for each connection state, it stores the ID of the corresponding source output register

    int configs[17]; // interconnect configurations flags

    int L;
    int C;

    int MII;
    int execution_time;
    int num_contexts_for_one_iteration;
    int mapping_flag; // identifies the algorithm used for mapping. Set as 0 (no algorithm) as default (not yet mapped).

    int st_trghpt;            // store memory throughput
    int ld_thrgpt;            // load memory throughput
    int se_ld;                // Streaming Engine Load Bandwidth (in Bytes/cycle)
    int se_st;                // Streaming Engine Store Bandwidth (in Bytes/cycle)
    int data_width;           // Data Width (in Bytes)
    struct _cgra *next_slice; // next slice (in time)
    struct _cgra *prev_slice; // previous slice (in time)
} cgra;

/**************************************************
 * PE Functions
 *************************************************/
pe *create_pe()
{

    pe *new = (pe *)malloc(sizeof(pe));

    new->tile = 0;
    new->instr = NULL;
    new->powerOn = POWER_ON; // Turned on by default
    new->RFsize = 0;
    new->registerFile = NULL;
    new->CUsize = 0;
    new->constantUnits = NULL;
    new->NumOutputRegisters = 1; // By default, assume 1 output register (minimum)
    new->outputRegisters = NULL;
    new->outputRegisterTimes = NULL;
    new->pipelineStages = 1;

    new->fu_NInputs = 2;

    new->registerFileAccess = 0;
    new->registerFileTime = NULL;
    new->registerFileReservations = NULL;
    new->constantUnitReservations = NULL;

    new->rfPortsToOutputRegisters.counter = 0;
    new->rfPortsToOutputRegisters.limit = 0;
    new->rfPortsToOutputRegisters.val = NULL;
    new->rfPortsToOutputRegisters.t = NULL;
    new->rfPortsToInputMuxes.counter = 0;
    new->rfPortsToInputMuxes.limit = 0;
    new->rfPortsToInputMuxes.val = NULL;
    new->rfPortsToInputMuxes.t = NULL;

    int i;

    for (i = 0; i < FUNCTS; i++)
        new->functs[i] = 0;

    return new;
}

void set_pe_tile(pe *target, int val)
{
    target->tile = val;
}

void init_pe_n_output_registers(pe *target, int n, int rfrp)
{
    target->NumOutputRegisters = n;
    target->outputRegisters = (int *)calloc(n, sizeof(int));
    target->outputRegisterTimes = (int *)calloc(n, sizeof(int));

    // Define the number of LRF Output Ports
    target->rfPortsToOutputRegisters.limit = rfrp;
    target->rfPortsToOutputRegisters.val = (int *)calloc(rfrp, sizeof(int));
    target->rfPortsToOutputRegisters.t = (int *)calloc(rfrp, sizeof(int));
}

void delete_pe_output_registers(pe *target)
{
    free(target->outputRegisters);
    free(target->outputRegisterTimes);

    if (target->rfPortsToOutputRegisters.limit > 0)
    {
        free(target->rfPortsToOutputRegisters.val);
        free(target->rfPortsToOutputRegisters.t);
    }
}

void init_pe_registerFile(pe *target, int rfsize, int rfrp)
{
    int i;
    target->RFsize = rfsize;
    target->registerFile = (int *)calloc(rfsize, sizeof(int));
    target->registerFileTime = (int *)calloc(rfsize, sizeof(int));
    target->registerFileReservations = (int **)malloc(rfsize * sizeof(int *));
    for (i = 0; i < rfsize; i++)
    {
        target->registerFileReservations[i] = calloc(8, sizeof(int));
    }

    // Define the number of LRF Output Ports directed at the FU
    target->rfPortsToInputMuxes.limit = rfrp;
    target->rfPortsToInputMuxes.val = (int *)calloc(rfrp, sizeof(int));
    target->rfPortsToInputMuxes.t = (int *)calloc(rfrp, sizeof(int));
}

void delete_pe_registerFile(pe *p)
{
    if (p != NULL && p->registerFile != NULL)
        free(p->registerFile);
    if (p != NULL && p->registerFileTime != NULL)
        free(p->registerFileTime);
    if (p != NULL && p->registerFileReservations != NULL)
    {
        for (int i = 0; i < p->RFsize; i++)
            free(p->registerFileReservations[i]);
        free(p->registerFileReservations);
    }

    if (p->rfPortsToInputMuxes.limit > 0)
    {
        free(p->rfPortsToInputMuxes.val);
        free(p->rfPortsToInputMuxes.t);
    }
}

void init_pe_constantUnits(pe *target, int cusize)
{
    int i;
    target->CUsize = cusize;
    target->constantUnits = (int *)calloc(cusize, sizeof(int));
    target->constantUnitReservations = (int **)malloc(cusize * sizeof(int *));
    for (i = 0; i < cusize; i++)
    {
        target->constantUnitReservations[i] = calloc(8, sizeof(int));
    }
}

pe *copy_pe_params(pe *target)
{
    if (target == NULL)
        return NULL;

    pe *copy = (pe *)malloc(sizeof(pe));
    if (copy == NULL)
        return NULL;

    // Basic scalar fields
    copy->tile = target->tile;
    copy->instr = NULL;  // instruction not copied
    copy->powerOn = target->powerOn;
    copy->pipelineStages = target->pipelineStages;
    copy->fu_NInputs = target->fu_NInputs;
    copy->registerFileAccess = target->registerFileAccess;

    // Functional units
    for (int i = 0; i < FUNCTS; i++)
        copy->functs[i] = target->functs[i];

    // Output Registers
    copy->NumOutputRegisters = target->NumOutputRegisters;
    if (copy->NumOutputRegisters > 0)
    {
        copy->outputRegisters = (int *)calloc(copy->NumOutputRegisters, sizeof(int));
        copy->outputRegisterTimes = (int *)calloc(copy->NumOutputRegisters, sizeof(int));
    }
    else
    {
        copy->outputRegisters = NULL;
        copy->outputRegisterTimes = NULL;
    }

    // Register File
    copy->RFsize = target->RFsize;
    if (copy->RFsize > 0)
    {
        copy->registerFile = (int *)calloc(copy->RFsize, sizeof(int));
        copy->registerFileTime = (int *)calloc(copy->RFsize, sizeof(int));
        copy->registerFileReservations = (int **)calloc(copy->RFsize, sizeof(int *));
        for (int i = 0; i < copy->RFsize; i++)
        {
            copy->registerFileReservations[i] = (int *)calloc(8, sizeof(int));
        }
    }
    else
    {
        copy->registerFile = NULL;
        copy->registerFileTime = NULL;
        copy->registerFileReservations = NULL;
    }

    // Constant Units
    copy->CUsize = target->CUsize;
    if (copy->CUsize > 0)
    {
        copy->constantUnits = (int *)calloc(copy->CUsize, sizeof(int));
        copy->constantUnitReservations = (int **)calloc(copy->CUsize, sizeof(int *));
        for (int i = 0; i < copy->CUsize; i++)
        {
            copy->constantUnitReservations[i] = (int *)calloc(8, sizeof(int));
        }
    }
    else
    {
        copy->constantUnits = NULL;
        copy->constantUnitReservations = NULL;
    }

    // RF Ports to Input Muxes
    copy->rfPortsToInputMuxes.limit = target->rfPortsToInputMuxes.limit;
    copy->rfPortsToInputMuxes.counter = 0;
    if (copy->rfPortsToInputMuxes.limit > 0)
    {
        copy->rfPortsToInputMuxes.val = (int *)calloc(copy->rfPortsToInputMuxes.limit, sizeof(int));
        copy->rfPortsToInputMuxes.t = (int *)calloc(copy->rfPortsToInputMuxes.limit, sizeof(int));
    }
    else
    {
        copy->rfPortsToInputMuxes.val = NULL;
        copy->rfPortsToInputMuxes.t = NULL;
    }

    // RF Ports to Output Registers
    copy->rfPortsToOutputRegisters.limit = target->rfPortsToOutputRegisters.limit;
    copy->rfPortsToOutputRegisters.counter = 0;
    if (copy->rfPortsToOutputRegisters.limit > 0)
    {
        copy->rfPortsToOutputRegisters.val = (int *)calloc(copy->rfPortsToOutputRegisters.limit, sizeof(int));
        copy->rfPortsToOutputRegisters.t = (int *)calloc(copy->rfPortsToOutputRegisters.limit, sizeof(int));
    }
    else
    {
        copy->rfPortsToOutputRegisters.val = NULL;
        copy->rfPortsToOutputRegisters.t = NULL;
    }

    return copy;
}

void delete_pe(pe *p)
{
    if (p != NULL && p->constantUnits != NULL)
        free(p->constantUnits);
    if (p != NULL && p->registerFile != NULL)
        free(p->registerFile);
    if (p != NULL && (p->registerFileTime != NULL || p->RFsize > 0))
        free(p->registerFileTime);
    if (p != NULL && (p->registerFileReservations != NULL || p->RFsize > 0))
    {
        for (int i = 0; i < p->RFsize; i++)
            free(p->registerFileReservations[i]);
        free(p->registerFileReservations);
    }
    if (p != NULL && (p->constantUnitReservations != NULL || p->CUsize > 0))
    {
        for (int i = 0; i < p->CUsize; i++)
            free(p->constantUnitReservations[i]);
        free(p->constantUnitReservations);
    }
    if (p != NULL)
    {
        free(p->outputRegisters);
        free(p->outputRegisterTimes);
    }

    if (p != NULL)
    {
        free(p->rfPortsToInputMuxes.val);
        free(p->rfPortsToInputMuxes.t);
    }

    if (p != NULL)
    {
        free(p->rfPortsToOutputRegisters.val);
        free(p->rfPortsToOutputRegisters.t);
    }

    if (p != NULL)
        free(p);
}

/**************************************************
 * CGRA Functions
 *************************************************/

/**
 * Creates the CGRA Data Structure
 */
cgra *create_cgra(int L, int C, int se_ld, int se_st, int dw)
{

    int i, j;
    cgra *new = (cgra *)malloc(sizeof(cgra));
    new->L = L;
    new->C = C;
    new->st_trghpt = se_st / dw;
    new->ld_thrgpt = se_ld / dw;
    new->se_ld = se_ld;
    new->se_st = se_st;
    new->data_width = dw;
    new->next_slice = NULL;
    new->prev_slice = NULL;

    // PE Grid
    new->grid = (pe ***)malloc(L * sizeof(pe **));

    for (i = 0; i < L; i++)
    {
        new->grid[i] = (pe **)malloc(C * sizeof(pe *));
        for (j = 0; j < C; j++)
            new->grid[i][j] = create_pe();
    }

    for (i = 0; i < 17; i++)
        new->configs[i] = 0;

    new->execution_time = 0;
    new->num_contexts_for_one_iteration = 0;
    new->mapping_flag = 0; // Not yet mapped

    // IC Grid
    new->lats = (int **)malloc(L * C * sizeof(int *));
    new->new_states = (int ***)malloc(L * C * sizeof(int **));

    new->state_src = (stp **)malloc(L * C * sizeof(stp *));

    for (i = 0; i < L * C; i++)
    {
        new->lats[i] = (int *)malloc(L * C * sizeof(int));
        new->new_states[i] = (int **)malloc(L * C * sizeof(int *));
        new->state_src[i] = (stp *)calloc(L * C, sizeof(stp));

        for (j = 0; j < L * C; j++)
        {
            new->lats[i][j] = INFINITY; // No connections yet
            new->new_states[i][j] = (int *)calloc(8, sizeof(int));
        }
    }

    return new;
}

/**
 * Sets cgra tile to val on tile with coordinates (l, c)
 */

void set_cgra_value(cgra *t, int val, int l, int c)
{
    t->grid[l][c]->tile = val;
}

void set_cgra_tile(cgra *t, int l, int c, dfg_instr *curr)
{
    t->grid[l][c]->instr = curr;
}

int get_cgra_tile_value(cgra *t, int l, int c)
{
    if (t->grid[l][c] == NULL) // Non-existent PE
        return -1;
    return t->grid[l][c]->tile;
}

dfg_instr *get_cgra_tile(cgra *t, int l, int c)
{
    if (t->grid[l][c] == NULL)
        return NULL;
    return t->grid[l][c]->instr;
}

int isOutputStreamPort(cgra *c, int i, int j)
{
    if (c->grid[i][j] == NULL)
        return 0;
    return HAS_FUNCT(c->grid[i][j], OP_STREAM_OUT);
}

int isInputStreamPort(cgra *c, int i, int j)
{
    if (c->grid[i][j] == NULL)
        return 0;
    return HAS_FUNCT(c->grid[i][j], OP_STREAM_IN);
}

int isStreamPort(cgra *c, int i, int j)
{
    if (c->grid[i][j] == NULL)
        return 0;
    return HAS_FUNCT(c->grid[i][j], OP_STREAM_IN) || HAS_FUNCT(c->grid[i][j], OP_STREAM_OUT);
}

int rmvStreamFuncts(cgra *c, int i, int j)
{
    if (c->grid[i][j] == NULL)
        return 0;
    if (HAS_FUNCT(c->grid[i][j], OP_STREAM_IN))
        RMV_FUNCT(c->grid[i][j], OP_STREAM_IN);
    if (HAS_FUNCT(c->grid[i][j], OP_STREAM_OUT))
        RMV_FUNCT(c->grid[i][j], OP_STREAM_OUT);
    // This is never explicitly set
    /* if (HAS_FUNCT(c->grid[i][j], OP_STREAM_IO))
        RMV_FUNCT(c->grid[i][j], OP_STREAM_IO); */
    return 1;
}

int isPE(cgra *c, int i, int j)
{
    if (c->grid[i][j] == NULL || isStreamPort(c, i, j))
        return 0;
    return 1;
}

/**
 * Sets a target CGRA tile to a specific type of PE (e.g. funct = 0 => ALU, funct = 1 => LSU)
 */
void set_cgra_tile_funct(cgra *nc, int l, int c, int funct)
{
    int i;
    if (funct == OP_FULL)
    {
        SET_FUNCT(nc->grid[l][c], OP_FULL);
        for (i = OP_ADD; i < OP_MAX; i++)
            SET_FUNCT(nc->grid[l][c], i);
    }
    else if (funct == OP_STREAM_IN)
    {
        SET_FUNCT(nc->grid[l][c], OP_STREAM_IN);
    }
    else if (funct == OP_STREAM_OUT)
    {
        SET_FUNCT(nc->grid[l][c], OP_STREAM_OUT);
    }
    else if (funct == OP_STREAM_IO)
    {
        SET_FUNCT(nc->grid[l][c], OP_STREAM_IN);
        SET_FUNCT(nc->grid[l][c], OP_STREAM_OUT);
    }
    else if (funct == OP_ALU)
    {
        SET_FUNCT(nc->grid[l][c], OP_ALU);
        for (i = OP_ADD; i < OP_LOAD; i++)
            SET_FUNCT(nc->grid[l][c], i);
    }
    else if (funct == OP_LSU)
    {
        SET_FUNCT(nc->grid[l][c], OP_LSU);
        for (i = OP_LOAD; i < OP_ICMP; i++)
            SET_FUNCT(nc->grid[l][c], i);
    }
    else
    {
        SET_FUNCT(nc->grid[l][c], funct);
    }
    // nc->grid[l][c]->functs[funct] = 1;
}

int *getPENeighbours(cgra *c, int i, int j)
{

    int k, pos = i * c->C + j, N = 0, *neighbours = (int *)malloc((c->L * c->C + 1) * sizeof(int));

    for (k = 0; k < c->L * c->C; k++)
    {
        if (c->lats[pos][k] < INFINITY)
        {
            neighbours[++N] = k;
        }
    }
    neighbours[0] = N;
    return neighbours;
}

int getNNeighboursforPE(cgra *c, int i, int j)
{

    int k, pos = i * c->C + j, N = 0;

    for (k = 0; k < c->L * c->C; k++)
    {
        if (c->lats[pos][k] < INFINITY)
        {
            ++N;
        }
    }
    return N;
}

int getConnVal(cgra *c, int i1, int j1, int i2, int j2)
{
    if (i1 < 0 || i2 < 0 || j1 < 0 || j2 < 0)
        return 0;

    if (i1 >= c->L || i2 >= c->L || j1 >= c->C || j2 >= c->C)
        return 0;

    if (c->grid[i1][j1] == NULL || c->grid[i2][j2] == NULL)
        return 0;

    int pos1 = i1 * c->C + j1, pos2 = i2 * c->C + j2;

    return c->state_src[pos1][pos2].val;
}

int getConnTime(cgra *c, int i1, int j1, int i2, int j2)
{
    if (i1 < 0 || i2 < 0 || j1 < 0 || j2 < 0)
        return 0;

    if (i1 >= c->L || i2 >= c->L || j1 >= c->C || j2 >= c->C)
        return 0;

    if (c->grid[i1][j1] == NULL || c->grid[i2][j2] == NULL)
        return 0;

    int pos1 = i1 * c->C + j1, pos2 = i2 * c->C + j2;

    return c->state_src[pos1][pos2].t;
}

int checkConnValTime(cgra *c, int i1, int j1, int i2, int j2, int val, int time)
{
    if (i1 < 0 || i2 < 0 || j1 < 0 || j2 < 0)
        return 0;

    if (i1 >= c->L || i2 >= c->L || j1 >= c->C || j2 >= c->C)
        return 0;

    if (c->grid[i1][j1] == NULL || c->grid[i2][j2] == NULL)
        return 0;

    int pos1 = i1 * c->C + j1, pos2 = i2 * c->C + j2;

    if (c->state_src[pos1][pos2].val == val && c->state_src[pos1][pos2].t == time)
        return 1;

    return 0;
}

void setConnValTime(cgra *c, int i1, int j1, int i2, int j2, int val, int time)
{
    if (i1 < 0 || i2 < 0 || j1 < 0 || j2 < 0)
        return;

    if (i1 >= c->L || i2 >= c->L || j1 >= c->C || j2 >= c->C)
        return;

    if (c->grid[i1][j1] == NULL || c->grid[i2][j2] == NULL)
        return;

    int pos1 = i1 * c->C + j1, pos2 = i2 * c->C + j2;

    c->state_src[pos1][pos2].val = val;
    c->state_src[pos1][pos2].t = time;
}

int getNumOutputRegisters(cgra *c, int i, int j)
{
    if (c->grid[i][j] == NULL)
        return 0;
    return c->grid[i][j]->NumOutputRegisters;
}

int markOutputRegister(cgra *c, int i, int j, int idx, int val, int time)
{
    // No output register to mark
    if (c->grid[i][j] == NULL)
        return 0;

    if (c->grid[i][j]->NumOutputRegisters <= idx || idx < 0)
        return 0;

    c->grid[i][j]->outputRegisters[idx] = val;
    c->grid[i][j]->outputRegisterTimes[idx] = time;
    return 1;
}

int markUncommittedOutputRegister(cgra *c, int i, int j, int val, int time)
{
    // No output register to mark
    if (i < 0 || i > c->L - 1 || j < 0 || j > c->C - 1 || c->grid[i][j] == NULL)
        return 0;

    int k;
    for (k = 0; k < c->grid[i][j]->NumOutputRegisters; k++)
    {
        if (c->grid[i][j]->outputRegisters[k] == NOT_YET_COMMITTED)
        {
            c->grid[i][j]->outputRegisters[k] = val;
            c->grid[i][j]->outputRegisterTimes[k] = time;
            return 1;
        }
    }
    return 0;
}

int hasFreeOutputRegister(cgra *c, int i, int j)
{
    // Streaming Ports don't count as having registers, they are just connections to the streaming engine outside
    /* if (isStreamPort(c, i, j))
        return 0; */

    if (c->grid[i][j] == NULL)
        return -1;

    int k;
    for (k = 0; k < c->grid[i][j]->NumOutputRegisters; k++)
    {
        if (c->grid[i][j]->outputRegisters[k] == 0)
            return k;
    }
    return -1; // No free output register found
}

int hasOutputRegister(cgra *c, int i, int j, int val, int time)
{
    if (i < 0 || i > c->L - 1 || j < 0 || j > c->C - 1)
        return -1;

    if (c->grid[i][j] == NULL || c->grid[i][j]->NumOutputRegisters <= 0)
        return -1;

    int k;
    for (k = 0; k < c->grid[i][j]->NumOutputRegisters; k++)
    {
        if (c->grid[i][j]->outputRegisters[k] == val && c->grid[i][j]->outputRegisterTimes[k] == time)
            return k;
    }
    return -1;
}

int getOutputRegister(cgra *c, int i, int j, int idx)
{
    if (c->grid[i][j] == NULL || idx < 0 || idx >= c->grid[i][j]->NumOutputRegisters)
        return -1;
    return c->grid[i][j]->outputRegisters[idx];
}

int getOutputRegisterTime(cgra *c, int i, int j, int idx)
{
    if (c->grid[i][j] == NULL || idx < 0 || idx >= c->grid[i][j]->NumOutputRegisters)
        return -1;
    return c->grid[i][j]->outputRegisterTimes[idx];
}

void initOutputRegisters(cgra *c, int i, int j, int n, int rfrp)
{
    init_pe_n_output_registers(c->grid[i][j], n, rfrp);
}

void initLocalRegisterFile(cgra *c, int i, int j, int rfsize, int rfrp)
{
    init_pe_registerFile(c->grid[i][j], rfsize, rfrp);
}

void initConstantUnits(cgra *c, int i, int j, int cusize)
{
    init_pe_constantUnits(c->grid[i][j], cusize);
}

void setPEPipelineStages(cgra *c, int i, int j, int numStages)
{
    if (c->grid[i][j] == NULL)
        return;
    c->grid[i][j]->pipelineStages = numStages;
}

int getPEPipelineStages(cgra *c, int i, int j)
{
    if (c->grid[i][j] == NULL)
        return 0;
    return c->grid[i][j]->pipelineStages;
}

int getNFUInputs(cgra *c, int i, int j)
{
    if (c->grid[i][j] == NULL)
        return 0;
    return c->grid[i][j]->fu_NInputs;
}

void resetLocalRegisterFiles(cgra *c)
{
    int i, j, r;
    while (c->next_slice != NULL)
    {
        for (i = 0; i < c->L; i++)
        {
            for (j = 0; j < c->C; j++)
            {
                if (c->grid[i][j] != NULL && c->grid[i][j]->RFsize > 0 && c->grid[i][j]->powerOn == POWER_ON)
                {
                    for (r = 0; r < c->grid[i][j]->RFsize; r++)
                    {
                        c->grid[i][j]->registerFile[r] = 0;
                    }
                }
            }
        }
        c = c->next_slice;
    }
}

int getNFreeOutputRegisters(cgra *c, int i, int j)
{
    if (c->grid[i][j] == NULL)
        return 0;
    int r, n_free = 0;
    for (r = 0; r < c->grid[i][j]->NumOutputRegisters; r++)
    {
        if (c->grid[i][j]->outputRegisters[r] == 0)
            n_free++;
    }
    return n_free;
}

int getNFreeLRFEntries(cgra *c, int i, int j)
{
    if (c->grid[i][j] == NULL)
        return 0;
    int r, n_free = 0;
    for (r = 0; r < c->grid[i][j]->RFsize; r++)
    {
        if (c->grid[i][j]->registerFile[r] == 0)
            n_free++;
    }
    return n_free;
}

int getEffNumOutputRegisters(cgra *c, int i, int j)
{
    if (c->grid[i][j] == NULL)
        return 0;

    int r, sz = -1;
    for (r = 0; r < c->grid[i][j]->NumOutputRegisters; r++)
    {
        if (c->grid[i][j]->outputRegisters[r] != 0)
            sz = r;
    }
    return sz + 1;
}

int getEffLRFSize(cgra *c, int i, int j)
{
    if (c->grid[i][j] == NULL)
        return 0;

    int r, sz = -1;
    for (r = 0; r < c->grid[i][j]->RFsize; r++)
    {
        if (c->grid[i][j]->registerFile[r] != 0)
            sz = r;
    }
    return sz + 1;
}

int getNFreeCUEntries(cgra *c, int i, int j)
{
    if (c->grid[i][j] == NULL)
        return 0;
    int r, n_free = 0;
    for (r = 0; r < c->grid[i][j]->CUsize; r++)
    {
        if (c->grid[i][j]->constantUnits[r] == 0)
            n_free++;
    }
    return n_free;
}

int hasCUEntry(cgra *c, int i, int j, int val)
{
    if (c->grid[i][j] == NULL)
        return 0;
    int r;
    for (r = 0; r < c->grid[i][j]->CUsize; r++)
    {
        // Has the value stored @ clock cycle t
        if (c->grid[i][j]->constantUnits[r] == val)
            return 1;
    }
    return 0;
}

int signCUEntry(cgra *c, int i, int j, int val, int id)
{
    if (c->grid[i][j] == NULL)
        return 0;
    int r;
    for (r = 0; r < c->grid[i][j]->CUsize; r++)
    {
        if (c->grid[i][j]->constantUnits[r] == val)
        {
            c->grid[i][j]->constantUnitReservations[r][id / 32] |= (1U << (id % 32));
            return 1;
        }
    }
    return 0;
}

int reserveConstantUnit(cgra *c, int i, int j, int id)
{
    if (c->grid[i][j] == NULL)
        return 0;
    int r;
    for (r = 0; r < c->grid[i][j]->CUsize; r++)
    {
        if (c->grid[i][j]->constantUnits[r] == 0)
        {
            c->grid[i][j]->constantUnits[r] = id;
            return 1;
        }
    }
    return 0;
}

int hasFreeLRFEntry(cgra *c, int i, int j)
{
    if (c->grid[i][j] == NULL)
        return 0;
    int r;
    for (r = 0; r < c->grid[i][j]->RFsize; r++)
    {
        if (c->grid[i][j]->registerFile[r] == 0)
            return 1;
    }
    return 0;
}

int hasLRFEntry(cgra *c, int i, int j, int t, int val)
{
    if (c->grid[i][j] == NULL)
        return 0;
    int r;
    for (r = 0; r < c->grid[i][j]->RFsize; r++)
    {
        // Has the value stored @ clock cycle t
        if (c->grid[i][j]->registerFile[r] == val && c->grid[i][j]->registerFileTime[r] == t)
            return 1;
    }
    return 0;
}

int getCUsize(cgra *c, int i, int j)
{
    if (c->grid[i][j] == NULL)
        return 0;
    return c->grid[i][j]->CUsize;
}

int cuentryIsSigned(cgra *c, int i, int j, int val)
{
    if (c->grid[i][j] == NULL)
        return 0;
    int k, r;
    for (r = 0; r < c->grid[i][j]->CUsize; r++)
    {
        // If this LRF Entry is signed by the target op, unsign it
        if (c->grid[i][j]->constantUnits[r] == val)
        {
            for (k = 0; k < 8; k++)
            {
                if (c->grid[i][j]->constantUnitReservations[r][k] != 0)
                    return 1;
            }
            return 0;
        }
    }
    return 0;
}

int cuentrySignedBy(cgra *c, int i, int j, int val, int id)
{
    if (c->grid[i][j] == NULL)
        return 0;
    int r;
    for (r = 0; r < c->grid[i][j]->CUsize; r++)
    {
        if (c->grid[i][j]->constantUnits[r] == val)
        {
            return (c->grid[i][j]->constantUnitReservations[r][id / 32] & (1U << (id % 32))) != 0;
        }
    }
    return 0;
}

int unsignCUEntry(cgra *c, int i, int j, int val, int id)
{
    if (c->grid[i][j] == NULL)
        return 0;
    int r;
    for (r = 0; r < c->grid[i][j]->CUsize; r++)
    {
        // If this LRF Entry is signed by the target op, unsign it
        if (c->grid[i][j]->constantUnits[r] == val && cuentrySignedBy(c, i, j, val, id))
        {
            c->grid[i][j]->constantUnitReservations[r][id / 32] &= ~(1U << (id % 32)); // Clear the corresponding bit
            // Reservation is now empty after unsigning. Free this reservation
            if (!cuentryIsSigned(c, i, j, val))
            {
                c->grid[i][j]->constantUnits[r] = FREE;
            }
            return 1;
        }
    }
    return 0;
}

/**
 * Return values: reservation success ? 1 : 0
 */
int reserveRegister(cgra *c, int i, int j, int t, int id)
{
    if (c->grid[i][j] == NULL)
        return 0;
    int r;
    for (r = 0; r < c->grid[i][j]->RFsize; r++)
    {
        if (c->grid[i][j]->registerFile[r] == 0)
        {
            c->grid[i][j]->registerFile[r] = id;
            c->grid[i][j]->registerFileTime[r] = t;
            return 1;
        }
    }
    return 0;
}

int signLRFEntry(cgra *c, int i, int j, int t, int val, int id)
{
    if (c->grid[i][j] == NULL)
        return 0;
    int r;
    for (r = 0; r < c->grid[i][j]->RFsize; r++)
    {
        if (c->grid[i][j]->registerFile[r] == val && c->grid[i][j]->registerFileTime[r] == t)
        {
            c->grid[i][j]->registerFileReservations[r][id / 32] |= (1U << (id % 32));
            return 1;
        }
    }
    return 0;
}

int entrySignedBy(cgra *c, int i, int j, int t, int val, int id)
{
    if (c->grid[i][j] == NULL)
        return 0;
    int r;
    for (r = 0; r < c->grid[i][j]->RFsize; r++)
    {
        if (c->grid[i][j]->registerFile[r] == val && c->grid[i][j]->registerFileTime[r] == t)
        {
            return (c->grid[i][j]->registerFileReservations[r][id / 32] & (1U << (id % 32))) != 0;
        }
    }
    return 0;
}

int entryIsSigned(cgra *c, int i, int j, int t, int val)
{
    if (c->grid[i][j] == NULL)
        return 0;
    int k, r;
    for (r = 0; r < c->grid[i][j]->RFsize; r++)
    {
        // If this LRF Entry is signed by the target op, unsign it
        if (c->grid[i][j]->registerFile[r] == val && c->grid[i][j]->registerFileTime[r] == t)
        {
            for (k = 0; k < 8; k++)
            {
                if (c->grid[i][j]->registerFileReservations[r][k] != 0)
                    return 1;
            }
            return 0;
        }
    }
    return 0;
}

int unsignLRFEntry(cgra *c, int i, int j, int t, int val, int id)
{
    if (c->grid[i][j] == NULL)
        return 0;
    int r;
    for (r = 0; r < c->grid[i][j]->RFsize; r++)
    {
        // If this LRF Entry is signed by the target op, unsign it
        if (c->grid[i][j]->registerFile[r] == val && c->grid[i][j]->registerFileTime[r] == t && entrySignedBy(c, i, j, t, val, id))
        {
            c->grid[i][j]->registerFileReservations[r][id / 32] &= ~(1U << (id % 32)); // Clear the corresponding bit
            // Reservation is now empty after unsigning. Free this reservation
            if (!entryIsSigned(c, i, j, t, val))
            {
                c->grid[i][j]->registerFile[r] = FREE;
                c->grid[i][j]->registerFileTime[r] = 0;
                // if the cleared value was the one accessing the RF at this time, enable the RF's access once again
                if (c->grid[i][j]->registerFileAccess == val)
                    c->grid[i][j]->registerFileAccess = 0;
            }
            return 1;
        }
    }
    return 0;
}

/***
 * Searches for the RF entry @ time t. Returns the address (index of the array)
 */
int getAddress(cgra *c, int i, int j, int t, int val)
{
    if (c->grid[i][j] == NULL)
        return 0;
    int r;
    for (r = 0; r < c->grid[i][j]->RFsize; r++)
    {
        // Has the value stored @ clock cycle t
        if (c->grid[i][j]->registerFile[r] == val && c->grid[i][j]->registerFileTime[r] == t)
            return r;
    }
    return -1;
}

int getAddressNoTime(cgra *c, int i, int j, int val)
{
    if (c->grid[i][j] == NULL)
        return 0;
    int r;
    for (r = 0; r < c->grid[i][j]->RFsize; r++)
    {
        // Has the value stored @ clock cycle t
        if (c->grid[i][j]->registerFile[r] == val)
            return r;
    }
    return -1;
}

int getCnstAddress(cgra *c, int i, int j, int val)
{
    if (c->grid[i][j] == NULL)
        return 0;
    int r;
    for (r = 0; r < c->grid[i][j]->CUsize; r++)
    {
        // Has the value stored @ clock cycle t
        if (c->grid[i][j]->constantUnits[r] == val)
            return r;
    }
    return 0;
}

cgra *getNextModuloSlice(cgra *c);

int getLRFVal(cgra *c, int i, int j, int addr)
{
    if (c->grid[i][j] == NULL || addr < 0 || addr >= c->grid[i][j]->RFsize)
        return 0;
    return c->grid[i][j]->registerFile[addr];
}

/**
 * Searches for a Register that is free for as many clock cycles as required. Returns the address (index of the array)
 */
int isAddressable(cgra *c, int i, int j, int val, int t, int cc)
{
    if (c->grid[i][j] == NULL)
        return -1;

    /* int *regs = (int *)calloc(c->grid[i][j]->RFsize, sizeof(int)); */
    int k, r, reg, currAddr = -1;

    cgra *nc;

    // Register occupied by some 'val' of a different iteration. Not addressable for this iteration's 'val'
    for (r = 0; r < c->grid[i][j]->RFsize; r++)
    {
        nc = c;
        for (k = 0; k < cc; k++)
        {
            reg = nc->grid[i][j]->registerFile[r];
            // Register occupied by some 'val' of a different iteration. Not addressable for this iteration's 'val'
            if ((reg == val || reg == NOT_YET_COMMITTED) && nc->grid[i][j]->registerFileTime[r] != t + k)
            {
                return -1;
            }

            /** if c->grid[i][j]->registerFile[r] != 0 && != val || registerFileTime does not match, (in use => not addressable) */
            nc = getNextModuloSlice(nc);
        }
    }

    for (r = 0; r < c->grid[i][j]->RFsize; r++)
    {
        nc = c;
        for (k = 0; k < cc; k++)
        {
            reg = nc->grid[i][j]->registerFile[r];
            if (reg == val && nc->grid[i][j]->registerFileTime[r] == t + k)
            {
                // Register 'r' is addressable!
                currAddr = r;
                nc = c;
                for (k = 0; k < cc; k++)
                {
                    reg = nc->grid[i][j]->registerFile[currAddr];
                    // Register occupied by some other value. Not addressable for 'val'
                    if (reg != 0 && reg != val && reg != NOT_YET_COMMITTED)
                    {
                        break;
                    }
                    // Register occupied by some 'val' of a different iteration. Not addressable for this iteration's 'val'
                    if ((reg == val || reg == NOT_YET_COMMITTED) && nc->grid[i][j]->registerFileTime[currAddr] != t + k)
                    {
                        break;
                    }

                    /** if c->grid[i][j]->registerFile[r] != 0 && != val || registerFileTime does not match, (in use => not addressable) */
                    nc = getNextModuloSlice(nc);
                }
                // Register 'r' is addressable!
                if (k == cc)
                {
                    return currAddr;
                }
            }

            /** if c->grid[i][j]->registerFile[r] != 0 && != val || registerFileTime does not match, (in use => not addressable) */
            nc = getNextModuloSlice(nc);
        }
    }

    for (r = 0; r < c->grid[i][j]->RFsize; r++)
    {
        nc = c;
        for (k = 0; k < cc; k++)
        {
            reg = nc->grid[i][j]->registerFile[r];
            // Register occupied by some other value. Not addressable for 'val'
            if (reg != 0 && reg != val && reg != NOT_YET_COMMITTED)
            {
                break;
            }
            // Register occupied by some 'val' of a different iteration. Not addressable for this iteration's 'val'
            if ((reg == val || reg == NOT_YET_COMMITTED) && nc->grid[i][j]->registerFileTime[r] != t + k)
            {
                break;
            }

            /** if c->grid[i][j]->registerFile[r] != 0 && != val || registerFileTime does not match, (in use => not addressable) */
            nc = getNextModuloSlice(nc);
        }
        // Register 'r' is addressable!
        if (k == cc)
        {
            break;
        }
    }

    // LRF not addressable for 'val'
    if (r >= c->grid[i][j]->RFsize)
        return -1;
    return r;
}

int swapRegister(cgra *c, int i, int j, int t, int val, int addr)
{
    if (c->grid[i][j] == NULL || addr < 0 || addr >= c->grid[i][j]->RFsize)
        return 0;

    int k, rsv, swp_addr;

    for (k = 0; k < c->grid[i][j]->RFsize; k++)
    {
        if (c->grid[i][j]->registerFileTime[k] == t && (c->grid[i][j]->registerFile[k] == val || c->grid[i][j]->registerFile[k] == NOT_YET_COMMITTED))
        {
            swp_addr = k;
            break;
        }
    }

    if (k == c->grid[i][j]->RFsize || k == addr)
        return 0;

    c->grid[i][j]->registerFile[addr] = c->grid[i][j]->registerFile[swp_addr];
    c->grid[i][j]->registerFileTime[addr] = c->grid[i][j]->registerFileTime[swp_addr];
    for (rsv = 0; rsv < 8; rsv++)
        c->grid[i][j]->registerFileReservations[addr][rsv] = c->grid[i][j]->registerFileReservations[swp_addr][rsv];

    c->grid[i][j]->registerFile[swp_addr] = FREE;
    c->grid[i][j]->registerFileTime[swp_addr] = 0;
    for (rsv = 0; rsv < 8; rsv++)
        c->grid[i][j]->registerFileReservations[swp_addr][rsv] = 0;
    return 1;
}

/**
 * Return values: able to set uncommitted reservations ? 1 : 0
 */
int setUncommittedReservation(cgra *c, int i, int j, int t, int id)
{
    if (i < 0 || i > c->L - 1 || j < 0 || j > c->C - 1 || c->grid[i][j] == NULL)
        return 0;
    int r;
    for (r = 0; r < c->grid[i][j]->RFsize; r++)
    {
        if (c->grid[i][j]->registerFile[r] == NOT_YET_COMMITTED && c->grid[i][j]->registerFileTime[r] == t)
        {
            c->grid[i][j]->registerFile[r] = id;
            return 1;
        }
    }
    return 0;
}

int changeSetReservation(cgra *c, int i, int j, int old, int newval)
{
    if (c->grid[i][j] == NULL)
        return 0;
    int r;
    for (r = 0; r < c->grid[i][j]->RFsize; r++)
    {
        if (c->grid[i][j]->registerFile[r] == old)
        {
            c->grid[i][j]->registerFile[r] = newval;
            return 1;
        }
    }
    return 0;
}

/*********************************************************************************
 * reserveRFReadPort
 * inputs: device model, PE coordinates, targetStructure (0 if FU mux_in, 1 if
 * output register), target reservation value [val, t]
 * reserves a RF ReadPort to a given value on a target structure (FU mux_in or
 * output register).
 * Return value: could reserve RF Read Port ? 1 : 0
 ********************************************************************************/
int reserveRFReadPort(cgra *c, int i, int j, int targetStructure, int val, int t)
{
    int cnt, lmt;
    // FU Mux_in
    if (targetStructure == 0)
    {
        cnt = c->grid[i][j]->rfPortsToInputMuxes.counter;
        lmt = c->grid[i][j]->rfPortsToInputMuxes.limit;
        // No more ports to reserve
        if (cnt >= lmt)
            return 0;

        // the Mux_in reads can be added 'as a stack'.
        // An exception to this would be when not having bypass capabilities
        // and having a NOP operation, which, for now, is not allowed
        c->grid[i][j]->rfPortsToInputMuxes.val[cnt] = val;
        c->grid[i][j]->rfPortsToInputMuxes.t[cnt] = t;
        c->grid[i][j]->rfPortsToInputMuxes.counter = cnt + 1;
        return 1;
    }
    // Output Registers
    else if (targetStructure == 1)
    {
        cnt = c->grid[i][j]->rfPortsToOutputRegisters.counter;
        lmt = c->grid[i][j]->rfPortsToOutputRegisters.limit;
        // No more ports to reserve
        if (cnt >= lmt)
            return 0;

        // Verify that it hasn't been reserved yet
        for (int k = 0; k < cnt; k++)
        {
            if (c->grid[i][j]->rfPortsToOutputRegisters.val[k] == val && c->grid[i][j]->rfPortsToOutputRegisters.t[k] == t)
                return 1;
        }
        c->grid[i][j]->rfPortsToOutputRegisters.val[cnt] = val;
        c->grid[i][j]->rfPortsToOutputRegisters.t[cnt] = t;
        c->grid[i][j]->rfPortsToOutputRegisters.counter = cnt + 1;
        return 1;
    }

    return 0;
}

/*****************************************************
 * removeRFRPReservationMuxIn
 * Inputs: device model and PE coordinates
 * Removes the target RF Read for data that is consumed
 * on this PE at this cycle
 ****************************************************/
int removeRFRPReservationMuxIn(cgra *c, int i, int j, int val, int t)
{
    int k, cnt = c->grid[i][j]->rfPortsToInputMuxes.counter;

    // No reservations yet, therefore none to remove
    if (cnt <= 0)
        return 0;

    for (k = 0; k < c->grid[i][j]->rfPortsToInputMuxes.limit; k++)
    {
        if (c->grid[i][j]->rfPortsToInputMuxes.val[k] == val && c->grid[i][j]->rfPortsToInputMuxes.t[k] == t)
            break;
    }
    // No reservation to remove
    if (k == c->grid[i][j]->rfPortsToInputMuxes.limit)
        return 0;

    c->grid[i][j]->rfPortsToInputMuxes.val[k] = c->grid[i][j]->rfPortsToInputMuxes.val[cnt - 1];
    c->grid[i][j]->rfPortsToInputMuxes.t[k] = c->grid[i][j]->rfPortsToInputMuxes.t[cnt - 1];
    c->grid[i][j]->rfPortsToInputMuxes.val[cnt - 1] = 0;
    c->grid[i][j]->rfPortsToInputMuxes.t[cnt - 1] = 0;
    c->grid[i][j]->rfPortsToInputMuxes.counter--;
    return 1;
}

/*****************************************************
 * removeRFRPReservationOR
 * Inputs: device model and PE coordinates
 * Removes the target RF Read for output registers
 ****************************************************/
int removeRFRPReservationOR(cgra *c, int i, int j, int val, int t)
{
    int k, cnt = c->grid[i][j]->rfPortsToOutputRegisters.counter;

    // No reservations yet, therefore none to remove
    if (cnt <= 0)
        return 0;

    for (k = 0; k < c->grid[i][j]->rfPortsToOutputRegisters.limit; k++)
    {
        if (c->grid[i][j]->rfPortsToOutputRegisters.val[k] == val && c->grid[i][j]->rfPortsToOutputRegisters.t[k] == t)
            break;
    }
    // No reservation to remove
    if (k == c->grid[i][j]->rfPortsToOutputRegisters.limit)
        return 0;

    c->grid[i][j]->rfPortsToOutputRegisters.val[k] = c->grid[i][j]->rfPortsToOutputRegisters.val[cnt - 1];
    c->grid[i][j]->rfPortsToOutputRegisters.t[k] = c->grid[i][j]->rfPortsToOutputRegisters.t[cnt - 1];
    c->grid[i][j]->rfPortsToOutputRegisters.val[cnt - 1] = 0;
    c->grid[i][j]->rfPortsToOutputRegisters.t[cnt - 1] = 0;
    c->grid[i][j]->rfPortsToOutputRegisters.counter--;
    return 1;
}

int getNRFRPMuxIn(cgra *c, int i, int j)
{
    return c->grid[i][j]->rfPortsToInputMuxes.limit;
}

int getNRFRPOR(cgra *c, int i, int j)
{
    return c->grid[i][j]->rfPortsToOutputRegisters.limit;
}

int getNFreeRFRPMuxIn(cgra *c, int i, int j)
{
    int cnt = c->grid[i][j]->rfPortsToInputMuxes.counter, lmt = c->grid[i][j]->rfPortsToInputMuxes.limit;

    if (lmt <= cnt)
        return 0;

    return lmt - cnt;
}

int getNFreeRFRPOR(cgra *c, int i, int j)
{
    int cnt = c->grid[i][j]->rfPortsToOutputRegisters.counter, lmt = c->grid[i][j]->rfPortsToOutputRegisters.limit;

    if (lmt <= cnt)
        return 0;

    return lmt - cnt;
}

void setRFAccess(cgra *c, int i, int j, int val)
{
    if (c->grid[i][j] != NULL)
        c->grid[i][j]->registerFileAccess = val;
}

int getRFAccess(cgra *c, int i, int j)
{
    if (c->grid[i][j] == NULL)
        return -1;
    return c->grid[i][j]->registerFileAccess;
}

int getRFSize(cgra *c, int i, int j)
{
    if (c->grid[i][j] == NULL)
        return 0;
    return c->grid[i][j]->RFsize;
}

int peHasFunct(cgra *c, int i, int j, int funct);
/**
 * Reserves a given operation output to the first free register of the PE's RF. Returns -1 if the RF is full
 */
int reserveRegisters(cgra *c, int i, int j, int id, int num_regs)
{
    if (num_regs == 0 || peHasFunct(c, i, j, OP_STREAM_IN) || peHasFunct(c, i, j, OP_STREAM_OUT))
        return 0;

    int r, freeSlots = 0, targetSlots = 0, t;
    for (r = 0; r < c->grid[i][j]->RFsize; r++)
    {
        if (c->grid[i][j]->registerFile[r] == id)
        {
            targetSlots++;
        }
        else if (c->grid[i][j]->registerFile[r] == 0)
        {
            freeSlots++;
        }
    }
    // If there are enough registers available for reservations (reminder of 1 register per ID per context in between the scheduling of the target and the input)
    if (freeSlots + targetSlots >= num_regs)
    {
        if (targetSlots >= num_regs)
            return 0;

        num_regs -= targetSlots;
        t = num_regs;

        for (r = 0; r < c->grid[i][j]->RFsize; r++)
        {
            if (c->grid[i][j]->registerFile[r] == 0)
            {
                c->grid[i][j]->registerFile[r] = id;
                num_regs--;
                if (num_regs == 0)
                    return t;
            }
        }
    }
    return -1;
}

int reserveRegAddr(cgra *c, int i, int j, int t, int id, int addr)
{
    if (c->grid[i][j] == NULL)
        return 0;

    if (c->grid[i][j]->registerFile[addr] == 0)
    {
        c->grid[i][j]->registerFile[addr] = id;
        c->grid[i][j]->registerFileTime[addr] = t;
        return 1;
    }
    return 0;
}

int freeRegisters(cgra *c, int i, int j, int id, int num_regs)
{
    if (num_regs == 0)
        return 0;

    int r, t = num_regs;
    for (r = c->grid[i][j]->RFsize - 1; r >= 0; r--)
    {
        if (c->grid[i][j]->registerFile[r] == id)
        {
            c->grid[i][j]->registerFile[r] = 0;
            num_regs--;
            if (num_regs == 0)
                return t;
        }
    }
    return t - num_regs; // return number of registers that were freed
}

void set_cgra_interconnect(cgra *nc, int i1, int j1, int i2, int j2, int lat)
{
    if (i1 < 0 || i2 < 0 || j1 < 0 || j2 < 0)
        return;
    if (i1 >= nc->L || i2 >= nc->L || j1 >= nc->C || j2 >= nc->C)
        return;
    if (nc->grid[i1][j1] == NULL || nc->grid[i1][j2] == NULL)
        return;
    if (lat == -1)
        lat = INFINITY;
    nc->lats[i2 * nc->C + j2][i1 * nc->C + j1] = lat;
}

int get_cgra_interconnect(cgra *nc, int i1, int j1, int i2, int j2)
{
    if (i1 < 0 || i2 < 0 || j1 < 0 || j2 < 0)
        return INFINITY;
    if (i1 >= nc->L || i2 >= nc->L || j1 >= nc->C || j2 >= nc->C)
        return INFINITY;
    if (nc->grid[i1][j1] == NULL || nc->grid[i1][j2] == NULL)
        return INFINITY;
    return nc->lats[i2 * nc->C + j2][i1 * nc->C + j1];
}

void set_cgra_interconnects(cgra *nc, int side, int lat)
{

    int i, j, ij_input = 0, nij_input = 0, ij_output = 0, nij_output = 0;

    if (side < 0 || side > 16)
        return;

    nc->configs[side] = 1;

    if (side == 16)
    {
        for (i = 0; i < nc->L; i++)
        {
            for (j = 0; j < nc->C; j++)
            {
                if (nc->grid[i][j] != NULL && isStreamPort(nc, i, j))
                {
                    if (j < nc->C - 1)
                    {
                        if (nc->grid[i][j + 1] != NULL && isStreamPort(nc, i, j + 1))
                        {
                            nc->lats[i * nc->C + j][i * nc->C + j + 1] = INFINITY;
                            nc->lats[i * nc->C + j + 1][i * nc->C + j] = INFINITY;
                        }
                        if (i < nc->L - 1 && nc->grid[i + 1][j + 1] != NULL && isStreamPort(nc, i + 1, j + 1))
                        {
                            nc->lats[i * nc->C + j][(i + 1) * nc->C + j + 1] = INFINITY;
                            nc->lats[(i + 1) * nc->C + j + 1][i * nc->C + j] = INFINITY;
                        }
                    }
                    if (i < nc->L - 1)
                    {
                        if (nc->grid[i + 1][j] != NULL && isStreamPort(nc, i + 1, j))
                        {
                            nc->lats[i * nc->C + j][(i + 1) * nc->C + j] = INFINITY;
                            nc->lats[(i + 1) * nc->C + j][i * nc->C + j] = INFINITY;
                        }
                        if (j > 0 && nc->grid[i + 1][j - 1] != NULL && isStreamPort(nc, i + 1, j - 1))
                        {
                            nc->lats[i * nc->C + j][(i + 1) * nc->C + j - 1] = INFINITY;
                            nc->lats[(i + 1) * nc->C + j - 1][i * nc->C + j] = INFINITY;
                        }
                    }
                }
            }
        }
        return;
    }

    if (side < 12)
    {
        for (i = 0; i < nc->L; i++)
        {
            for (j = 0; j < nc->C; j++)
            {
                if (nc->grid[i][j] == NULL)
                    continue;

                ij_input = HAS_FUNCT(nc->grid[i][j], OP_STREAM_IN) && !HAS_FUNCT(nc->grid[i][j], OP_STREAM_OUT);
                ij_output = !HAS_FUNCT(nc->grid[i][j], OP_STREAM_IN) && HAS_FUNCT(nc->grid[i][j], OP_STREAM_OUT);

                if (side == 0 || side == 3 || side == 4)
                { // One sided horizontal: left to right
                    if (j < nc->C - 1 && nc->grid[i][j + 1] != NULL)
                    {
                        nij_input = HAS_FUNCT(nc->grid[i][j + 1], OP_STREAM_IN) && !HAS_FUNCT(nc->grid[i][j + 1], OP_STREAM_OUT);
                        if (!nij_input && !ij_output)
                            nc->lats[i * nc->C + j + 1][i * nc->C + j] = lat;
                    }
                }
                if (side == 0 || side == 3 || side == 5)
                { // One sided horizontal: right to left
                    if (j < nc->C - 1 && nc->grid[i][j + 1] != NULL)
                    {
                        nij_output = !HAS_FUNCT(nc->grid[i][j + 1], OP_STREAM_IN) && HAS_FUNCT(nc->grid[i][j + 1], OP_STREAM_OUT);
                        if (!ij_input && !nij_output)
                            nc->lats[i * nc->C + j][i * nc->C + j + 1] = lat;
                    }
                }
                if (side == 1 || side == 3 || side == 6)
                { // One sided horizontal: up to down
                    if (i < nc->L - 1 && nc->grid[i + 1][j] != NULL)
                    {
                        nij_input = HAS_FUNCT(nc->grid[i + 1][j], OP_STREAM_IN) && !HAS_FUNCT(nc->grid[i + 1][j], OP_STREAM_OUT);
                        if (!nij_input && !ij_output)
                            nc->lats[(i + 1) * nc->C + j][i * nc->C + j] = lat;
                    }
                }
                if (side == 1 || side == 3 || side == 7)
                { // One sided horizontal: down to up
                    if (i < nc->L - 1 && nc->grid[i + 1][j] != NULL)
                    {
                        nij_output = !HAS_FUNCT(nc->grid[i + 1][j], OP_STREAM_IN) && HAS_FUNCT(nc->grid[i + 1][j], OP_STREAM_OUT);
                        if (!ij_input && !nij_output)
                            nc->lats[i * nc->C + j][(i + 1) * nc->C + j] = lat;
                    }
                }
                if (side == 2 || side == 8)
                { // Diagonal to SE
                    if (i < nc->L - 1 && j < nc->C - 1 && nc->grid[i + 1][j + 1] != NULL)
                    {
                        nij_output = !HAS_FUNCT(nc->grid[i + 1][j + 1], OP_STREAM_IN) && HAS_FUNCT(nc->grid[i + 1][j + 1], OP_STREAM_OUT);
                        if (!ij_input && !nij_output)
                            nc->lats[i * nc->C + j][(i + 1) * nc->C + j + 1] = lat;
                    }
                }
                if (side == 2 || side == 9)
                { // Diagonal to NE
                    if (i < nc->L - 1 && j > 0 && nc->grid[i + 1][j - 1] != NULL)
                    {
                        nij_input = HAS_FUNCT(nc->grid[i + 1][j - 1], OP_STREAM_IN) && !HAS_FUNCT(nc->grid[i + 1][j - 1], OP_STREAM_OUT);
                        if (!nij_input && !ij_output)
                            nc->lats[(i + 1) * nc->C + j - 1][i * nc->C + j] = lat;
                    }
                }
                if (side == 2 || side == 10)
                { // Diagonal to NW
                    if (i < nc->L - 1 && j < nc->C - 1 && nc->grid[i + 1][j + 1] != NULL)
                    {
                        nij_input = HAS_FUNCT(nc->grid[i + 1][j + 1], OP_STREAM_IN) && !HAS_FUNCT(nc->grid[i + 1][j + 1], OP_STREAM_OUT);
                        if (!nij_input && !ij_output)
                            nc->lats[(i + 1) * nc->C + j + 1][i * nc->C + j] = lat;
                    }
                }
                if (side == 2 || side == 11)
                { // Diagonal to SW
                    if (i < nc->L - 1 && j > 0 && nc->grid[i + 1][j - 1] != NULL)
                    {
                        nij_output = !HAS_FUNCT(nc->grid[i + 1][j - 1], OP_STREAM_IN) && HAS_FUNCT(nc->grid[i + 1][j - 1], OP_STREAM_OUT);
                        if (!ij_input && !nij_output)
                            nc->lats[i * nc->C + j][(i + 1) * nc->C + j - 1] = lat;
                    }
                }
            }
        }
        return;
    }

    int j1, i1;
    // Check for horizontal wrap arounds
    for (i = 0; i < nc->L; i++)
    {
        // Wrap around LR and RL
        if (side == 12 || side == 13)
        {
            j = 0;
            do
            {
                j++;
            } while ((nc->grid[i][j] == NULL || isStreamPort(nc, i, j)) && j < nc->C - 1);
            j1 = nc->C - 1;
            do
            {
                j1--;
            } while ((nc->grid[i][j1] == NULL || isStreamPort(nc, i, j1)) && j1 > 0);
            if (j >= nc->C - 1 || j1 <= 0)
                continue;
            if (side == 12)
                nc->lats[i * nc->C + j1][i * nc->C + j] = lat;
            else if (side == 13)
                nc->lats[i * nc->C + j][i * nc->C + j1] = lat;
        }
    }

    // Check for vertical wrap arounds
    for (j = 0; j < nc->C; j++)
    {
        // Wrap around LR and RL
        if (side == 14 || side == 15)
        {
            i = 0;
            do
            {
                i++;
            } while ((nc->grid[i][j] == NULL || isStreamPort(nc, i, j)) && i < nc->L - 1);
            i1 = nc->L - 1;
            do
            {
                i1--;
            } while ((nc->grid[i1][j] == NULL || isStreamPort(nc, i1, j)) && i1 > 0);
            if (i >= nc->L - 1 || i1 <= 0)
                continue;
            if (side == 14)
                nc->lats[i1 * nc->C + j][i * nc->C + j] = lat;
            else if (side == 15)
                nc->lats[i * nc->C + j][i1 * nc->C + j] = lat;
        }
    }
}

int hasInterconnects(cgra *nc, int config)
{
    return nc->configs[config] == 1;
}

void set_next_slice(cgra *nc, cgra *slice)
{
    nc->next_slice = slice;
}

void set_prev_slice(cgra *nc, cgra *slice)
{
    nc->prev_slice = slice;
}

int get_cgra_L(cgra *c)
{
    return c->L;
}

int get_cgra_C(cgra *c)
{
    return c->C;
}

int getDataWidth(cgra *c)
{
    return c->data_width;
}

int get_grid_lat(cgra *c, int i, int j)
{
    return c->lats[i][j];
}

void setDeviceMII(cgra *c, int MII)
{
    c->MII = MII;
}

int getDeviceMII(cgra *c)
{
    return c->MII;
}

/**
 * Returns 0 if pe is free, i.e. currently not outputing anything
 * Returns the locked id, otherwise
 */
int pe_in_use(cgra *c, int pos)
{

    int i, k, sz = c->L * c->C;

    for (i = 0; i < sz; i++)
    {
        for (k = 0; k < 8; k++)
        {
            if (c->new_states[i][pos][k] != 0) // edge does not exist or is free
                return c->new_states[i][pos][k];
        }
    }
    return 0;
}

int pe_occupied(cgra *c, int i, int j)
{

    if (i >= c->L || j >= c->C || c->grid[i][j] == NULL)
        return -1;

    if (c->grid[i][j]->tile == 0 && c->grid[i][j]->instr == NULL)
        return 0;
    return 1;
}

int pe_occupied_by(cgra *c, int i, int j)
{

    if (i >= c->L || j >= c->C || c->grid[i][j] == NULL)
        return -1;

    if (c->grid[i][j]->tile == 0 && c->grid[i][j]->instr == NULL)
        return 0;
    return c->grid[i][j]->tile;
}

void add_conn_state(cgra *c, int i, int j, int opID)
{
    c->new_states[i][j][opID / 32] |= (1U << (opID % 32));
}

void remove_conn_state(cgra *c, int i, int j, int opID)
{
    c->new_states[i][j][opID / 32] &= ~(1U << (opID % 32));
}

int connUsedBy(cgra *c, int i1, int j1, int i2, int j2, int opID)
{

    int pos1 = i1 * c->C + j1, pos2 = i2 * c->C + j2;
    return (c->new_states[pos1][pos2][opID / 32] & (1U << (opID % 32))) != 0;
}

cgra *getPrevModuloSlice(cgra *fs);
cgra *getNextModuloSlice(cgra *fs);

/**
 * Inputs: slice where target op is located, coords of said op, id of said op, id of the connection's input, iid
 * Searches for all connections. If a connection reserved by op is found, return the position of the other PE
 * Return value: position of the other PE (i * C + j)
 */
int isConnectedToPE(cgra *c, int i, int j, int id, int iid, int t)
{

    int m, n;
    // cgra *prev = getPrevModuloSlice(c);

    // Search through all (m, n) positions
    for (m = 0; m < c->L; m++)
    {
        for (n = 0; n < c->C; n++)
        {
            if (connUsedBy(c, i, j, m, n, id) && checkConnValTime(c, i, j, m, n, iid, t))
                return m * c->C + n;
        }
    }
    return -1;
}

/***
 * Auxiliary function for exporting mapping results: def
 */
int inputConnectedToPE(cgra *c, int i, int j, int id, int iid, int fu_t)
{
    int m, n;
    // cgra *prev = getPrevModuloSlice(c);

    // Search through all (m, n) positions
    for (m = 0; m < c->L; m++)
    {
        for (n = 0; n < c->C; n++)
        {
            // if (connUsedBy(c, i, j, m, n, id) && prev->grid[m][n]->outputRegister == iid && prev->grid[m][n]->outputRegisterTime == fu_t - 1)
            if (connUsedBy(c, i, j, m, n, id) && checkConnValTime(c, i, j, m, n, iid, fu_t - 1))
                return m * c->C + n;
        }
    }
    return -1;
}

int connectsToPE(cgra *c, int i, int j, int id, int iid)
{
    int m, n;
    // cgra *prev = getPrevModuloSlice(c);

    // Search through all (m, n) positions
    for (m = 0; m < c->L; m++)
    {
        for (n = 0; n < c->C; n++)
        {
            if (connUsedBy(c, m, n, i, j, id) && getConnVal(c, m, n, i, j) == iid)
                return m * c->C + n;
        }
    }
    return -1;
}

/***
 * Aimed @ Sofia's array
 *
 */
int getEnteredPort(cgra *c, int i, int j, int address)
{
    int pos, ii, jj, t, target_id = getPrevModuloSlice(c)->grid[i][j]->registerFile[address], id = c->grid[i][j]->tile;
    cgra *p = getPrevModuloSlice(c);

    if (target_id <= 0 || id <= 0)
        return 0;

    t = p->grid[i][j]->registerFileTime[address];
    while (getPrevModuloSlice(p)->grid[i][j]->registerFile[address] == target_id && getPrevModuloSlice(p)->grid[i][j]->registerFileTime[address] == t - 1)
    {
        p = getPrevModuloSlice(p);
        t = p->grid[i][j]->registerFileTime[address];
    }
    pos = inputConnectedToPE(p, i, j, id, target_id, t);
    ii = pos / get_cgra_C(c);
    jj = pos % get_cgra_C(c);

    if (ii < i && jj == j)
        return 1;
    else if (ii == i && jj < j)
        return 2;
    else if (ii > i && jj == j)
        return 3;
    else if (ii == i && jj > j)
        return 4;
    return 0;
}

/********
 * Checks if connection [pos2] ---> [pos1] is free or in use
 */
int connInUse(cgra *c, int i1, int j1, int i2, int j2)
{

    if (i1 < 0 || i2 < 0 || j1 < 0 || j2 < 0)
        return 0;

    if (i1 >= c->L || i2 >= c->L || j1 >= c->C || j2 >= c->C)
        return 0;

    if (c->grid[i1][j1] == NULL || c->grid[i2][j2] == NULL)
        return 0;

    int i, pos1 = i1 * c->C + j1, pos2 = i2 * c->C + j2;

    for (i = 0; i < 8; i++)
    {
        if (c->new_states[pos1][pos2][i] != 0)
        {
            return 1;
        }
    }
    return 0;
}

int getconnLat(cgra *c, int i1, int j1, int i2, int j2)
{

    if (i1 < 0 || i2 < 0 || j1 < 0 || j2 < 0)
        return INFINITY;

    if (i1 >= c->L || i2 >= c->L || j1 >= c->C || j2 >= c->C)
        return INFINITY;

    if (c->grid[i1][j1] == NULL || c->grid[i2][j2] == NULL)
        return INFINITY;

    int pos1 = i1 * c->C + j1, pos2 = i2 * c->C + j2;

    return c->lats[pos1][pos2];
}

int getNConnections(cgra *c, int i1, int j1)
{
    int conn = 0, pos = i1 * c->C + j1;
    for (int i = 0; i < c->L; i++)
        for (int j = 0; j < c->C; j++)
            if (c->lats[pos][i * c->C + j] > 0 && c->lats[pos][i * c->C + j] < INFINITY)
                conn++;
    return conn;
}

/**
 * Inputs: slice where target op is located, coords of said op, id of said op
 * Searches for all connections. If a connection to op is found, return the position of the other PE
 * Return value: position of the other PE (i * C + j)
 */
int hasConnectedPEs(cgra *c, int i, int j)
{

    int m, n;
    cgra *next = getNextModuloSlice(c);

    // Search through all (m, n) positions
    for (m = 0; m < c->L; m++)
    {
        for (n = 0; n < c->C; n++)
        {
            if (connInUse(next, m, n, i, j))
            {
                return m * c->C + n;
            }
        }
    }
    return -1;
}

int hasConnectedPEsWithVal(cgra *c, int i, int j, int val, int time)
{
    int m, n;
    cgra *next = getNextModuloSlice(c);

    // Search through all (m, n) positions
    for (m = 0; m < c->L; m++)
    {
        for (n = 0; n < c->C; n++)
        {
            if (connInUse(next, m, n, i, j) && checkConnValTime(next, m, n, i, j, val, time))
            {
                return m * c->C + n;
            }
        }
    }
    return -1;
}

int ioConnectsToPE(cgra *c, int i, int j)
{

    int m, n;
    cgra *next = getNextModuloSlice(c);

    // Search through all (m, n) positions
    for (m = 0; m < c->L; m++)
    {
        for (n = 0; n < c->C; n++)
        {
            if (connInUse(next, i, j, m, n))
            {
                return m * c->C + n;
            }
        }
    }
    return -1;
}

int peHasFunct(cgra *c, int i, int j, int op)
{
    if (c->grid[i][j] == NULL)
        return 0;
    return HAS_FUNCT(c->grid[i][j], op);
}

void set_execution_time(cgra *c, int exec_time)
{
    c->execution_time = exec_time;
}

void set_num_contexts_for_one_iteration(cgra *c, int num)
{
    c->num_contexts_for_one_iteration = num;
}

int get_execution_time(cgra *c)
{
    return c->execution_time;
}

void set_mapping(cgra *c, int algorithm_id)
{
    c->mapping_flag = algorithm_id;
}

int get_mapping(cgra *c)
{
    return c->mapping_flag;
}

void set_pe_power_mode(cgra *c, int i, int j, int powerOn)
{
    c->grid[i][j]->powerOn = powerOn;
}

int get_pe_power_mode(cgra *c, int i, int j)
{
    if (c->grid[i][j] == NULL)
        return POWER_OFF;
    return c->grid[i][j]->powerOn;
}

/** LRF SECTION IS COMMENTED */
int setDirectionOpIDs(cgra *c, int i, int j, int *directions, int idx)
{
    if (c->grid[i][j] == NULL)
        return 0;

    int t = getOutputRegisterTime(c, i, j, idx);

    // North
    // if (i > 0 && connInUse(c, i, j, i - 1, j) && (getPrevModuloSlice(c)->grid[i - 1][j]->outputRegisterTime == t - 1 || t == -1))
    if (i > 0 && connInUse(c, i, j, i - 1, j) && (getConnTime(c, i, j, i - 1, j) == t - 1 || t == -1))
    {
        // directions[0] = getPrevModuloSlice(c)->grid[i - 1][j]->outputRegister;
        directions[0] = getConnVal(c, i, j, i - 1, j);
    }
    // West
    // if (j > 0 && connInUse(c, i, j, i, j - 1) && (getPrevModuloSlice(c)->grid[i][j - 1]->outputRegisterTime == t - 1 || t == -1))
    if (j > 0 && connInUse(c, i, j, i, j - 1) && (getConnTime(c, i, j, i, j - 1) == t - 1 || t == -1))
    {
        // directions[1] = getPrevModuloSlice(c)->grid[i][j - 1]->outputRegister;
        directions[1] = getConnVal(c, i, j, i, j - 1);
    }
    // South
    // if (i < c->L && connInUse(c, i, j, i + 1, j) && (getPrevModuloSlice(c)->grid[i + 1][j]->outputRegisterTime == t - 1 || t == -1))
    if (i < c->L && connInUse(c, i, j, i + 1, j) && (getConnTime(c, i, j, i + 1, j) == t - 1 || t == -1))
    {
        // directions[2] = getPrevModuloSlice(c)->grid[i + 1][j]->outputRegister;
        directions[2] = getConnVal(c, i, j, i + 1, j);
    }
    // East
    // if (j < c->C && connInUse(c, i, j, i, j + 1) && (getPrevModuloSlice(c)->grid[i][j + 1]->outputRegisterTime == t - 1 || t == -1))
    if (j < c->C && connInUse(c, i, j, i, j + 1) && (getConnTime(c, i, j, i, j + 1) == t - 1 || t == -1))
    {
        // directions[3] = getPrevModuloSlice(c)->grid[i][j + 1]->outputRegister;
        directions[3] = getConnVal(c, i, j, i, j + 1);
    }
    // LRF
    // if (hasLRFEntry(getPrevModuloSlice(c), i, j, c->grid[i][j]->outputRegisterTime - 1, c->grid[i][j]->outputRegister))
    if (hasLRFEntry(getPrevModuloSlice(c), i, j, c->grid[i][j]->outputRegisterTimes[idx] - 1, c->grid[i][j]->outputRegisters[idx]))
    {
        directions[5] = c->grid[i][j]->outputRegisters[idx];
    }

    return 1;
}

/**
 * Returns the latency of the interconnect between two tiles
 */
int get_ic_cost(cgra *c, int i1, int j1, int i2, int j2)
{
    return c->lats[i1 * c->C + j1][i2 * c->C + j2];
}

int get_cgra_ld_trghpt(cgra *c)
{
    return c->ld_thrgpt;
}

int get_cgra_st_trghpt(cgra *c)
{
    return c->st_trghpt;
}

cgra *get_next_slice(cgra *nc)
{
    return nc->next_slice;
}

cgra *get_prev_slice(cgra *nc)
{
    return nc->prev_slice;
}

cgra *get_slice(cgra *nc, int slice_num)
{

    int i;
    cgra *c = nc;

    for (i = 0; i < slice_num; i++)
        c = c->next_slice;

    return c;
}

cgra *getFirstSlice(cgra *c)
{

    cgra *next = NULL;

    while (c != NULL)
    {
        next = c;
        c = c->prev_slice;
    }

    return next;
}

cgra *getLastSlice(cgra *c)
{
    cgra *next;
    while (c != NULL)
    {
        next = c;
        c = c->next_slice;
    }

    return next;
}

cgra *getModuloSlice(cgra *fs, int t, int II)
{

    int i;
    cgra *c = fs;
    for (i = 0; i < t; i++)
    {
        if (c->next_slice == NULL)
            c = fs;
        else
            c = c->next_slice;
    }

    return c;
}

cgra *getNextModuloSlice(cgra *fs)
{
    if (fs->next_slice == NULL)
        return getFirstSlice(fs);
    return fs->next_slice;
}

cgra *getPrevModuloSlice(cgra *fs)
{
    if (fs->prev_slice == NULL)
        return getLastSlice(fs);
    return fs->prev_slice;
}

int get_n_cgra_slices(cgra *nc)
{

    int i = 0;

    while (nc != NULL)
    {
        nc = nc->next_slice;
        i++;
    }
    return i;
}

void append_slice(cgra *nc, cgra *slice)
{

    while (nc->next_slice != NULL)
    {
        nc = nc->next_slice;
    }
    nc->next_slice = slice;
}

int get_n_pe(cgra *nc)
{
    int i, j, n_PEs = 0;

    for (i = 0; i < nc->L; i++)
        for (j = 0; j < nc->C; j++)
            if (nc->grid[i][j] != NULL && isStreamPort(nc, i, j) == 0)
                n_PEs++;

    return n_PEs;
}

int get_n_pe_w_funct(cgra *nc, int funct)
{
    int i, j, n_PEs = 0;

    for (i = 0; i < nc->L; i++)
        for (j = 0; j < nc->C; j++)
            if (nc->grid[i][j] != NULL && HAS_FUNCT(nc->grid[i][j], funct) == 1)
                n_PEs++;

    return n_PEs;
}

// Type: input (0), output(1), both(2); in use (1) or all (0)
int get_n_stream_ports(cgra *nc, int type, int in_use_or_all)
{
    int i, j, n_stream_ports = 0;

    for (i = 0; i < nc->L; i++)
        for (j = 0; j < nc->C; j++)
            if (nc->grid[i][j] != NULL && isStreamPort(nc, i, j))
            {
                if (in_use_or_all == 0 || nc->grid[i][j]->instr != NULL)
                {
                    if (type == 0 && HAS_FUNCT(nc->grid[i][j], OP_STREAM_IN))
                        n_stream_ports++;
                    else if (type == 1 && HAS_FUNCT(nc->grid[i][j], OP_STREAM_OUT))
                        n_stream_ports++;
                    else if (type == 2)
                        n_stream_ports++;
                }
            }

    return n_stream_ports;
}

void remove_pe_from_cgra(cgra *nc, int l, int c)
{
    delete_pe(nc->grid[l][c]);
    nc->grid[l][c] = NULL;
}

/**
 * Returns a copy of the input cgra
 */
cgra *copy_cgra(cgra *target)
{

    int i, j, k, r;
    cgra *copy = create_cgra(target->L, target->C, target->se_ld, target->se_st, target->data_width);
    for (i = 0; i < target->L; i++)
    {
        for (j = 0; j < target->C; j++)
        {
            // Existing Tile
            if (target->grid[i][j] != NULL)
            {
                copy->grid[i][j]->tile = target->grid[i][j]->tile;
                copy->grid[i][j]->instr = target->grid[i][j]->instr;
                copy->grid[i][j]->powerOn = target->grid[i][j]->powerOn;
                copy->grid[i][j]->pipelineStages = target->grid[i][j]->pipelineStages;
                /*                 for (k = 0; k < 9; k++)
                                    copy->grid[i][j]->neighbours[k] = target->grid[i][j]->neighbours[k]; */
                for (k = 0; k < FUNCTS; k++)
                    copy->grid[i][j]->functs[k] = target->grid[i][j]->functs[k];

                copy->grid[i][j]->NumOutputRegisters = target->grid[i][j]->NumOutputRegisters;
                copy->grid[i][j]->outputRegisters = (int *)calloc(copy->grid[i][j]->NumOutputRegisters, sizeof(int));
                copy->grid[i][j]->outputRegisterTimes = (int *)calloc(copy->grid[i][j]->NumOutputRegisters, sizeof(int));
                for (k = 0; k < copy->grid[i][j]->NumOutputRegisters; k++)
                {
                    copy->grid[i][j]->outputRegisters[k] = target->grid[i][j]->outputRegisters[k];
                    copy->grid[i][j]->outputRegisterTimes[k] = target->grid[i][j]->outputRegisterTimes[k];
                }

                copy->grid[i][j]->RFsize = target->grid[i][j]->RFsize;
                copy->grid[i][j]->registerFile = (int *)calloc(copy->grid[i][j]->RFsize, sizeof(int));
                for (k = 0; k < copy->grid[i][j]->RFsize; k++)
                    copy->grid[i][j]->registerFile[k] = target->grid[i][j]->registerFile[k];
                copy->grid[i][j]->registerFileTime = (int *)calloc(copy->grid[i][j]->RFsize, sizeof(int));
                for (k = 0; k < copy->grid[i][j]->RFsize; k++)
                    copy->grid[i][j]->registerFileTime[k] = target->grid[i][j]->registerFileTime[k];
                copy->grid[i][j]->registerFileReservations = (int **)calloc(copy->grid[i][j]->RFsize, sizeof(int *));
                for (k = 0; k < copy->grid[i][j]->RFsize; k++)
                {
                    copy->grid[i][j]->registerFileReservations[k] = (int *)calloc(8, sizeof(int));
                    for (r = 0; r < 8; r++)
                        copy->grid[i][j]->registerFileReservations[k][r] = target->grid[i][j]->registerFileReservations[k][r];
                }
                copy->grid[i][j]->registerFileAccess = target->grid[i][j]->registerFileAccess;

                copy->grid[i][j]->CUsize = target->grid[i][j]->CUsize;
                copy->grid[i][j]->constantUnits = (int *)calloc(copy->grid[i][j]->CUsize, sizeof(int));
                for (k = 0; k < copy->grid[i][j]->CUsize; k++)
                    copy->grid[i][j]->constantUnits[k] = target->grid[i][j]->constantUnits[k];
                copy->grid[i][j]->constantUnitReservations = (int **)calloc(copy->grid[i][j]->CUsize, sizeof(int *));
                for (k = 0; k < copy->grid[i][j]->CUsize; k++)
                {
                    copy->grid[i][j]->constantUnitReservations[k] = (int *)calloc(8, sizeof(int));
                    for (r = 0; r < 8; r++)
                        copy->grid[i][j]->constantUnitReservations[k][r] = target->grid[i][j]->constantUnitReservations[k][r];
                }
                // RF Read Ports
                copy->grid[i][j]->rfPortsToInputMuxes.limit = target->grid[i][j]->rfPortsToInputMuxes.limit;
                copy->grid[i][j]->rfPortsToInputMuxes.counter = target->grid[i][j]->rfPortsToInputMuxes.counter;
                copy->grid[i][j]->rfPortsToInputMuxes.val = (int *)calloc(copy->grid[i][j]->rfPortsToInputMuxes.limit, sizeof(int));
                copy->grid[i][j]->rfPortsToInputMuxes.t = (int *)calloc(copy->grid[i][j]->rfPortsToInputMuxes.limit, sizeof(int));
                for (k = 0; k < copy->grid[i][j]->rfPortsToInputMuxes.limit; k++)
                {
                    copy->grid[i][j]->rfPortsToInputMuxes.val[k] = target->grid[i][j]->rfPortsToInputMuxes.val[k];
                    copy->grid[i][j]->rfPortsToInputMuxes.t[k] = target->grid[i][j]->rfPortsToInputMuxes.t[k];
                }

                copy->grid[i][j]->rfPortsToOutputRegisters.limit = target->grid[i][j]->rfPortsToOutputRegisters.limit;
                copy->grid[i][j]->rfPortsToOutputRegisters.counter = target->grid[i][j]->rfPortsToOutputRegisters.counter;
                copy->grid[i][j]->rfPortsToOutputRegisters.val = (int *)calloc(copy->grid[i][j]->rfPortsToOutputRegisters.limit, sizeof(int));
                copy->grid[i][j]->rfPortsToOutputRegisters.t = (int *)calloc(copy->grid[i][j]->rfPortsToOutputRegisters.limit, sizeof(int));
                for (k = 0; k < copy->grid[i][j]->rfPortsToOutputRegisters.limit; k++)
                {
                    copy->grid[i][j]->rfPortsToOutputRegisters.val[k] = target->grid[i][j]->rfPortsToOutputRegisters.val[k];
                    copy->grid[i][j]->rfPortsToOutputRegisters.t[k] = target->grid[i][j]->rfPortsToOutputRegisters.t[k];
                }
            }
            // This PE doesn't exist in the target, so neither should it exist in the copy
            else
                remove_pe_from_cgra(copy, i, j);
        }
    }
    for (i = 0; i < target->L * target->C; i++)
        for (j = 0; j < target->C * target->L; j++)
        {
            copy->lats[i][j] = target->lats[i][j];
            copy->state_src[i][j].val = target->state_src[i][j].val;
            copy->state_src[i][j].t = target->state_src[i][j].t;
            for (k = 0; k < 8; k++)
                copy->new_states[i][j][k] = target->new_states[i][j][k];
        }

    for (i = 0; i < 17; i++)
        copy->configs[i] = target->configs[i];

    copy->execution_time = target->execution_time;
    copy->mapping_flag = target->mapping_flag;

    return copy;
}

cgra *copy_all_cgra_slices(cgra *target)
{

    cgra *copy, *first_slice;

    copy = copy_cgra(target);
    first_slice = copy;
    target = target->next_slice;

    while (target != NULL)
    {

        copy->next_slice = copy_cgra(target);
        copy->next_slice->prev_slice = copy;
        copy = copy->next_slice;
        target = target->next_slice;
    }

    return first_slice;
}

/**
 * Frees all memory associated to cgra c
 */
void delete_cgra(cgra *c)
{

    int i, j;
    cgra *aux;

    while (c != NULL)
    {
        aux = c->next_slice;

        for (i = 0; i < c->L; i++)
        {
            for (j = 0; j < c->C; j++)
                delete_pe(c->grid[i][j]);
            free(c->grid[i]);
        }
        free(c->grid);
        for (i = 0; i < c->L * c->C; i++)
        {
            free(c->lats[i]);
            free(c->state_src[i]);
            for (j = 0; j < c->L * c->C; j++)
                free(c->new_states[i][j]);
            free(c->new_states[i]);
        }
        free(c->lats);
        free(c->new_states);
        free(c->state_src);
        free(c);

        c = aux;
    }
}

void get_color_by_funct(cgra *c, int i, int j)
{
    if (c->grid[i][j] == NULL)
        return;

    if (c->grid[i][j]->powerOn == POWER_OFF)
    {
        printf("\033[0;90m");
    }
    else if (c->grid[i][j]->tile > 0 && HAS_FUNCT(c->grid[i][j], OP_ALU))
        printf("\033[1;37m");
    else if (c->grid[i][j]->tile > 0 && HAS_FUNCT(c->grid[i][j], OP_LSU))
    {
        if (!strcmp(get_instr_op(c->grid[i][j]->instr), "LOAD"))
            printf("\033[1;32m");
        else
            printf("\033[1;35m");
    }
    else if (c->grid[i][j]->tile > 0 && isStreamPort(c, i, j))
    {
        printf("\033[1;33m");
    }
    else if (c->grid[i][j]->tile > 0 && HAS_FUNCT(c->grid[i][j], OP_FULL))
    {
        printf("\033[1;36m");
    }
    else if (c->grid[i][j]->tile > 0)
    {
        printf("\033[0;91m");
    }
    else if (c->grid[i][j]->tile == 0 && isStreamPort(c, i, j))
    {
        printf("\033[0;90m");
    }
}

/**
 * Displays the cgra in its current state
 */
void display_cgra(cgra *c, int type)
{

    int i, j, pos;
    char c1, c2, c3;

    /* for (i = 0; i < c->L * c->C; i++){
        for (j = 0; j < c->L * c->C; j++){
            printf("[");
            for (int k = 0; k < 8; k++)
            if (c->new_states[i][j][k] != 0)
                printf("(%d,%d,%d) ",i,j,c->new_states[i][j][k]);
            printf("] ");
        }
        printf("\n");
    }
    printf("\n"); */

    if (type == 0)
        printf("CGRA: \n");
    for (i = 0; i < c->L; i++)
    {
        for (j = 0; j < c->C; j++)
        {
            c1 = ' ';
            c2 = ' ';
            c3 = ' ';
            pos = i * c->C + j;

            if (c->grid[i][j] != NULL)
            {
                // if (pos - c->C >= 0 && c->states[pos][pos - c->C] > 0)
                if (pos - c->C >= 0 && connInUse(c, i, j, i - 1, j))
                    c1 = '|';
                // if (i > 0 && j > 0 && c->states[pos][pos - c->C - 1] > 0)
                if (i > 0 && j > 0 && connInUse(c, i, j, i - 1, j - 1))
                    c2 = '\\';
                // if (i > 0 && j < c->C - 1 && c->states[pos][pos - c->C + 1] > 0)
                if (i > 0 && j < c->C - 1 && connInUse(c, i, j, i - 1, j + 1))
                    c3 = '/';

                // WRAP AROUND DU
                if (connInUse(c, i, j, c->L - 1 - i, j) && i <= 1 && c->configs[15] == 1)
                    c1 = '|';

                printf("%c %c %c", c2, c1, c3);
            }
            else
                printf("     ");
        }
        printf("\n");
        for (j = 0; j < c->C; j++)
        {
            c1 = ' ';
            c2 = ' ';
            c3 = ' ';
            pos = i * c->C + j;

            if (c->grid[i][j] != NULL)
            {
                // if (j > 0 && c->states[pos][pos - 1] > 0)
                if (j > 0 && connInUse(c, i, j, i, j - 1))
                    printf(">");
                // else if (j == 0 && c->states[pos][pos + c->C - 1] > 0 && c->C > 2)
                else if (j <= 1 && connInUse(c, i, j, i, c->C - 1 - j) && c->configs[13] == 1)
                    printf(">");
                else
                    printf(" ");

                get_color_by_funct(c, i, j);
                if (HAS_FUNCT(c->grid[i][j], OP_STREAM_IN) && !HAS_FUNCT(c->grid[i][j], OP_STREAM_OUT) && c->grid[i][j]->tile == 0)
                    printf(" I\033[0;0m ");
                else if (HAS_FUNCT(c->grid[i][j], OP_STREAM_OUT) && !HAS_FUNCT(c->grid[i][j], OP_STREAM_IN) && c->grid[i][j]->tile == 0)
                    printf(" O\033[0;0m ");
                else if (HAS_FUNCT(c->grid[i][j], OP_STREAM_IN) && HAS_FUNCT(c->grid[i][j], OP_STREAM_OUT) && c->grid[i][j]->tile == 0)
                    printf("IO\033[0;0m ");
                else
                    printf("%2d\033[0;0m ", c->grid[i][j]->tile);

                // if (j < c->C - 1 && c->states[pos][pos + 1] > 0)
                if (j < c->C - 1 && connInUse(c, i, j, i, j + 1))
                    printf("<");
                // else if (j == c->C - 1 && c->states[pos][pos - j] > 0 && c->C > 2)
                else if (j >= c->C - 2 && connInUse(c, i, j, i, c->C - 1 - j) && c->configs[12] == 1)
                    printf("<");
                else
                    printf(" ");
            }
            else
                printf("     ");
        }
        printf("\n");
        for (j = 0; j < c->C; j++)
        {
            c1 = ' ';
            c2 = ' ';
            c3 = ' ';
            pos = i * c->C + j;

            if (c->grid[i][j] != NULL)
            {
                // if (pos + c->C < c->L * c->C && c->states[pos][pos + c->C] > 0)
                if (pos + c->C < c->L * c->C && connInUse(c, i, j, i + 1, j))
                    c1 = '|';
                // if (i < c->L - 1 && j > 0 && c->states[pos][pos + c->C - 1] > 0)
                if (i < c->L - 1 && j > 0 && connInUse(c, i, j, i + 1, j - 1))
                    c2 = '/';
                // if (i < c->L - 1 && j < c->C - 1 && c->states[pos][pos + c->C + 1] > 0)
                if (i < c->L - 1 && j < c->C - 1 && connInUse(c, i, j, i + 1, j + 1))
                {
                    c3 = '\\';
                }

                // WRAP AROUND UD
                if (connInUse(c, i, j, c->L - 1 - i, j) && i >= c->L - 2 && c->configs[14] == 1)
                    c1 = '|';

                printf("%c %c %c", c2, c1, c3);
            }
            else
                printf("     ");
        }
        printf("\n");
    }
    /* printf("\n"); */
}

/**
 * State: FREE or IN_USE
 */
void set_power_for_pe_set(cgra *c, int powerMode, int state)
{

    int i, j;

    cgra *slice;
    for (i = 0; i < c->L; i++)
    {
        for (j = 0; j < c->C; j++)
        {
            slice = c;
            if (slice->grid[i][j] == NULL)
                continue;

            while (slice != NULL)
            {
                if (!((slice->grid[i][j]->tile == 0 && slice->grid[i][j]->instr == NULL && pe_in_use(slice, i * c->C + j) == 0)))
                    break;
                slice = slice->next_slice;
            }
            if ((slice == NULL && state == FREE) || (slice != NULL && state == IN_USE))
            {
                slice = c;
                while (slice != NULL)
                {
                    set_pe_power_mode(slice, i, j, powerMode); // pe is unused -> turn it off to save power
                    slice = slice->next_slice;
                }
            }
        }
    }
}

void get_color_for_lrf(cgra *c, int i, int j, dfg *d, int reg, int cu)
{

    if (c->grid[i][j] == NULL)
        return;

    if (c->grid[i][j]->powerOn == POWER_OFF)
    {
        printf("\033[0;90m");
    }
    else
    {
        char *op;
        if (cu == -1)
        {
            op = get_instr_op(get_instr_by_op_id(d, c->grid[i][j]->outputRegisters[reg]));
            if (!strcmp(op, "CONST"))
                printf("\033[1;32m");
            else
                printf("\033[1;31m");
        }
        else if (reg < c->grid[i][j]->RFsize)
        {
            if (cu == 1)
                op = get_instr_op(get_instr_by_op_id(d, c->grid[i][j]->constantUnits[reg]));
            else if (cu == 0)
                op = get_instr_op(get_instr_by_op_id(d, c->grid[i][j]->registerFile[reg]));
            if (!strcmp(op, "CONST"))
                printf("\033[1;32m");
            else
                printf("\033[1;31m");
        }
    }
}

void display_localRegisterFiles(cgra *c, dfg *d)
{

    int i, j, r, num_regs = 0;
    for (i = 0; i < c->L; i++)
    {
        for (j = 0; j < c->C; j++)
        {
            if (c->grid[i][j] != NULL && !isStreamPort(c, i, j))
            {
                num_regs += c->grid[i][j]->RFsize + 1 + c->grid[i][j]->CUsize; // + 1 due to the output register
                printf("RF [\033[0;32m%d\033[0;0m,\033[0;32m%d\033[0;0m]: ", i, j);

                // display output register

                for (r = 0; r < c->grid[i][j]->NumOutputRegisters; r++)
                {
                    if (c->grid[i][j]->outputRegisters[r] > 0)
                    {
                        if (d != NULL)
                            get_color_for_lrf(c, i, j, d, r, -1);
                        else
                            printf("\033[1;36m");
                        printf("%2d \033[0;0m", c->grid[i][j]->outputRegisters[r]);
                    }
                    else
                        printf("%2d ", c->grid[i][j]->outputRegisters[r]);
                }

                if (c->grid[i][j]->RFsize > 0)
                    printf("|");

                for (r = 0; r < c->grid[i][j]->RFsize; r++)
                {
                    if (c->grid[i][j]->registerFile[r] > 0)
                    {
                        if (d != NULL)
                            get_color_for_lrf(c, i, j, d, r, 0);
                        else
                            printf("\033[1;36m");
                        printf("%2d\033[0;0m ", c->grid[i][j]->registerFile[r]);
                    }
                    else
                        printf("%2d ", c->grid[i][j]->registerFile[r]);
                }

                if (c->grid[i][j]->CUsize > 0)
                    printf("|");

                for (r = 0; r < c->grid[i][j]->CUsize; r++)
                {
                    if (c->grid[i][j]->constantUnits[r] > 0)
                    {
                        if (d != NULL)
                            get_color_for_lrf(c, i, j, d, r, 1);
                        else
                            printf("\033[1;36m");
                        printf("%2d\033[0;0m ", c->grid[i][j]->constantUnits[r]);
                    }
                    else
                        printf("%2d ", c->grid[i][j]->constantUnits[r]);
                }
                if (num_regs >= (c->L - 2) * (c->C - 2) + 5 || num_regs >= 12)
                {
                    num_regs = 0;
                    printf("\n");
                }
                else
                    //printf("\t");
                    printf("  ");
            }
        }
    }
    printf("\n");
}

void display_config_arch(cgra *template)
{
    int i, j;
    cgra *c = copy_cgra(template);

    for (i = 0; i < c->L * c->C; i++)
        for (j = 0; j < c->L * c->C; j++)
        {
            if (c->lats[i][j] != __INT_MAX__)
                c->new_states[i][j][0] = 1;
        }
    printf("\033[1;33mCGRA's Internal Architecture:\033[0;0m\n");
    display_cgra(c, 1);
    display_localRegisterFiles(c, NULL);
    delete_cgra(c);
}

/**
 * Assuming c as the CGRA slice in the first cycle, displays all slices, in order
 */
void display_cgra_in_time(cgra *nc, dfg *d)
{
    int i;
    i = 1;
    while (nc != NULL)
    {
        printf("\033[1;36mCycle: \033[0;95m %d\033[0;0m\n--------------", i++);
        display_cgra(nc, 1);

        if (d != NULL)
        {
            printf("\033[1;32mRegisters: RF <Output Registers> | < Registers from the Local RF>\033[0;0m\n");
            display_localRegisterFiles(nc, d);
        }

        nc = nc->next_slice;
    }
}

void display_lats(cgra *c)
{

    int i, j;
    for (i = 0; i < c->L * c->C; i++)
    {
        for (j = 0; j < c->C * c->L; j++)
        {
            if (c->lats[i][j] == __INT_MAX__)
                printf("x ");
            else if (c->lats[i][j] != 0)
                printf("\033[0;36m%d\033[0;0m ", c->lats[i][j]);
            else
                printf("0 ");
        }
        printf("\n");
    }
    printf("\n\n");
}

/**
 * Displays the CGRA's input and output streams
 */
void display_cgra_IOs(cgra *c)
{

    int inputs[c->C * c->L], outputs[c->C * c->L];
    int i, j, k, isStreamIn, isStreamOut, idx_i, idx_o, config = 0;

    printf("\033[1;32mPE Array Input and Output Streams\033[0;0m\n");
    printf("---------------------------------\n");

    while (c != NULL)
    {
        config++;
        idx_i = 0;
        idx_o = 0;

        for (i = 0; i < c->L; i++)
        {
            for (j = 0; j < c->C; j++)
            {
                // Initialize each position of the array to coordinate "-1"
                inputs[i * c->C + j] = -1;
                outputs[i * c->C + j] = -1;

                if (c->grid[i][j] == NULL || c->grid[i][j]->instr == NULL)
                    continue;

                // Any Load with no inputs is considered an input stream (load stream). The process is analogous for output streams (w/ stores)
                isStreamIn = (!strcmp(get_instr_op(c->grid[i][j]->instr), "STREAM_IN"));
                isStreamOut = (!strcmp(get_instr_op(c->grid[i][j]->instr), "STREAM_OUT"));
                
                if (isStreamIn)
                {
                    inputs[idx_i++] = i * c->C + j;
                }
                else if (isStreamOut)
                {
                    outputs[idx_o++] = i * c->C + j;
                }
            }
        }

        if (inputs[0] != -1 || outputs[0] != -1)
        {
            printf("\033[0;32m@ Configuration:\033[0;0m %d\n", config);
        }

        if (inputs[0] != -1)
        {

            printf("\nInput Streams:\n");
            for (k = 0; k < c->C * c->L; k++)
            {
                if (inputs[k] == -1)
                    break;

                i = inputs[k] / c->C;
                j = inputs[k] % c->C;
                printf("Port @ [\033[0;32m%d\033[0;0m,\033[0;32m%d\033[0;0m]: Instruction - \033[0;36m%d\033[0;0m\n", i, j, c->grid[i][j]->tile);
            }
        }

        if (outputs[0] != -1)
        {
            printf("\nOutput Streams:\n");
            for (k = 0; k < c->C * c->L; k++)
            {
                if (outputs[k] == -1)
                    break;

                i = outputs[k] / c->C;
                j = outputs[k] % c->C;
                printf("Port @ [\033[0;32m%d\033[0;0m,\033[0;32m%d\033[0;0m]: Instruction - \033[0;36m%d\033[0;0m\n", i, j, c->grid[i][j]->tile);
            }
        }

        if (inputs[0] != -1 || outputs[0] != -1)
            printf("\n");

        c = c->next_slice;
    }
}

/*********************************************************************************
 *  Mapping
 *********************************************************************************/

// Builds the base cgra out of the template, which is replicated for the Initiation Interval
cgra *buildBaseCGRA(cgra *template, int II)
{
    cgra *next, *c = copy_cgra(template), *fs = c;
    int i;
    // Create as many slices as cycles for scheduling
    for (i = 0; i < II - 1; i++)
    {
        next = copy_cgra(template);
        set_next_slice(c, next);
        set_prev_slice(next, c);
        c = get_next_slice(c);
    }

    return fs;
}

/**
 * Returns 1 (true) if all Instructions in the dfg have been marked as placed and 0 (false) otherwise
 */
int allInstructionsPlaced(int **arr, dfg *d)
{
    int i;
    for (i = 0; i < get_dfg_size(d); i++)
    {
        if (arr[i][0] == 0)
            return 0;
    }
    return 1;
}

/**
 * Returns 1 (true) if all target's inputs have been marked as placed and 0 (false) otherwise
 */
int allInputsPlaced(dfg_instr *target, dfg *d, int *placed[4])
{

    int i, t = get_n_inputs(target);

    for (i = 0; i < t; i++)
    {
        if (placed[get_input_id(target, i) - 1][0] == 0)
        {
            return 0;
        }
    }
    return 1;
}

/**
 * Checks for Structural Hazards
 * ----------------------------------------------------------
 * Returns 1 if target instruction can be placed in target PE
 * Returns 0 otherwise
 */
int checkStructHazard(cgra *c, dfg_instr *target, int i, int j)
{
    char *op = get_instr_op(target);

    if (c->grid[i][j] == NULL)
        return 0;

    if (c->grid[i][j]->powerOn == POWER_OFF)
        return 0;

    if (HAS_FUNCT(c->grid[i][j], get_operation_index(op)))
        return 1;
    return 0;
}

/*************************************************************************
 * Critical Path
 *************************************************************************/

/**
 * Defines the execution time for each context and sets it on each corresponding context of the cgra
 */
void define_exec_time(cgra *first_slice, dfg *d, int **placed, int II)
{

    int slice;

    cgra *curr_slice = first_slice;

    for (slice = 0; slice < II; slice++)
    {
        curr_slice->execution_time = 1;
        // printf("execution time for slice %d: %d\n", slice, curr_slice->execution_time);
        //  curr_slice->execution_time = get_context_exec_time(d, placed, slice);
        curr_slice = curr_slice->next_slice;
    }
}

/**
 * Returns the critical path latecy of a CGRA Mapped to the clock cycle
 */
int get_exec_time_between_iters(cgra *c, dfg *d, int **placed)
{
    int exec_time = 0;

    while (c != NULL)
    {
        exec_time += c->execution_time;
        c = c->next_slice;
    }
    return exec_time;
}

int get_exec_time_one_iter(cgra *c, dfg *d, int **placed)
{
    int i, exec_time = 0, contexts_for_one_iter = 0;
    cgra *base = c;

    contexts_for_one_iter = c->num_contexts_for_one_iteration;
    for (i = 0; i < contexts_for_one_iter; i++)
    {
        exec_time += c->execution_time;
        if (c->next_slice == NULL)
            c = base;
        else
            c = c->next_slice;
    }
    return exec_time;
}

/*************************************************************************
 * Utilization Ratios
 *************************************************************************/

/**
 * Returns the PE utilization ratio (# PEs in Use / # PEs in total)
 */
float get_pe_util_ratio(cgra *c)
{

    int i, j, pe_amount = 0, pe_used = 0;

    cgra *nc;

    for (i = 0; i < c->L; i++)
    {
        for (j = 0; j < c->C; j++)
        {
            nc = c;
            if (nc->grid[i][j] != NULL && !isStreamPort(nc, i, j) && nc->grid[i][j]->powerOn == POWER_ON)
            {
                pe_amount++;
                while (nc != NULL)
                {

                    if (nc->grid[i][j]->tile != 0 && nc->grid[i][j]->powerOn == POWER_ON)
                    {
                        pe_used++;
                        break;
                    }

                    nc = nc->next_slice;
                }
            }
        }
    }
    return (float)pe_used / pe_amount;
}

/**
 * Returns the PE utilization ratio (# PEs in Use / # PEs in total)
 */
float get_dynamic_pe_util_ratio(cgra *c)
{

    int i, j, pe_amount = 0, pe_used = 0;

    cgra *nc = c;
    while (nc != NULL)
    {
        for (i = 0; i < nc->L; i++)
        {
            for (j = 0; j < nc->C; j++)
            {
                if (nc->grid[i][j] != NULL && !isStreamPort(nc, i, j) && nc->grid[i][j]->powerOn == POWER_ON)
                {
                    pe_amount++;
                    if (nc->grid[i][j]->tile != 0 && nc->grid[i][j]->powerOn == POWER_ON)
                    {
                        pe_used++;
                    }
                }
            }
        }
        nc = nc->next_slice;
    }

    return (float)pe_used / pe_amount;
}

float get_dynamic_pe_util_ratio_w_routing(cgra *c)
{

    int i, j, pe_amount = 0, pe_used = 0;

    cgra *nc = c;
    while (nc != NULL)
    {
        for (i = 0; i < nc->L; i++)
        {
            for (j = 0; j < nc->C; j++)
            {
                if (nc->grid[i][j] != NULL && !isStreamPort(nc, i, j) && nc->grid[i][j]->powerOn == POWER_ON)
                {
                    pe_amount++;
                    if ((nc->grid[i][j]->tile != 0 || pe_in_use(c, i * nc->C + j)) && nc->grid[i][j]->powerOn == POWER_ON)
                    {
                        pe_used++;
                    }
                }
            }
        }
        nc = nc->next_slice;
    }

    return (float)pe_used / pe_amount;
}

float output_register_util_ratio(cgra *c)
{
    int i, j, reg_amount = 0, reg_used = 0, II = get_n_cgra_slices(c);

    for (i = 0; i < c->L; i++)
        for (j = 0; j < c->C; j++)
            if (c->grid[i][j] != NULL && c->grid[i][j]->NumOutputRegisters > 0 && !isStreamPort(c, i, j))
                reg_amount += c->grid[i][j]->NumOutputRegisters;
    reg_amount *= II;

    while (c != NULL)
    {
        for (i = 0; i < c->L; i++)
        {
            for (j = 0; j < c->C; j++)
            {
                if (c->grid[i][j] != NULL && c->grid[i][j]->NumOutputRegisters > 0 && !isStreamPort(c, i, j))
                {
                    reg_used += c->grid[i][j]->NumOutputRegisters - getNFreeOutputRegisters(c, i, j);
                }
            }
        }
        c = c->next_slice;
    }
    if (reg_amount > 0)
        return (float)reg_used / reg_amount;
    else
        return 1.0;
}

float register_file_util_ratio(cgra *c)
{

    int i, j, reg_amount = 0, reg_used = 0, II = get_n_cgra_slices(c);

    for (i = 0; i < c->L; i++)
        for (j = 0; j < c->C; j++)
            if (c->grid[i][j] != NULL && c->grid[i][j]->RFsize > 0)
                reg_amount += c->grid[i][j]->RFsize;
    reg_amount *= II;

    while (c != NULL)
    {
        for (i = 0; i < c->L; i++)
        {
            for (j = 0; j < c->C; j++)
            {
                if (c->grid[i][j] != NULL && c->grid[i][j]->RFsize > 0)
                {
                    reg_used += c->grid[i][j]->RFsize - getNFreeLRFEntries(c, i, j);
                }
            }
        }
        c = c->next_slice;
    }
    if (reg_amount > 0)
        return (float)reg_used / reg_amount;
    else
        return 1.0;
}

float most_constrained_RF_util_ratio(cgra *c)
{

    int i, j, reg_used, II = get_n_cgra_slices(c);
    cgra *nc = c;
    float peak_constraint = 0.0, constraint;

    for (i = 0; i < c->L; i++)
    {
        for (j = 0; j < c->C; j++)
        {
            if (c->grid[i][j] == NULL || c->grid[i][j]->RFsize <= 0)
                continue;
            reg_used = 0;
            while (c != NULL)
            {
                reg_used += getNFreeLRFEntries(c, i, j);
                c = c->next_slice;
            }
            constraint = 1.0 - (float)reg_used / (nc->grid[i][j]->RFsize * II);
            if (constraint > peak_constraint)
                peak_constraint = constraint;
            c = nc;
        }
    }
    return peak_constraint;
}

/**
 * Returns the maximum input throughput for a CGRA mapped with a given dfg
 */
float max_input_throughput(cgra *c)
{

    int i, j, max = 0, throughput;

    while (c != NULL)
    {

        throughput = 0;
        for (i = 0; i < c->L; i++)
            for (j = 0; j < c->C; j++)
            {
                if (c->grid[i][j] != NULL && c->grid[i][j]->instr != NULL && !strcmp(get_instr_op(c->grid[i][j]->instr), "STREAM_IN"))
                    throughput++;
            }

        if (throughput > max)
            max = throughput;
        c = c->next_slice;
    }

    return (float)max;
}

/**
 * Returns the average input throughput for a CGRA mapped with a given dfg
 */
float avg_input_throughput(cgra *c)
{

    int i, j, throughput = 0, slices = get_n_cgra_slices(c);

    while (c != NULL)
    {

        for (i = 0; i < c->L; i++)
            for (j = 0; j < c->C; j++)
            {
                if (c->grid[i][j] != NULL && c->grid[i][j]->instr != NULL && !strcmp(get_instr_op(c->grid[i][j]->instr), "STREAM_IN"))
                    throughput++;
            }
        c = c->next_slice;
    }

    return (float)throughput / (float)slices;
}

/**
 * Returns the maximum output throughput for a CGRA mapped with a given dfg
 */
float max_output_throughput(cgra *c)
{

    int i, j, max = 0, throughput;

    while (c != NULL)
    {

        throughput = 0;
        for (i = 0; i < c->L; i++)
            for (j = 0; j < c->C; j++)
            {
                if (c->grid[i][j] != NULL && c->grid[i][j]->instr != NULL && !strcmp(get_instr_op(c->grid[i][j]->instr), "STREAM_OUT"))
                    throughput++;
            }

        if (throughput > max)
            max = throughput;
        c = c->next_slice;
    }

    return (float)max;
}

/**
 * Returns the average output throughput for a CGRA mapped with a given dfg
 */
float avg_output_throughput(cgra *c)
{

    int i, j, throughput = 0, slices = get_n_cgra_slices(c);

    while (c != NULL)
    {

        for (i = 0; i < c->L; i++)
            for (j = 0; j < c->C; j++)
            {
                if (c->grid[i][j] != NULL && c->grid[i][j]->instr != NULL && !strcmp(get_instr_op(c->grid[i][j]->instr), "STREAM_OUT"))
                    throughput++;
            }
        c = c->next_slice;
    }

    return (float)throughput / (float)slices;
}

float max_ipc(cgra *c)
{

    int i, j, pe_used = 0, max = 0;

    while (c != NULL)
    {

        pe_used = 0;
        for (i = 0; i < c->L; i++)
            for (j = 0; j < c->C; j++)
            {
                if (c->grid[i][j] != NULL && c->grid[i][j]->tile != 0 && !isStreamPort(c, i, j) && c->grid[i][j]->powerOn == POWER_ON)
                    pe_used++;
            }

        if (pe_used > max)
            max = pe_used;
        c = c->next_slice;
    }

    return (float)max;
}

float avg_ipc(cgra *c)
{

    int i, j, pe_used = 0, II = get_n_cgra_slices(c);

    cgra *nc = c;
    while (nc != NULL)
    {
        for (i = 0; i < nc->L; i++)
        {
            for (j = 0; j < nc->C; j++)
            {
                if (nc->grid[i][j] != NULL && !isStreamPort(nc, i, j) && nc->grid[i][j]->powerOn == POWER_ON)
                {
                    if (nc->grid[i][j]->tile != 0 && nc->grid[i][j]->powerOn == POWER_ON)
                    {
                        pe_used++;
                    }
                }
            }
        }
        nc = nc->next_slice;
    }
    return (float)pe_used / (float)II;
}

int maxVectWidth(cgra *c)
{
    int maxInputsPerCycle = c->se_ld / c->data_width, currentMaxInputsPerCycle = max_input_throughput(c);
    int maxVectorWidth;

    currentMaxInputsPerCycle = currentMaxInputsPerCycle > 0 ? currentMaxInputsPerCycle : 1;
    // Assuming that the input DFG corresponds to one iteration of the Kernel
    maxVectorWidth = (maxInputsPerCycle / currentMaxInputsPerCycle);

    return maxVectorWidth;
}

int maxVectIterPerCycle(cgra *c, int unrollingFactor)
{
    int II = get_n_cgra_slices(getFirstSlice(c)), maxIPC = maxVectWidth(c) * unrollingFactor / II;
    return maxIPC;
}

float ratioII(cgra *c)
{
    int II = get_n_cgra_slices(c);
    int MII = c->MII;

    return (float)II / (float)MII;
}

uint32_t next_pow2(uint32_t n) {
    if (n == 0) return 1; // define behavior for zero

    n--;
    n |= n >> 1;
    n |= n >> 2;
    n |= n >> 4;
    n |= n >> 8;
    n |= n >> 16;
    return n + 1;
}

/*******
 * Computes a rough area estimate for the PE.
 * It is done by estimating the different units that it should have in its internal architecture.
 */
float get_pe_area_estimate(cgra *c, int i, int j)
{

    pe* pe = c->grid[i][j];
    int data_width = c->data_width;
    int n_registers, nbits = data_width * 8, n_ops = 0;
    uint32_t mux_size;
    float area = 0.0, ff_size = 2.1061;

    for (int i = OP_ADD; i < MAX_OPS; i++){
        area += HAS_FUNCT(pe, i) * get_op_estimated_area_cost(i, nbits);
        n_ops += HAS_FUNCT(pe, i);
    }
    area += get_estimated_mux_area(n_ops, nbits);

    /* printf("fu area: %lf\n", area); */

    n_registers = pe->RFsize + pe->NumOutputRegisters + pe->CUsize;
    // Add register area (for now ignore config memory)
    area += (float)n_registers * ff_size * nbits;
    /* printf("register area: %lf\n", (float)n_registers * ff_size * nbits); */
    // Crossbar area (several multiplexers)
    if (!HAS_FUNCT(pe, OP_STREAM_IN) && !HAS_FUNCT(pe, OP_STREAM_OUT))
    {
        // RF Read/Write port muxes
        mux_size = next_pow2(pe->RFsize);
        area += (pe->rfPortsToInputMuxes.limit + pe->rfPortsToOutputRegisters.limit + 1) * get_estimated_mux_area(mux_size, nbits);
        /* printf("lrf area: %lf\n", (float)n_registers * ff_size * nbits + (pe->rfPortsToInputMuxes.limit + pe->rfPortsToOutputRegisters.limit + 1) * get_estimated_mux_area(mux_size, nbits)); */
        
        // FU Input muxes
        mux_size = next_pow2(getNNeighboursforPE(c, i, j) + (pe->rfPortsToInputMuxes.limit > 0 ? 1 : 0));
        area += pe->fu_NInputs * get_estimated_mux_area(mux_size, nbits);
        
        // Output Muxes
        mux_size = next_pow2(pe->NumOutputRegisters);
        area += 4 * get_estimated_mux_area(mux_size, nbits); 
    }
    /* printf("total area: %lf\n", area); */
    return area;
}
/***
 * Computes a rough area estimate for the PEA.
 * It is done by adding all area estimates for each PE.
 */
float get_cgra_area_estimate(cgra *c)
{

    int i, j;
    float area = 0.0;

    for (i = 0; i < c->L; i++)
    {
        for (j = 0; j < c->C; j++)
        {
            if (c->grid[i][j] != NULL)
                area += get_pe_area_estimate(c, i, j) * 1.05; // Add 5% for pe routing overhead
        }
    }

    /* printf("array area: %lf\n", area); */

    return area;
}

/*******
 * Computes a rough power estimate for the PE.
 * It is done by estimating the different units that it should have in its internal architecture.
 */
float get_pe_power_estimate(cgra *c, int i, int j)
{

    pe* pe = c->grid[i][j];
    int data_width = c->data_width;
    int n_registers, nbits = data_width * 8, n_ops = 0;
    uint32_t mux_size;
    float power = 0.0, ff_pow = 3.8741;

    for (int i = OP_ADD; i < MAX_OPS; i++){
        power += HAS_FUNCT(pe, i) * get_op_estimated_power_cost(i, nbits);
        n_ops += HAS_FUNCT(pe, i);
    }
    power += get_estimated_mux_power(n_ops, nbits);

    //printf("fu power: %lf\n", power);

    n_registers = pe->RFsize + pe->NumOutputRegisters + pe->CUsize;
    // Add register area (for now ignore config memory)
    power += (float)n_registers * ff_pow * nbits;
    //printf("register power: %lf\n", (float)n_registers * ff_pow * nbits);

    // Crossbar area (several multiplexers)
    if (!HAS_FUNCT(pe, OP_STREAM_IN) && !HAS_FUNCT(pe, OP_STREAM_OUT))
    {
        // RF Read/Write port muxes
        mux_size = next_pow2(pe->RFsize);
        power += (pe->rfPortsToInputMuxes.limit + pe->rfPortsToOutputRegisters.limit + 1) * get_estimated_mux_power(mux_size, nbits);
        //printf("lrf power: %lf\n", (float)n_registers * ff_pow * nbits + (pe->rfPortsToInputMuxes.limit + pe->rfPortsToOutputRegisters.limit + 1) * get_estimated_mux_power(mux_size, nbits));

        // FU Input muxes
        mux_size = next_pow2(getNNeighboursforPE(c, i, j) + (pe->rfPortsToInputMuxes.limit > 0 ? 1 : 0));
        power += pe->fu_NInputs * get_estimated_mux_power(mux_size, nbits);
        
        // Output Muxes
        mux_size = next_pow2(pe->NumOutputRegisters);
        power += 4 * get_estimated_mux_power(mux_size, nbits); 
    }
    //printf("total power: %lf\n", power);
    return power;
}

/***
 * Computes a rough power estimate for the PEA.
 * It is done by adding all power estimates for each PE.
 */
float get_cgra_power_estimate(cgra *c)
{

    int i, j;
    float power = 0.0;

    for (i = 0; i < c->L; i++)
    {
        for (j = 0; j < c->C; j++)
        {
            if (c->grid[i][j] != NULL)
                power += get_pe_power_estimate(c, i, j);
        }
    }

    return power;
}

/**
 * Cost function that defines the resource costs for a specific mapping in the target CGRA
 * Let X be the configuration costs, T the total active time; Let P be an array with the numbers of PEs (and Stream Ports) of each function
 * and C an array of costs for each PE funct:
 * Cost = aX + bT + (C * P)
 */
float get_resource_cost(cgra *c, dfg *d, int **placed)
{

    cgra *iter;
    int i, total_active_time = get_n_cgra_slices(c);
    float total_config_cost = 0, cost;

    // Constant factors
    float const_a = 5.0, const_b = 1.0;

    float config_costs[14] = {2, 2, 8, 4, 1, 1, 1, 1, 2, 2, 2, 2, 6, 6};

    float total_pe_costs = 0.0, tile_costs[OP_MAX] = {4, 3, 2, 2, 2.5, 1, 1, 1.4, 1.8, 1, 1.2, 1.5, 1.5};

    for (i = 0; i < 14; i++)
        total_config_cost += c->configs[i] * config_costs[i];

    for (i = 0; i < OP_MAX; i++)
    {
        if (i == OP_STREAM_IN || i == OP_STREAM_OUT)
            continue;
        total_pe_costs += get_n_pe_w_funct(c, i) * tile_costs[i];
    }

    for (i = 0, iter = c; i < get_n_cgra_slices(c); i++)
    {
        total_pe_costs += (float)get_n_stream_ports(iter, 2, 1) * tile_costs[OP_STREAM_IN];
        iter = iter->next_slice;
    }

    cost = const_a * total_config_cost + const_b * (float)total_active_time + total_pe_costs;
    // printf("cost = %f * %f + %f * %d + %f = %f\n", const_a, total_config_cost, const_b, total_active_time, total_pe_costs, cost);
    return cost;
}

/*************************************************************************
 * Other Analyses
 *************************************************************************/

/**
 * Displays the Dataflow, Cycle by Cycle
 */
void display_cycle_by_cycle(cgra *c, dfg *d, int **placed)
{
    int cpath = get_n_cgra_slices(c), cycle = 1, i, is_number;
    char input[10]; // Allow space for newline and null terminator
    char analyse = 'Y';

    while (analyse == 'Y' || analyse == 'y')
    {
        printf("\033[1;36m@ Clock Cycle:\033[0;95m %d\033[0;0m\n--------------\n\n", cycle++);
        display_cgra(c, 1);
        c = c->next_slice;
        if (cycle <= cpath)
        {
            printf("Analyse Next Cycle? (Y/n)\nOr skip N cycles? (input number of cycles)\n");

            fgets(input, sizeof(input), stdin); // Read input

            // Flag to check if input is a number
            is_number = 1;
            i = 0;
            while (input[i] == ' ')
                i++; // Skip leading spaces

            // Check if the input is a number
            for (int j = i; input[j] != '\0' && input[j] != '\n'; j++)
            {
                if (!isdigit(input[j]))
                {
                    is_number = 0;
                    break;
                }
            }

            if (is_number && input[i] != '\n')
            {
                cycle += atoi(input + i) - 1;
                for (i = 0; i < atoi(input + i) - 1; i++)
                {
                    if (c->next_slice != NULL)
                        c = c->next_slice;
                }
            }
            else if (input[i] == 'n' || input[i] == 'N')
                analyse = 'n';
            else
                analyse = 'Y';
        }
        else
        {
            printf("Reached last cycle!\n");
            analyse = 'n';
        }
    }
}

/**
 * Displays the Dataflow, as an animation
 */
void display_animation(cgra *c, dfg *d, int **placed)
{
    int cpath = get_n_cgra_slices(c), cycle = 1;

    while (cycle <= cpath)
    {
        system("clear");
        printf("\033[1;36m@ Clock Cycle:\033[0;95m %d\033[0;0m\n--------------\n\n", cycle++);
        display_cgra(c, 1);
        c = c->next_slice;
        usleep(500000);
    }
}

void auto_prune(cgra **c, dfg **d, cgra *template, int N, int *prune_info)
{
    cgra *curr;
    dfg *curr_dfg;
    int i, j, k, unused, rf, f, n, or, rfrp[2] = {0};
    int usedDirections[4]; // L, R, U, D
    int fu_ops[OP_MAX - OP_ADD], fu_ins;

    for (i = 0; i < template->L; i++)
    {
        for (j = 0; j < template->C; j++)
        {
            unused = 1;
            for (k = 0; k < 4; k++)
                usedDirections[k] = 1;

            for (k = OP_ADD; k < OP_MAX; k++)
                fu_ops[k - OP_ADD] = 0;

            rf = 0;
            or = 0;
            rfrp[0] = 0; // mux in
            rfrp[1] = 0; // output registers
            fu_ins = 0;

            for (n = 0; n < N; n++)
            {
                curr = c[n];
                curr_dfg = d[n];

                if (curr->grid[i][j] == NULL)
                    continue;

                // Analyse for all slices of this result
                while (curr != NULL)
                {
                    // If the FU is used, or the LRF is accessed, then this PE must be in use
                    if (curr->grid[i][j]->tile != 0 || curr->grid[i][j]->registerFileAccess > 0)
                    {
                        unused = 0;

                        if (curr->grid[i][j]->tile != 0)
                        {
                            fu_ops[get_operation_index(get_instr_op(curr->grid[i][j]->instr)) - OP_ADD] = 1;
                            int ninputs = get_n_inputs(curr->grid[i][j]->instr) + get_n_consts(curr->grid[i][j]->instr);
                            int *r = getInputRecArray(curr_dfg, curr->grid[i][j]->instr);
                            ninputs += r[0];
                            free(r);

                            fu_ins = fu_ins < ninputs ? ninputs : fu_ins;
                        }
                    }
                    // If any of the output registers is used, then this PE must be in use
                    for (k = 0; k < curr->grid[i][j]->NumOutputRegisters; k++)
                    {
                        if (curr->grid[i][j]->outputRegisters[k] > 0)
                            unused = 0;
                    }

                    // RF Read Ports
                    int cnt = curr->grid[i][j]->rfPortsToInputMuxes.counter;
                    rfrp[0] = cnt > rfrp[0] ? cnt : rfrp[0];
                    cnt = curr->grid[i][j]->rfPortsToOutputRegisters.counter;
                    rfrp[1] = cnt > rfrp[1] ? cnt : rfrp[1];

                    // Check for used connections
                    if (j > 0 && connInUse(curr, i, j, i, j - 1))
                        usedDirections[0] = 0;
                    if (j < template->C - 1 && connInUse(curr, i, j, i, j + 1))
                        usedDirections[1] = 0;
                    if (i > 0 && connInUse(curr, i, j, i - 1, j))
                        usedDirections[2] = 0;
                    if (i < template->L - 1 && connInUse(curr, i, j, i + 1, j))
                        usedDirections[3] = 0;

                    f = getEffLRFSize(curr, i, j);
                    rf = rf < f ? f : rf;
                    f = getEffNumOutputRegisters(curr, i, j);
                    or = or < f ? f : or;
                    curr = curr->next_slice;
                }
            }
            if (unused == 1)
            {
                if (template->grid[i][j] != NULL && isStreamPort(template, i, j))
                    prune_info[3]++;
                else if (template->grid[i][j] != NULL)
                    prune_info[2]++;
                prune_info[0] += getNConnections(c[0], i, j);
                if (template->grid[i][j] != NULL && !isStreamPort(template, i, j))
                {
                    prune_info[1] += template->grid[i][j]->RFsize;
                    prune_info[4] += template->grid[i][j]->NumOutputRegisters;
                    for (k = OP_ADD; k < OP_MAX; k++)
                    {
                        if (HAS_FUNCT(template->grid[i][j], k))
                        {
                            RMV_FUNCT(template->grid[i][j], k);
                            prune_info[5]++;
                        }
                    }
                    prune_info[6] += template->grid[i][j]->rfPortsToInputMuxes.limit;
                    prune_info[6] += template->grid[i][j]->rfPortsToOutputRegisters.limit;
                    prune_info[7] += template->grid[i][j]->fu_NInputs;
                }
                set_cgra_interconnect(template, i + 1, j, i, j, INFINITY);
                set_cgra_interconnect(template, i - 1, j, i, j, INFINITY);
                set_cgra_interconnect(template, i, j + 1, i, j, INFINITY);
                set_cgra_interconnect(template, i, j - 1, i, j, INFINITY);
                delete_pe(template->grid[i][j]);
                template->grid[i][j] = NULL;
            }
            else
            {
                // Change the template device
                curr = template;
                prune_info[1] += curr->grid[i][j]->RFsize - rf;
                delete_pe_registerFile(curr->grid[i][j]);
                init_pe_registerFile(curr->grid[i][j], rf, curr->grid[i][j]->rfPortsToInputMuxes.limit);
                prune_info[4] += curr->grid[i][j]->NumOutputRegisters - or;
                delete_pe_output_registers(curr->grid[i][j]);
                init_pe_n_output_registers(curr->grid[i][j], or, curr->grid[i][j]->rfPortsToOutputRegisters.limit);

                // Delete unused OPs
                for (k = OP_ADD; k < OP_MAX; k++)
                {
                    if (fu_ops[k - OP_ADD] == 0 && HAS_FUNCT(curr->grid[i][j], k))
                    {
                        RMV_FUNCT(curr->grid[i][j], k);
                        prune_info[5]++;
                    }
                }

                // curr->grid[i][j]->RFsize = rf;
                if (j > 0 && usedDirections[0] > 0 && get_cgra_interconnect(c[0], i, j - 1, i, j) != INFINITY)
                {
                    set_cgra_interconnect(curr, i, j - 1, i, j, INFINITY);
                    prune_info[0]++;
                }
                if (j < template->C - 1 && usedDirections[1] > 0 && get_cgra_interconnect(c[0], i, j + 1, i, j) != INFINITY)
                {
                    set_cgra_interconnect(curr, i, j + 1, i, j, INFINITY);
                    prune_info[0]++;
                }
                if (i > 0 && usedDirections[2] > 0 && get_cgra_interconnect(c[0], i - 1, j, i, j) != INFINITY)
                {
                    set_cgra_interconnect(curr, i - 1, j, i, j, INFINITY);
                    prune_info[0]++;
                }
                if (i < template->L - 1 && usedDirections[3] > 0 && get_cgra_interconnect(c[0], i + 1, j, i, j) != INFINITY)
                {
                    set_cgra_interconnect(curr, i + 1, j, i, j, INFINITY);
                    prune_info[0]++;
                }
                // Prune RF RW Ports
                prune_info[6] += curr->grid[i][j]->rfPortsToInputMuxes.limit - rfrp[0];
                prune_info[6] += curr->grid[i][j]->rfPortsToOutputRegisters.limit - rfrp[1];
                curr->grid[i][j]->rfPortsToInputMuxes.limit = rfrp[0];
                curr->grid[i][j]->rfPortsToOutputRegisters.limit = rfrp[1];

                prune_info[7] += curr->grid[i][j]->fu_NInputs - fu_ins;
                curr->grid[i][j]->fu_NInputs = fu_ins;
            }
        }
    }
}

cgra *load_mapping(cgra *template, cgra *target)
{
    int i, j, k, r, invalid = 0, II = get_n_cgra_slices(target);
    cgra *load = buildBaseCGRA(template, II), *load_base = load;

    for (; load != NULL; load = load->next_slice, target = target->next_slice)
    {
        for (i = 0; i < target->L; i++)
        {
            for (j = 0; j < target->C; j++)
            {
                // Existing Tile
                if (target->grid[i][j] != NULL)
                {
                    // If the new architecture doesn't have PEs that are required on the mapped device, then the mapping cannot be loaded
                    if (template->grid[i][j] == NULL && target->grid[i][j]->tile != 0)
                    {
                        invalid = 1;
                        break;
                    }
                    else if (template->grid[i][j] == NULL)
                        continue;

                    load->grid[i][j]->tile = target->grid[i][j]->tile;
                    load->grid[i][j]->instr = target->grid[i][j]->instr;
                    load->grid[i][j]->powerOn = target->grid[i][j]->powerOn;
                    load->grid[i][j]->pipelineStages = target->grid[i][j]->pipelineStages;
                    /*                 for (k = 0; k < 9; k++)
                                        copy->grid[i][j]->neighbours[k] = target->grid[i][j]->neighbours[k]; */
                    for (k = 0; k < FUNCTS; k++)
                        load->grid[i][j]->functs[k] = target->grid[i][j]->functs[k];

                    if (load->grid[i][j]->NumOutputRegisters < target->grid[i][j]->NumOutputRegisters)
                    {
                        for (k = load->grid[i][j]->NumOutputRegisters; k < target->grid[i][j]->NumOutputRegisters; k++)
                            if (target->grid[i][j]->outputRegisters[k] != 0)
                            {
                                invalid = 1;
                                break;
                            }
                    }

                    // Check if the PE's register file can be copied onto the new architecture
                    if (load->grid[i][j]->RFsize < target->grid[i][j]->RFsize)
                    {
                        for (k = load->grid[i][j]->RFsize; k < target->grid[i][j]->RFsize; k++)
                            if (target->grid[i][j]->registerFile[k] != 0)
                            {
                                invalid = 1;
                                break;
                            }
                    }

                    // Check if the PE's constant units can be copied onto the new architecture
                    if (load->grid[i][j]->CUsize < target->grid[i][j]->CUsize)
                    {
                        for (k = load->grid[i][j]->CUsize; k < target->grid[i][j]->CUsize; k++)
                            if (target->grid[i][j]->constantUnits[k] != 0)
                            {
                                invalid = 1;
                                break;
                            }
                    }

                    for (k = 0; k < load->grid[i][j]->NumOutputRegisters; k++)
                    {
                        load->grid[i][j]->outputRegisters[k] = target->grid[i][j]->outputRegisters[k];
                        load->grid[i][j]->outputRegisterTimes[k] = target->grid[i][j]->outputRegisterTimes[k];
                    }
                    for (k = 0; k < load->grid[i][j]->RFsize; k++)
                        load->grid[i][j]->registerFile[k] = target->grid[i][j]->registerFile[k];
                    for (k = 0; k < load->grid[i][j]->RFsize; k++)
                        load->grid[i][j]->registerFileTime[k] = target->grid[i][j]->registerFileTime[k];
                    for (k = 0; k < load->grid[i][j]->RFsize; k++)
                    {
                        for (r = 0; r < 8; r++)
                            load->grid[i][j]->registerFileReservations[k][r] = target->grid[i][j]->registerFileReservations[k][r];
                    }
                    load->grid[i][j]->registerFileAccess = target->grid[i][j]->registerFileAccess;

                    for (k = 0; k < load->grid[i][j]->CUsize; k++)
                        load->grid[i][j]->constantUnits[k] = target->grid[i][j]->constantUnits[k];
                    for (k = 0; k < load->grid[i][j]->CUsize; k++)
                    {
                        for (r = 0; r < 8; r++)
                            load->grid[i][j]->constantUnitReservations[k][r] = target->grid[i][j]->constantUnitReservations[k][r];
                    }
                }
            }
            if (invalid > 0)
                break;
        }

        if (invalid > 0)
        {
            delete_cgra(load_base);
            return NULL;
        }

        for (i = 0; i < target->L * target->C; i++)
            for (j = 0; j < target->C * target->L; j++)
            {
                load->lats[i][j] = target->lats[i][j];
                load->state_src[i][j].val = target->state_src[i][j].val;
                load->state_src[i][j].t = target->state_src[i][j].t;
                for (k = 0; k < 8; k++)
                    load->new_states[i][j][k] = target->new_states[i][j][k];
            }

        for (i = 0; i < 15; i++)
            load->configs[i] = target->configs[i];

        load->execution_time = target->execution_time;
        load->mapping_flag = target->mapping_flag;
    }

    return load_base;
}

cgra *buildHmgCopy(cgra *template, int rows, int cols)
{
    cgra *new_dev;

    if (rows <= 2 || cols <= 2)
    {
        //printf("Invalid size for the new device: %d rows, %d cols.\n", rows, cols);
        return NULL;
    }

    int has_io_top = 0, has_io_bottom = 0, has_io_left = 0, has_io_right = 0;
    
    for (int j = 0; j < template->C; j++) {
        if (template->grid[0][1] && (isStreamPort(template, 0, 1)))
            has_io_top = 1;
        if (template->grid[template->L - 1][1] && (isStreamPort(template, template->L - 1, 1)))
            has_io_bottom = 1;
    }

    for (int i = 0; i < template->L; i++) {
        if (template->grid[1][0] && (isStreamPort(template, 1, 0)))
            has_io_left = 1;
        if (template->grid[1][template->C - 1] && (isStreamPort(template, 1, template->C - 1)))
            has_io_right = 1;
    }



    int i, j;

    new_dev = create_cgra(rows, cols, template->se_ld, template->se_st, template->data_width);
    
    if (has_io_top > 0)
    {
        for (j = 0; j < cols; j++)
        {
            remove_pe_from_cgra(new_dev, 0, j);
            if (template->grid[0][1] != NULL)
                new_dev->grid[0][j] = copy_pe_params(template->grid[0][1]);
        }
    }

    if (has_io_left > 0)
    {
        for (j = 0; j < rows; j++)
        {
            remove_pe_from_cgra(new_dev, j, 0);
            if (template->grid[1][0] != NULL)
                new_dev->grid[j][0] = copy_pe_params(template->grid[1][0]);
        }
    }

    if (has_io_bottom > 0)
    {
        for (j = 0; j < cols; j++)
        {
            remove_pe_from_cgra(new_dev, rows - 1, j);
            if (template->grid[template->L - 1][1] != NULL)
                new_dev->grid[rows - 1][j] = copy_pe_params(template->grid[template->L - 1][1]);
        }
    }

    if (has_io_right > 0)
    {
        for (j = 0; j < rows; j++)
        {
            remove_pe_from_cgra(new_dev, j, cols - 1);
            if (template->grid[1][template->C - 1] != NULL)
                new_dev->grid[j][cols - 1] = copy_pe_params(template->grid[1][template->C - 1]);
        }
    }

    // Remove corners
    if (template->grid[0][0] == NULL)
        remove_pe_from_cgra(new_dev, 0, 0);
    if (template->grid[0][template->C - 1] == NULL)
        remove_pe_from_cgra(new_dev, 0, cols - 1);
    if (template->grid[template->L - 1][0] == NULL)
        remove_pe_from_cgra(new_dev, rows - 1, 0);
    if (template->grid[template->L - 1][template->C - 1] == NULL)
        remove_pe_from_cgra(new_dev, rows - 1, cols - 1);
    

    for (i = has_io_top > 0; i < rows - (has_io_bottom > 0); i++)
    {
        for (j = has_io_left > 0; j < cols - (has_io_right > 0); j++)
        {
            remove_pe_from_cgra(new_dev, i, j);
            new_dev->grid[i][j] = copy_pe_params(template->grid[has_io_top > 0][has_io_left > 0]);
        }
    }

    for (i = 0; i < 17; i++)
    {
        new_dev->configs[i] = template->configs[i];
        if (template->configs[i] > 0)
            set_cgra_interconnects(new_dev, i, 1);
    }

    return new_dev;
    
}


/**
 * Shows a summary of the mapping process:
 * In which PEs the Instructions and Streams were placed
 * The Time window of such placements
 */
void mapping_summary(cgra *c, dfg *d, int **placed)
{

    int i, j, len, id, II = get_n_cgra_slices(c);
    dfg_instr *curr;
    cgra *slice;

    dfg_instr **sorted_ops = (dfg_instr **)malloc(get_dfg_size(d) * sizeof(dfg_instr *));

    for (i = 0; i < get_dfg_size(d); i++)
    {
        curr = get_dfg_instr(d, i);
        sorted_ops[get_instr_id(curr) - 1] = curr;
    }

    printf("|------------------------------ SUMMARY -------------------------------|\n");
    printf("|--Instruction--| Operation | PE Coord | Starts @ Cycle | Ends @ Cycle |\n");
    printf("|---------------|-----------|----------|----------------|--------------|\n");

    for (i = 0; i < get_dfg_size(d); i++)
    {
        // curr = get_dfg_instr(d, i);
        curr = sorted_ops[i];
        len = strlen(get_instr_op(curr));
        id = get_instr_id(curr);

        slice = c;
        slice = getModuloSlice(c, placed[id - 1][2], II);

        printf("|\033[1;36m%s\033[1;0m", get_instr_name(curr));
        for (j = 0; j < 15 - (int)strlen(get_instr_name(curr)); j++)
            printf(" ");

        printf("|[%2d] ", id);
        if (!strcmp(get_instr_op(curr), "STREAM_IN"))
            printf("INPUT ");
        else if (!strcmp(get_instr_op(curr), "STREAM_OUT"))
            printf("OUTPUT");
        else
        {
            printf("%s", get_instr_op(curr));

            for (j = 0; j < 6 - (int)len; j++)
                printf(" ");
        }
        printf("|  [");
        get_color_by_funct(slice, placed[id - 1][1] / get_cgra_C(c), placed[id - 1][1] % get_cgra_C(c));
        printf("%d,%d\033[0;0m]   |", placed[id - 1][1] / get_cgra_C(c), placed[id - 1][1] % get_cgra_C(c));
        printf(" \033[1;32m%2d\033[1;0m (+ k * II)  | \033[1;32m%2d\033[1;0m (+ k * II)|\n", placed[id - 1][2] + 1, placed[id - 1][3] + 1);
    }

    free(sorted_ops);

    return;
}
