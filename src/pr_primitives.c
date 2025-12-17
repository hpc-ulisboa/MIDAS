#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <stdbool.h>
#include "ops.h"
#include "dfg.h"
#include "cgra.h"
#include "pqueue.h"
#include "stack.h"
#include "files.h"
#include <omp.h>
#include <time.h>

#define SOFIA 0

#define ALMOST_COMMITTED (NOT_YET_COMMITTED - 1)
#define ABS(a) a > 0 ? a : -a

/************************************************************************
 * TODO:
 * - Route recurrence edges between operations
 * ---> should probably be included in routeOp; each recurrence edge can be routed by employing routeInTime, but with a negative time
 *      budget and advancing to the next modulo slice (instead of the previous)
 *      this can probably be executed easily, by adding an input flag to routeInTime, 'recurrence_edge' (if 1, switch prev with next
 *      and doing the inverse operations with times: multiply by -1 and add instead of subtract)
 *
 * - Add constants to the array
 * ---> if an operation has constant inputs, make a reservation the LRF of the PE where the operation will be placed for each constant
 *      input. This can be executed completely in placeOp. Of course, before placing, the number of free entries in the LRF must be
 *      checked and adequately reserved, for ALL slices, and reserved in ALL slices, even if the op is not placed in every slice.
 *      Naturally, when unmapping the operation, all constants related to the op must be unmapped from ALL slices.
 *
 */

typedef struct _stackItem
{
    int i;          // device row where the PE is located
    int j;          // device column where the PE is located
    int t;          // time t, for the device slice
    bool parentReg; // 0 if this item was obtained by reserving an output register, 1 if it reserved a register from the LRF
    cgra *c;
} stackItem;

stackItem *createStackItem(int i, int j, int t, bool parentReg, cgra *c)
{
    stackItem *si = malloc(sizeof(stackItem));
    si->i = i;
    si->j = j;
    si->t = t;
    si->parentReg = parentReg;
    si->c = c;
    return si;
}

void deleteStackItem(stackItem *si)
{
    free(si);
}

/**
 * This file will define a mapping algorithm for the targeted devices.
 * The mapper will map operations to a CYCLE-accurate time-extended CGRA.
 * -> At a simpler level, ignore the latency of operations
 * -> That being said, we can always extend the operations on time, for each of the contexts (MII would then be limited by the highest op latency, but would allow for pipelining)
 * -> routing nodes: simplest would be to use Dijkstra's Algorithm extended through time
 * -> There is a hickup though: what if the routing is shorter than the scheduling difference?
 * ---> How to route:
 *                 [] we can store the needed value in a register from the producer's RF and only route for the required time
 *                 [] we can route immediately to the consumer that requires it, before it starts executing and store in its RF (requires the consumer to be free at the cycle
 *                 where it would store the value)
 *                 [] we can explicitely define routing nodes as extra tasks to be mapped (extra nodes to the DFG) that get mapped aswell. That way, the choice of which
 *                 register to store the value in can be done dynamically, depending on the current configurations of the array
 *                 [] we can also try changing the schedule, if the node has the needed mobility
 */

/**********************************************************************************************************************************************
 * getResMinII
 * Inputs: the target cgra model and DFG
 * Calculates the minimum II imposed by the available resources
 * Analyses the instructions in the dfg and the available resources in the device model to calculate the imposed limitation
 * Return values: MII associated with the available resources
 **********************************************************************************************************************************************/
int getResMinII(cgra *template, dfg *d)
{

    int i, dfg_resources[OP_MAX] = {0}, cgra_resources[OP_MAX] = {0}, minII;
    int pe_res = 0, dfg_res = 0, ii_ins = 0, ii_outs = 0;

    // Calculate the DFG Resources Required
    getRequiredResources(d, dfg_resources);

    // Calculate the CGRA Resources Avaliable
    getAvailableResources(template, cgra_resources);

    for (i = OP_ADD; i < OP_MAX; i++)
    {
        pe_res += cgra_resources[i];
        dfg_res += dfg_resources[i];
    }

    pe_res = get_n_pe(template);

    dfg_res += pe_res - 1;
    dfg_res /= pe_res;

    // II for Stream Ins and Outs
    {
        if (cgra_resources[OP_STREAM_IN] == 0)
            ii_ins = dfg_resources[OP_STREAM_IN] == 0 ? 0 : INFINITY;
        else
            ii_ins = (dfg_resources[OP_STREAM_IN] + cgra_resources[OP_STREAM_IN] - 1) / cgra_resources[OP_STREAM_IN];

        if (cgra_resources[OP_STREAM_OUT] == 0)
            ii_outs = dfg_resources[OP_STREAM_OUT] == 0 ? 0 : INFINITY;
        else
            ii_outs = (dfg_resources[OP_STREAM_OUT] + cgra_resources[OP_STREAM_OUT] - 1) / cgra_resources[OP_STREAM_OUT];
    }

    // MinII will be a ratio of required resources over available resources, round up
    // The ratio will be the highest between input resources (stream ins), output resources (stream outs) and pe resources (assuming all have maximum capabilities, for simplicity)
    if (ii_ins > ii_outs)
        minII = ii_ins;
    else
        minII = ii_outs;

    if (minII < dfg_res)
        minII = dfg_res;

    return minII;
}

/**********************************************************************************************************************************************
 * getRecMinII
 * Inputs: the target schedule and DFG
 * Calculates the minimum II imposed by the recurrence edges in the DFG
 * Uses the following equation: Delay - II * distance <= 0
 * Return values: MII associated with the recurrence edges
 **********************************************************************************************************************************************/
int getRecMinII(int *base_scheduling, dfg *d)
{
    register int i, j, r, recMinII = 1, recII;
    dfg_instr *curr;

    // RecMinII: Delay - II * distance <= 0 => II >= Delay / distance = [delta Scheduling] / [delta Iterations]
    for (i = 0; i < get_dfg_size(d); i++)
    {
        curr = get_dfg_instr(d, i);
        r = get_n_recurrences(curr);
        for (j = 0; j < r; j++)
        {
            recII = base_scheduling[get_instr_id(curr) - 1] - base_scheduling[get_instr_id(get_recurrence(curr, j)) - 1] + get_instr_lat(get_recurrence(curr, j)); // first cycle where the output is available - first cycle where it is needed = delay
            recII = (recII + get_rec_dist(curr, j) - 1) / get_rec_dist(curr, j);
            // printf("recII is %d\n", recII);
            recMinII = recMinII > recII ? recMinII : recII;
        }
    }

    return recMinII;
}

/**********************************************************************************************************************************************
 * getPipelineMinII
 * Inputs: the target DFG
 * Calculates the minimum II imposed by the latencies of the operations in the DFG
 * For now, this consists in evaluating the maximum latency. But in the future, it might be interesting to consider Maximum / Minimum latencies
 * Which could result in less contexts, but each context representing more than 1 cycle
 * Return values: MII associated with the operation latencies
 **********************************************************************************************************************************************/
int getPipelineMinII(dfg *d)
{
    int MII;
    MII = getHighestInstrLat(d);
    // MII = getHighestInstrLat(d) / getLowestInstrLat(d);

    return MII;
}

/**********************************************************************************************************************************************
 * getMII
 * Inptus: the target CGRA, DFG and schedule
 * Calculates the Minimumm II, taking resources, recurrence edges [and pipeline stages (operation latencies)] into account
 * Return values: Minimum II
 **********************************************************************************************************************************************/
int getMII(cgra *c, dfg *d, int *schedule)
{
    int MII;
    int resMII = getResMinII(c, d);
    int recMII = getRecMinII(schedule, d);
    // int pipMII = getPipelineMinII(d);
    MII = resMII > recMII ? resMII : recMII;
    // MII = MII > pipMII ? MII : pipMII;
    return MII;
}

/**********************************************************************************************************************************************
 * getRFLimitations
 * Inptus: the target CGRA, DFG and schedule
 * Checks for limitations in the register files, which can make mapping impossible and is thus important to verify beforehand.
 * Return values: Mapping possible ? 1 : 0
 **********************************************************************************************************************************************/
int getRFLimitations(cgra *c, dfg *d)
{

    int i, j, nConsts = get_dfg_n_consts(d), totalRegisters = 0;

    for (i = 0; i < get_cgra_L(c); i++)
    {
        for (j = 0; j < get_cgra_C(c); j++)
        {
            totalRegisters += getRFSize(c, i, j);
        }
    }
    // Check if there are enough registers to accomodate for all the constants
    if (totalRegisters < nConsts)
        return 0; // Not enough registers to map all constants... Mapping is guaranteed to be impossible!

    // Incomplete!

    return 1;
}

/**********************************************************************************************************************************************
 * placeOp
 * Inputs: device model, placement coordinates (i,j) = (y,x), the target op, the placement info array, the modulo schedule and the II
 * places an operation in the device model, according to its latency and the modulo scheduling
 * Return values: placed ? 1 : 0\
 **********************************************************************************************************************************************/
int placeOp(cgra *first_slice, int i, int j, dfg *d, dfg_instr *target, int **placed, int *schedule, int II)
{

    int t, c, id, maxPlacements = II < get_instr_lat(target) ? II : get_instr_lat(target), consts = get_n_consts(target), ecnsts = 0;
    cgra *targetSlice, *slice;

    if (target == NULL)
        return 0;

    id = get_instr_id(target);
    targetSlice = getModuloSlice(first_slice, schedule[get_instr_id(target) - 1], II);
    slice = targetSlice;

    // There are not enough free RF read ports to consume the constants
    if (getNFreeRFRPMuxIn(slice, i, j) < consts)
        return 0;

    // Check if the PE to place the op in is free for all necessary cycles
    for (t = 0; t < maxPlacements; t++)
    {
        if (pe_occupied(slice, i, j)) // If it is occupied, then it is impossible to place the op
            return 0;
        slice = (get_next_slice(slice) == NULL ? first_slice : get_next_slice(slice));
    }
    // Check for LRF freedom to reserve for constants
    slice = first_slice;
    if (consts > 0)
    {
        if (getCUsize(slice, i, j) > 0)
        {
            // Check which constants used by the target are not yet reserved
            for (c = 0; c < consts; c++)
            {
                // Ignore constants that are already reserved
                if (!hasCUEntry(slice, i, j, get_instr_id(get_const(target, c))))
                    ecnsts++;
            }
            // Check if there are enough constant units to reserve the necessary constants
            if (getNFreeCUEntries(slice, i, j) < ecnsts)
                return 0; // Not enough free entries in the CU to reserve for constants
        }
        else
        {
            // Old constant management
            for (t = 0; t < II; t++)
            {
                // Check if the LRF has at least as many entries as constants used by the instruction at this clock cycle
                if (getNFreeLRFEntries(slice, i, j) < consts)
                    return 0;
                slice = (get_next_slice(slice) == NULL ? first_slice : get_next_slice(slice));
            }
        }
    }

    // If the FU is pipelined, reschedule the remaining nodes accordingly (this node's output will be delayed by the ppstages' latency)
    if (getPEPipelineStages(first_slice, i, j) > 1)
    {
        int canPlace = pipelineReschedule(schedule, first_slice, d, target, placed, getPEPipelineStages(first_slice, i, j) - 1, II);
        // Cannot reschedule the remaining nodes, therefore this node cannot be placed here
        if (canPlace == 0)
            return 0;
    }

    // Place the op
    slice = targetSlice;
    for (c = 0; c < consts; c++)
    {
        // Reserve the necessary mux_in ports
        reserveRFReadPort(targetSlice, i, j, 0, get_instr_id(get_const(target, c)), 0);
    }
    for (t = 0; t < maxPlacements; t++)
    {
        set_cgra_tile(slice, i, j, target);
        set_cgra_value(slice, get_instr_id(target), i, j);
        slice = (get_next_slice(slice) == NULL ? first_slice : get_next_slice(slice));
    }
    // Reserve the necessary LRF entries
    slice = first_slice;
    int addr;
    for (c = 0; c < consts; c++)
    {
        addr = isAddressable(slice, i, j, get_instr_id(get_const(target, c)), t, II);
        for (t = 0; t < II; t++)
        {
            if (getCUsize(slice, i, j) > 0)
            {
                // Reserve the necessary constant units
                if (!hasCUEntry(slice, i, j, get_instr_id(get_const(target, c))))
                    reserveConstantUnit(slice, i, j, get_instr_id(get_const(target, c)));
                signCUEntry(slice, i, j, get_instr_id(get_const(target, c)), id);
            }
            else
            {
                // Old constant management
                if (!hasLRFEntry(slice, i, j, 0, get_instr_id(get_const(target, c))))
                {
                    reserveRegAddr(slice, i, j, 0, get_instr_id(get_const(target, c)), addr);
                }
                signLRFEntry(slice, i, j, 0, get_instr_id(get_const(target, c)), id);
            }
            slice = (get_next_slice(slice) == NULL ? first_slice : get_next_slice(slice));
        }
    }

    placed[id - 1][0] = 1;                         // placed ? yes
    placed[id - 1][1] = i * get_cgra_C(slice) + j; // placement coordinates: iC + j
    placed[id - 1][2] = schedule[id - 1];          // first scheduling cycle
    // placed[id - 1][3] = (schedule[id - 1] + get_instr_lat(target) - 1) - 1; // last scheduling cycle
    // if (getPEPipelineStages(slice, i, j) > 1)
    placed[id - 1][3] = schedule[id - 1] + getPEPipelineStages(slice, i, j) - 1; // last scheduling cycle
    // else
    //     placed[id - 1][3] = (schedule[id - 1] + get_instr_lat(target) - 1) - 1; // last scheduling cycle
    return 1;
}

/***********************************************************************************************************************************************
 * routeByLRF
 * Inputs:
 * Return values: Could route with LRF ? 1 : 0
 **********************************************************************************************************************************************/
int routeByLRF(stackItem *si, stack *s, cgra *c, bool ***visited, int *rfAddrCounts, int *rfAddresses,
               int t, int t1, int t2, int iid, int i1, int j1)
{
    stackItem *nsi;
    int addr, next_rfac;
    next_rfac = (getRFAccess(si->c, si->i, si->j) == 0 || getRFAccess(si->c, si->i, si->j) == iid);

    // Check for a possible connection to itself
    if (visited[si->i][si->j][t - t2] == false && get_pe_power_mode(c, si->i, si->j) == POWER_ON)
    {
        if (((t < t1) || (si->i == i1 && si->j == j1)) && next_rfac)
        {
            if ((addr = isAddressable(c, si->i, si->j, iid, si->t, rfAddrCounts[t - t2 - 1] + 1)) >= 0)
            {
                rfAddrCounts[t - t2] = rfAddrCounts[t - t2 - 1] + 1;
                rfAddresses[t - t2] = addr;
                /* printf("[%d]: input %d's addressability: %d\n", id, iid, addr); */
                if (!hasLRFEntry(si->c, si->i, si->j, si->t, iid))
                    reserveRegister(si->c, si->i, si->j, si->t, NOT_YET_COMMITTED);
                nsi = createStackItem(si->i, si->j, t, true, c);
                push(s, (Item)nsi); // push onto the stack
                return 1;
            }
        }
    }
    return 0;
}

/***********************************************************************************************************************************************
 * routeToNeighbour
 * Inputs: current stack item, the stack, the device model for the device at the next time step, the visited matrix, rfAddrCounts,
 * the time steps of interest (t1 - target, t2 - input, t next time step), as well as the coordinates for the input (i1, j1)
 * Auxiliary Function for routeInTime_forward. Searches for a valid routing path segment between two different PEs. Routing
 * for these segments is always executed through the output registers
 * Return values: Could route to neighbour ? 1 : 0
 **********************************************************************************************************************************************/
int routeToNeighbour(stackItem *si, stack *s, cgra *c, bool ***visited, int *rfAddrCounts,
                     int t, int t1, int t2, int iid, int i1, int j1)
{
    int *neighbours, i, j, k, C = get_cgra_C(c), rfac;
    stackItem *nsi;

    rfac = (getRFAccess(si->c, si->i, si->j) == 0 || getRFAccess(si->c, si->i, si->j) == iid);
    // Search for unvisited neighbours that have either the LRF or the output register free
    neighbours = getPENeighbours(si->c, si->i, si->j);

    // Check neighbouring PEs apart from itself
    for (k = 1; k <= neighbours[0]; k++)
    {
        i = neighbours[k] / C;
        j = neighbours[k] % C;
        // Skip already visited PEs
        if (visited[i][j][t - t2] == true)
            continue;

        // Skip PEs that are powered off in this clock cycle
        if (get_pe_power_mode(si->c, si->i, si->j) == POWER_OFF)
            continue;

        // If the connection is already in use (for a different value), then it can't be used for this routing
        if (connInUse(si->c, si->i, si->j, i, j) && !checkConnValTime(si->c, si->i, si->j, i, j, iid, si->t))
            continue;

        // The search arrived at the same cycle as the routing destination, but at a different PE. Cannot search further
        if ((t == t1) && (i != i1 || j != j1))
            continue;

        /**********************************************************************************************************
         * Connection is already in use by the intended value! No need to reserve it, just add the PE to the stack!
         *********************************************************************************************************/
        if (connInUse(si->c, si->i, si->j, i, j) && checkConnValTime(si->c, si->i, si->j, i, j, iid, si->t) && (rfac || si->parentReg == false))
        {
            rfAddrCounts[t1 - t] = 0;
            /* printf("Added path to (%d, %d) @ t=%d!\n", i, j, t); */
            nsi = createStackItem(i, j, t, false, c);
            push(s, (Item)nsi); // push onto the stack
            free(neighbours);
            return 1;
        }

        /**************************************************************************************************************
         * Connection is free! Add the PE to the stack! If no output register has the intended value yet, but there are
         * fre output registers, then reserve one of them for the intended value. If no output registers are free, then
         * ignore this path.
         *************************************************************************************************************/
        else if (!connInUse(si->c, si->i, si->j, i, j) && (rfac || si->parentReg == false))
        {
            int or = -1;
            if (hasOutputRegister(si->c, si->i, si->j, iid, si->t) > -1)
            {
                rfAddrCounts[t1 - t] = 0;
                nsi = createStackItem(i, j, t, false, c);
                push(s, (Item)nsi); // push onto the stack
                free(neighbours);
                return 1;
            }
            else if ((or = hasFreeOutputRegister(si->c, si->i, si->j)) > -1)
            {
                rfAddrCounts[t1 - t] = 0;
                /* printf("Added path to (%d, %d) @ t=%d!\n", i, j, t); */
                markOutputRegister(si->c, si->i, si->j, or, NOT_YET_COMMITTED, si->t); // mark the free output register as uncommitted
                nsi = createStackItem(i, j, t, false, c);
                push(s, (Item)nsi); // push onto the stack
                free(neighbours);
                return 1;
            }
        }
    }
    free(neighbours);
    return 0;
}

/***********************************************************************************************************************************************
 * routeInTime_forward
 * Inputs: device model (first slice), target and input nodes (and respective coordinates), the schedule and the II
 * Forward version of routeInTime.
 * WARNING: Reuqires getPENeighbours to read c->lats[k][pos]!
 * Return values: The generated path (stackItem**). If no path was found, a NULL pointer is returned.
 **********************************************************************************************************************************************/
stackItem **routeInTime_forward(cgra *fs, dfg_instr *target, int i1, int j1, dfg_instr *input, int i2, int j2,
                                int t2, int *schedule, int II, int recFlag, int rfaFlag)
{

    int id = get_instr_id(target), iid = get_instr_id(input), L = get_cgra_L(fs), C = get_cgra_C(fs);
    int t1 = schedule[id - 1], /* t2 = schedule[iid - 1] + get_instr_lat(input) - 1, */ t, i, j;
    int nvisited, success = 0;
    int *rfAddrCounts, *rfAddresses;

    // For recurrence edges,
    if (recFlag == 1)
    {
        // The logic here is to route the target of the next 'dist' iterations to the input of the recurrence
        t1 += II * get_rec_dist_from_instr(input, target); // input is the one with the recurrence connection marked
    }
    // If the overlap between iterations is too much, it might be impossible to even start routing the recurrence
    if (t1 <= t2)
        return NULL;

    stackItem **path = (stackItem **)calloc((t1 - t2 + 1) * C, sizeof(stackItem *));      // array that stores the various stack items of the path, in order
    stackItem **finalPath = (stackItem **)calloc((t1 - t2 + 1) * C, sizeof(stackItem *)); // final path array (inverse order of path)

    // Create a binary matrix for all nodes (visited / unvisited)
    bool ***visited = (bool ***)malloc(L * sizeof(bool **));
    for (i = 0; i < L; i++)
    {
        visited[i] = (bool **)malloc(C * sizeof(bool *));
        for (j = 0; j < C; j++)
        {
            visited[i][j] = (bool *)calloc(t1 - t2 + 1, sizeof(bool));
        }
    }
    int pathIdx = 0;

    rfAddrCounts = (int *)calloc(t1 - t2 + 1, sizeof(int));
    rfAddresses = (int *)calloc(t1 - t2 + 1, sizeof(int));

    // Start @ t2 and move forwards in time, towards t1
    cgra *c = getModuloSlice(fs, t2, II);
    stack *s = createStack(L * C * (t1 - t2 - 1) + 1); // only need to search for PEs in the interval ]t2, t1[

    // Create and push an initial stack item with the starting point of the DFS (input of the target)
    stackItem *si = createStackItem(i2, j2, t2, false, c);
    push(s, (Item *)si);

    // Create an auxiliarry stackItem with the number of nodes
    path[pathIdx++] = createStackItem(-1, -1, t1 + 1, false, NULL);

    /******************************************************************************************************************
     * DFS: Main Loop
     * For the popped PE, check which connections are free to add to the routing path
     * paths to itself (on the next timestep) must ensure that the Local Register File (LRF) is free at this timestep
     * paths to other PEs must ensure that an output register is free at this timestep
     ******************************************************************************************************************/
    while (!isEmpty(s) && !success)
    {
        si = (stackItem *)pop(s);
        // The first tile of the path, which serves only to give the size of the path, or any other invalid tile
        if (si->i == -1 || si->j == -1 || si->t > t1 || si->t < t2)
        {
            deleteStackItem(si);
            pathIdx--;
            break;
        }

        if (si != path[pathIdx - 1])
            path[pathIdx++] = si;

        c = getNextModuloSlice(si->c);
        t = si->t + 1;

        nvisited = 0; // number of PEs added to the queue at the end of this iteration

        // Mark as Visited
        visited[si->i][si->j][si->t - t2] = true;

        // Search has arrived at the target PE at the target cycle
        if (si->i == i1 && si->j == j1 && si->t == t1)
        {
            success = 1;
            break;
        }

        /***********************************************************
         * Resource re-use policy: give priority to routing by LRF
         * first (minimizing the number of PEs that feature this
         * live-value). However, if an output register has already
         * reserved this value, give priority to routing with output
         * registers (to try and re-use this reserved value!)
         **********************************************************/
        if (hasOutputRegister(si->c, si->i, si->j, iid, si->t) > -1)
        {
            // Try routing to a neighbouring PE
            nvisited += routeToNeighbour(si, s, c, visited, rfAddrCounts, t, t1, t2, iid, i1, j1);
            if (nvisited)
                continue;
            // Try routing to itself for the next time step
            nvisited += routeByLRF(si, s, c, visited, rfAddrCounts, rfAddresses, t, t1, t2, iid, i1, j1);
        }
        else
        {
            // Try routing to itself for the next time step
            nvisited += routeByLRF(si, s, c, visited, rfAddrCounts, rfAddresses, t, t1, t2, iid, i1, j1);
            if (nvisited)
                continue;
            // Try routing to a neighbouring PE
            nvisited += routeToNeighbour(si, s, c, visited, rfAddrCounts, t, t1, t2, iid, i1, j1);
        }

        // This PE doesn't provide a valid path. Remove it from the final path array
        if (nvisited == 0)
        {
            stackItem *prev;
            if (pathIdx > 0)
            {
                prev = path[pathIdx - 2]; // the reservations were made in the previous path segment
                // Free reserved (but uncommitted) resources associated to this node
                if (si->parentReg == true) // parent PE = itself (parentReg is in the LRF)
                    setUncommittedReservation(prev->c, prev->i, prev->j, prev->t, FREE);
                else // parent PE != itself (parentReg is the output Reg)
                    markUncommittedOutputRegister(prev->c, prev->i, prev->j, FREE, 0);
            }
            deleteStackItem(si);
            push(s, (Item)path[--pathIdx - 1]);
        }
    }

    for (i = 0; i < L; i++)
    {
        for (j = 0; j < C; j++)
            free(visited[i][j]);
        free(visited[i]);
    }
    free(visited);
    /**************************************************************************************************
     * A path was successfully found.
     * Free all uncommitted resource reservations that don't belong in this path, then return the path.
     **************************************************************************************************/
    if (success)
    {
        path[0]->i = pathIdx;

        while (!isEmpty(s))
        {
            si = (stackItem *)pop(s);
            setUncommittedReservation(si->c, si->i, si->j, si->t, FREE);
            markUncommittedOutputRegister(si->c, si->i, si->j, FREE, 0);
            deleteStackItem(si);
        }
        free(rfAddrCounts);
        free(rfAddresses);
        deleteStack(s);

        finalPath[0] = path[0];
        for (i = 1; i < pathIdx; i++)
        {
            finalPath[i] = path[pathIdx - i];
        }
        return finalPath;
    }
    // No path is found, stack is empty
    else
    {
        for (i = 0; i < pathIdx; i++)
        {
            if (path[i])
            {
                setUncommittedReservation(path[i]->c, path[i]->i, path[i]->j, path[i]->t, FREE);
                markUncommittedOutputRegister(path[i]->c, path[i]->i, path[i]->j, FREE, 0);
                free(path[i]);
            }
        }
        free(path);
        path = NULL; // Return NULL if no path is found
        // printf("Failed at finding a path.\n");
        free(rfAddrCounts);
        free(rfAddresses);
        free(finalPath);
        deleteStack(s);
        return path;
    }

    /* if (RF_ADDRESSING == 1 && path != NULL)
    {
        for (i = t1 - t2; i >= 0; i--)
        {
            if (rfAddrCounts[i] > 0)
            {
                addr = rfAddresses[i];

                for (j = 0; j < rfAddrCounts[i]; j++)
                {
                    swapRegister(path[i + 1 - j]->c, path[i + 1 - j]->i, path[i + 1 - j]->j, path[i + 1 - j]->t, iid, addr);
                }

                i -= rfAddrCounts[i] - 1; // -1 to account for the cases with rotating LRFs
            }
        }
    } */

    return NULL;
}

/***********************************************************************************************************************************************
 * routeInTime
 * Inputs: device model (first slice), target and input nodes (and respective coordinates), the schedule and the II
 * Routes the target node to an 'input' node. This is achieved through a time-extended DFS, starting at the target's first cycle, t1, and ending
 * at the input's last cycle, t2. [t1 > t2]
 * Routes from a PE to itself (in the previous cycle) imply storing the input value in the PE's Local Register File (LRF) in the previous cycle.
 * Thus, the route is valid if the LRF has at least one free slot for reservation.
 * Routes from a PE to another (in the previous cycle) imply storing the input value in the other PE's Output register. Thus, the route is valid
 * if the Output Register is free.
 * When analyzing these routes, the LRF and/or the Output register might have the target input value already reserved (by a previous node that
 * was already routed). In this case, this route merges with the previous route (i.e. the path is considered successful upon reaching the common
 * PE between both routes).
 * Return values: The generated path (stackItem**). If no path was found, a NULL pointer is returned.
 **********************************************************************************************************************************************/
stackItem **routeInTime(cgra *fs, dfg_instr *target, int i1, int j1, dfg_instr *input, int i2, int j2,
                        int t2, int *schedule, int II, int recFlag)
{

    int id = get_instr_id(target), iid = get_instr_id(input), L = get_cgra_L(fs), C = get_cgra_C(fs);
    int t1 = schedule[id - 1], /* t2 = schedule[iid - 1] + get_instr_lat(input) - 1, */ t, k, i, j;
    int *neighbours, nvisited, success = 0;
    int *rfAddrCounts, *rfAddresses, addr;

    // For recurrence edges,
    if (recFlag == 1)
    {
        // The logic here is to route the target of the next 'dist' iterations to the input of the recurrence
        t1 += II * get_rec_dist_from_instr(input, target); // input is the one with the recurrence connection marked
    }
    // If the overlap between iterations is too much, it might be impossible to even start routing the recurrence
    if (t1 <= t2)
        return NULL;

    stackItem **path = (stackItem **)calloc((t1 - t2 + 1) * C, sizeof(stackItem *)); // array that stores the various stack items of the path, in order

    // Create a binary matrix for all nodes (visited / unvisited)
    bool ***visited = (bool ***)malloc(L * sizeof(bool **));
    for (i = 0; i < L; i++)
    {
        visited[i] = (bool **)malloc(C * sizeof(bool *));
        for (j = 0; j < C; j++)
        {
            visited[i][j] = (bool *)calloc(t1 - t2 + 1, sizeof(bool));
        }
    }
    int pathIdx = 0, rfac, next_rfac, rfrp_flag/*, rfrpMuxIn_flag */;

    rfAddrCounts = (int *)calloc(t1 - t2 + 1, sizeof(int));
    rfAddresses = (int *)calloc(t1 - t2 + 1, sizeof(int));

    cgra *c = getModuloSlice(fs, t1, II);
    stack *s = createStack(L * C * (t1 - t2 - 1) + 1); // only need to search for PEs in the interval ]t2, t1[

    // Create and push an initial stack item with the starting point of the DFS
    stackItem *si = createStackItem(i1, j1, t1, false, c), *nsi;
    push(s, (Item *)si);

    // Create an auxiliarry stackItem with the number of nodes
    path[pathIdx++] = createStackItem(-1, -1, t1 + 1, false, NULL);

    /******************************************************************************************************************
     * DFS: Main Loop
     * For the popped PE, check which connections are free to add to the routing path
     * paths to itself (on a previous time) must ensure that the Local Register File (LRF) is free at the previous time
     * paths to other PEs must ensure that the output register is free at the previous time
     ******************************************************************************************************************/
    while (!isEmpty(s) && !success)
    {
        si = (stackItem *)pop(s);
        // The first tile of the path, which serves only to give the size of the path, or any other invalid tile
        if (si->i == -1 || si->j == -1 || si->t > t1)
        {
            deleteStackItem(si);
            pathIdx--;
            break;
        }

        if (si != path[pathIdx - 1])
            path[pathIdx++] = si;

        c = getPrevModuloSlice(si->c);
        t = si->t - 1;

        nvisited = 0; // number of PEs added to the queue at the end of this iteration

        // Mark as Visited
        // markPEVisited(si->c, si->i, si->j, si->t);
        visited[si->i][si->j][si->t - t2] = true;

        // Search has arrived at the target PE at the target cycle
        if (si->i == i2 && si->j == j2 && si->t == t2)
        {
            success = 1;
            break;
        }

        // RFAccess Control: 1 to allow routing this section -> allows, if rfa flag is enabled or the RF is not written or it is written
        // by the same input (@ this clock cycle)
        rfac = (getRFAccess(si->c, si->i, si->j) == 0 || getRFAccess(si->c, si->i, si->j) == iid);
        next_rfac = (getRFAccess(c, si->i, si->j) == 0 || getRFAccess(c, si->i, si->j) == iid) || t - t2 > 1;
        // RF Read Control, for the Ports towards the FU input muxes. @ si->t = t1, routing toward the same PE
        // will lead to the usage of a RF Read port for a mux in
        /* rfrpMuxIn_flag = si->t < t1 || getNFreeRFRPMuxIn(si->c, si->i, si->j) > 0; */
        rfrp_flag = (si->t == t1 && getNFreeRFRPMuxIn(si->c, si->i, si->j) > 0)
                    || (si->t < t1 && getNFreeRFRPOR(si->c, si->i, si->j) > 0);

        // Check for a possible connection to itself
        if (visited[si->i][si->j][t - t2] == false && get_pe_power_mode(c, si->i, si->j) == POWER_ON)
        {
            if (((t > t2) || (si->i == i2 && si->j == j2)) && next_rfac && rfrp_flag/* rfrpMuxIn_flag */)
            {

                // For now, just check if isAddressable() works
                if ((addr = isAddressable(c, si->i, si->j, iid, t, rfAddrCounts[t1 - t - 1] + 1)) >= 0)
                {
                    rfAddrCounts[t1 - t] = rfAddrCounts[t1 - t - 1] + 1;
                    rfAddresses[t1 - t] = addr;
                    /* printf("[%d]: input %d's addressability: %d\n", id, iid, addr); */
                    if (!hasLRFEntry(c, si->i, si->j, t, iid))
                        reserveRegister(c, si->i, si->j, t, NOT_YET_COMMITTED);
                    nsi = createStackItem(si->i, si->j, t, true, c);
                    push(s, (Item)nsi); // push onto the stack
                    nvisited++;
                    continue;
                }
            }
        }

        // Search for unvisited neighbours that have either the LRF or the output register free
        neighbours = getPENeighbours(si->c, si->i, si->j);

        // Check neighbouring PEs apart from itself
        for (k = 1; k <= neighbours[0]; k++)
        {
            i = neighbours[k] / C;
            j = neighbours[k] % C;
            // Skip already visited PEs
            if (visited[i][j][t - t2] == true)
                continue;

            // Skip PEs that are powered off in this clock cycle
            if (get_pe_power_mode(c, i, j) == POWER_OFF)
                continue;

            // If the connection is already in use (for a different value), then it can't be used for this routing
            if (connInUse(si->c, si->i, si->j, i, j) && !checkConnValTime(si->c, si->i, si->j, i, j, iid, t))
                continue;

            // The search arrived at the same cycle as the routing destination, but at a different PE. Cannot search further
            if ((t == t2) && (i != i2 || j != j2))
                continue;

            // When routing to a different PE, make sure that it is not being written to (by another value) and the OR is free
            if (SOFIA == 1 && ((getRFAccess(c, i, j) != 0 && getRFAccess(c, i, j) != iid) || (hasFreeOutputRegister(si->c, si->i, si->j) == -1 && hasOutputRegister(si->c, si->i, si->j, iid, si->t) == -1 && hasOutputRegister(si->c, si->i, si->j, NOT_YET_COMMITTED, si->t) == -1 && !isOutputStreamPort(si->c, si->i, si->j))))
                continue;

            /**********************************************************************************************************
             * Connection is already in use by the intended value! No need to reserve it, just add the PE to the stack!
             *********************************************************************************************************/
            if (connInUse(si->c, si->i, si->j, i, j) && checkConnValTime(si->c, si->i, si->j, i, j, iid, t) && (rfac || si->parentReg == false))
            {
                rfAddrCounts[t1 - t] = 0;
                nsi = createStackItem(i, j, t, false, c);
                push(s, (Item)nsi); // push onto the stack
                nvisited++;
                break;
            }
            /**************************************************************************************************************
             * Connection is free! Add the PE to the stack! If no output register has the intended value yet, but there are
             * fre output registers, then reserve one of them for the intended value. If no output registers are free, then
             * ignore this path.
             *************************************************************************************************************/
            else if (!connInUse(si->c, si->i, si->j, i, j) && (rfac || si->parentReg == false))
            {
                int or = -1;
                if (hasOutputRegister(c, i, j, iid, t) > -1)
                {
                    rfAddrCounts[t1 - t] = 0;
                    nsi = createStackItem(i, j, t, false, c);
                    push(s, (Item)nsi); // push onto the stack
                    nvisited++;
                    break;
                }
                else if ((or = hasFreeOutputRegister(c, i, j)) > -1)
                {
                    rfAddrCounts[t1 - t] = 0;
                    /* printf("Added path to (%d, %d) @ t=%d!\n", i, j, t); */
                    markOutputRegister(c, i, j, or, NOT_YET_COMMITTED, t); // mark the free output register as uncommitted
                    nsi = createStackItem(i, j, t, false, c);
                    push(s, (Item)nsi); // push onto the stack
                    nvisited++;
                    break;
                }
            }
        }
        free(neighbours);

        // This PE doesn't provide a valid path. Remove it from the final path array
        if (nvisited == 0)
        {
            /* printf("path (%d,%d) @t=%d led nowhere!\n", si->i, si->j, si->t); */
            // Free reserved (but uncommitted) resources associated to this node
            if (si->parentReg == true) // parent PE = itself (parentReg is in the LRF)
                setUncommittedReservation(si->c, si->i, si->j, si->t, FREE);
            else // parent PE != itself (parentReg is the output Reg)
                markUncommittedOutputRegister(si->c, si->i, si->j, FREE, 0);
            deleteStackItem(si);
            push(s, (Item)path[--pathIdx - 1]);
        }
    }

    for (i = 0; i < L; i++)
    {
        for (j = 0; j < C; j++)
            free(visited[i][j]);
        free(visited[i]);
    }
    free(visited);
    /**************************************************************************************************
     * A path was successfully found.
     * Free all uncommitted resource reservations that don't belong in this path, then return the path.
     **************************************************************************************************/
    if (success)
    {
        path[0]->i = pathIdx;

        while (!isEmpty(s))
        {
            si = (stackItem *)pop(s);
            setUncommittedReservation(si->c, si->i, si->j, si->t, FREE);
            markUncommittedOutputRegister(si->c, si->i, si->j, FREE, 0);
            deleteStackItem(si);
        }
    }
    // No path is found, stack is empty
    else
    {
        for (i = 0; i < pathIdx; i++)
        {
            if (path[i])
            {
                setUncommittedReservation(path[i]->c, path[i]->i, path[i]->j, path[i]->t, FREE);
                markUncommittedOutputRegister(path[i]->c, path[i]->i, path[i]->j, FREE, 0);
                free(path[i]);
            }
        }
        free(path);
        path = NULL; // Return NULL if no path is found
        /* printf("Failed at finding a path.\n"); */
    }

    if (path != NULL)
    {
        for (i = t1 - t2; i >= 0; i--)
        {
            if (rfAddrCounts[i] > 0)
            {
                addr = rfAddresses[i];

                for (j = 0; j < rfAddrCounts[i]; j++)
                {
                    swapRegister(path[i + 1 - j]->c, path[i + 1 - j]->i, path[i + 1 - j]->j, path[i + 1 - j]->t, iid, addr);
                }

                i -= rfAddrCounts[i] - 1; // -1 to account for the cases with rotating LRFs
            }
        }
    }

    free(rfAddrCounts);
    free(rfAddresses);
    deleteStack(s);
    return path;
}

/************************************************************************************************************************************************
 * defineInputRoutingOrder
 * Inputs: device model (first slice), target node and the placement info array (placed)
 * Returns a list with the inputs. It is sorted by minimum manhattan distance to the target.
 * Return values: Sorted input array
 **********************************************************************************************************************************************/
dfg_instr **defineInputRoutingOrder(cgra *c, dfg_instr *target, int **placed)
{

    MinHeapNode **pq = (MinHeapNode **)malloc(get_n_inputs(target) * sizeof(MinHeapNode *));
    dfg_instr **orderedInputs = (dfg_instr **)calloc(get_n_inputs(target), sizeof(dfg_instr *));

    int i_target = placed[get_instr_id(target) - 1][1] / get_cgra_C(c), j_target = placed[get_instr_id(target) - 1][1] % get_cgra_C(c);
    int i, j;
    for (int k = 0; k < get_n_inputs(target); k++)
    {
        int id = get_instr_id(get_input(target, k));
        i = placed[id - 1][1] / get_cgra_C(c);
        j = placed[id - 1][1] % get_cgra_C(c);
        pq[k] = newMinHeapNode(id, abs(i - i_target) + abs(j - j_target));
    }
    qsort(pq, get_n_inputs(target), sizeof(MinHeapNode *), comparePQ);

    for (int k = 0; k < get_n_inputs(target); k++)
    {
        orderedInputs[k] = get_input_by_op_id(target, pq[k]->vertex);
        /* printf("(%d) input[%d] = %d\n", get_instr_id(target), k, pq[k]->vertex); */
    }
    for (int k = 0; k < get_n_inputs(target); k++)
        free(pq[k]);
    free(pq);
    return orderedInputs;
}

/************************************************************************************************************************************************
 * routeOp
 * Inputs: device model (first slice), target node, the placement info array (placed), the schedule and the II
 * Routes the target node to all of its inputs. Uses routeInTime to route the target to each input, separately. The valid generated routes, which
 * are still Not Yet Committed, are marked as "Almost Committed", to avoid being partially rewritten by the future routes within this function
 * and to still prevent overlapping routes to be generated. If the routing to one of the target's inputs fails, all already generated routes are
 * deleted.
 * Return values: Routed ? 1 : 0
 **********************************************************************************************************************************************/
int routeOp(cgra *first_slice, dfg_instr *target, int **placed, int *schedule, int II)
{

    int i, j, k, N, C = get_cgra_C(first_slice), id = get_instr_id(target), iid;
    int i1 = placed[id - 1][1] / C, j1 = placed[id - 1][1] % C;
    int i2, j2;

    // Target was not yet placed
    if (placed[id - 1][0] == 0)
        return 0;

    // No inputs to route the target to
    if (get_n_inputs(target) == 0)
        return 1;

    // Define the input routing order
    dfg_instr **input_order = defineInputRoutingOrder(first_slice, target, placed);

    // Array of paths
    stackItem ***paths = (stackItem ***)malloc(get_n_inputs(target) * sizeof(stackItem **));
    stackItem ***recurrence_paths = (stackItem ***)malloc(get_n_recurrences(target) * sizeof(stackItem **));
    stackItem *si, *next;

    for (k = 0; k < get_n_inputs(target); k++)
    {
        iid = get_instr_id(input_order[k]);
        // if the input wasn't mapped, skip the routing
        if (placed[iid - 1][0] == 0)
        {
            // printf("Target node %d's input node %d has not been mapped yet. Cannot route.\n", id, iid);
            break;
        }
        i2 = placed[iid - 1][1] / C;
        j2 = placed[iid - 1][1] % C;
        paths[k] = routeInTime(first_slice, target, i1, j1, input_order[k], i2, j2, placed[iid - 1][3], schedule, II, 0);

        // Failed to route to input k
        if (paths[k] == NULL)
            break;

        /**************************************************************************************************
         * Mark the entire path as almost committed
         * If it stays uncommitted it may be changed during the next routeInTime
         * However, it cannot be commited just yet. If routing with another input fails, it is necessary to
         * remove this route aswell
         **************************************************************************************************/
        si = paths[k][1];
        N = paths[k][0]->i;
        for (i = 2; i < N; i++)
        {
            next = paths[k][i];
            // Route to itself
            if (next->i == si->i && next->j == si->j)
            {
                setUncommittedReservation(next->c, next->i, next->j, next->t, ALMOST_COMMITTED - iid);
                signLRFEntry(next->c, next->i, next->j, next->t, ALMOST_COMMITTED - iid, id);
                if (i == N - 1 && getRFAccess(next->c, next->i, next->j) == 0)
                { // start of route and it routes to itself -> mark RF Access
                    setRFAccess(next->c, next->i, next->j, ALMOST_COMMITTED - iid);
                }
                // Input is consumed by the target op through the LRF -> port to FU mux_in is reserved!
                if (i == 2)
                {
                    int rfread = reserveRFReadPort(si->c, si->i, si->j, 0, iid, next->t);
                    if (rfread == 0)
                        printf("ERROR: path should be valid, but exceeds RF read ports to mux_in.\n");
                }
                else if (si->parentReg == false) // routes from this PE's RF to outside (therefore reads to the output register)
                {
                    int rfread = reserveRFReadPort(si->c, si->i, si->j, 1, iid, next->t);
                    if (rfread == 0)
                        printf("ERROR: path should be valid, but exceeds RF read ports to output registers.\n");
                }
            }
            // Route to a different PE
            else
            {
                markUncommittedOutputRegister(next->c, next->i, next->j, ALMOST_COMMITTED - iid, next->t);
                add_conn_state(si->c, si->i * C + si->j, next->i * C + next->j, id);
                // New unmarked connection -> lock it to a given data value and time
                if (getConnVal(si->c, si->i, si->j, next->i, next->j) == 0)
                {
                    setConnValTime(si->c, si->i, si->j, next->i, next->j, iid, next->t);
                }
                if (si->parentReg == true && getRFAccess(si->c, si->i, si->j) == 0) // routes from outside to this PE's RF
                {
                    setRFAccess(si->c, si->i, si->j, ALMOST_COMMITTED - iid);
                }
            }
            si = next;
        }
    }

    // Failed to route input k, therefore the placement of target is invalid
    if (k < get_n_inputs(target))
    {
        for (j = 0; j < k; j++)
        {
            iid = get_instr_id(input_order[j]);
            si = paths[j][1];
            N = paths[j][0]->i;
            deleteStackItem(paths[j][0]);
            for (i = 2; i < N; i++)
            {
                next = paths[j][i];
                // Route to itself
                if (next->i == si->i && next->j == si->j)
                {
                    unsignLRFEntry(next->c, next->i, next->j, next->t, ALMOST_COMMITTED - iid, id);
                    // changeSetReservation(next->c, next->i, next->j, ALMOST_COMMITTED-iid, FREE);
                    if (i == N - 1 && getRFAccess(next->c, next->i, next->j) == ALMOST_COMMITTED - iid)
                    { // start of route and it routes to itself -> mark RF Access
                        setRFAccess(next->c, next->i, next->j, 0);
                    }
                    if (i == 2)
                    {
                        // All RF RPs used to consume data for the target op are freed
                        int rfread = removeRFRPReservationMuxIn(si->c, si->i, si->j, iid, next->t);
                        if (rfread == 0)
                            printf("ERROR: failed to free a reserved RF read port.\n");
                    }
                    else if (si->parentReg == false && hasConnectedPEsWithVal(si->c, si->i, si->j, iid, si->t) < 0)
                    {
                        int rfread = removeRFRPReservationOR(si->c, si->i, si->j, iid, next->t);
                        if (rfread == 0)
                            printf("ERROR: failed to free a reserved RF read port (output register).\n");
                    }
                }
                // Route to a different PE
                else
                {
                    remove_conn_state(si->c, si->i * C + si->j, next->i * C + next->j, id);
                    if (!connInUse(si->c, si->i, si->j, next->i, next->j))
                    {
                        setConnValTime(si->c, si->i, si->j, next->i, next->j, FREE, 0);
                    }

                    if (hasConnectedPEsWithVal(next->c, next->i, next->j, iid, next->t) < 0)
                    {
                        // this output register is not used anymore, so free it
                        int or = hasOutputRegister(next->c, next->i, next->j, iid, next->t);
                        if (or < 0)
                            or = hasOutputRegister(next->c, next->i, next->j, ALMOST_COMMITTED - iid, next->t);
                        if (or < 0)
                            or = hasOutputRegister(next->c, next->i, next->j, NOT_YET_COMMITTED, next->t);
                        markOutputRegister(next->c, next->i, next->j, or, FREE, 0);
                    }
                    if (si->parentReg == true && getRFAccess(si->c, si->i, si->j) == ALMOST_COMMITTED - iid) // routes from outside to this PE's RF
                    {
                        setRFAccess(si->c, si->i, si->j, 0);
                    }
                }
                deleteStackItem(si);
                si = next;
            }
            deleteStackItem(si);
            free(paths[j]);
        }
        // Reserve the necessary LRF entries
        int consts = get_n_consts(target);
        for (int t = 0; t < II; t++)
        {
            for (int c = 0; c < consts; c++)
            {
                unsignLRFEntry(first_slice, i1, j1, t, get_instr_id(get_const(target, c)), id);
            }
            first_slice = (get_next_slice(first_slice) == NULL ? first_slice : get_next_slice(first_slice));
        }
        free(input_order);
        free(paths);
        free(recurrence_paths);
        return 0;
    }
    else
    {

        // The basic inputs were routed successfully. Now, let's try to route the recurrence edges!
        // Of course, only recurrence edges between operations shall be routed. Recurrences between IOs can be
        // ignored, as they are assumed to be solved by sending the output to the Streaming Engine
        if (!isIO(target))
        {
            for (k = 0; k < get_n_recurrences(target); k++)
            {
                iid = get_instr_id(get_recurrence(target, k));
                // if the input wasn't mapped, skip the routing
                if (placed[iid - 1][0] == 0)
                    continue;
                i2 = placed[iid - 1][1] / C;
                j2 = placed[iid - 1][1] % C;
                // Route the the node to the target (target is the input of the recurrence)
                recurrence_paths[k] = routeInTime(first_slice, get_recurrence(target, k), i2, j2, target, i1, j1, placed[id - 1][3], schedule, II, 1);

                // Failed to route the recurrence k
                if (recurrence_paths[k] == NULL)
                {
                    /* printf("failed @ routing the recurrence :(\n"); */
                    break;
                }

                /**************************************************************************************************
                 * Mark the entire path as almost committed
                 * If it stays uncommitted it may be changed during the next routeInTime
                 * However, it cannot be commited just yet. If routing with another input fails, it is necessary to
                 * remove this route aswell
                 **************************************************************************************************/
                si = recurrence_paths[k][1];
                N = recurrence_paths[k][0]->i;
                for (i = 2; i < N; i++)
                {
                    next = recurrence_paths[k][i];
                    // Route to itself
                    if (next->i == si->i && next->j == si->j)
                    {
                        setUncommittedReservation(next->c, next->i, next->j, next->t, ALMOST_COMMITTED - iid);
                        signLRFEntry(next->c, next->i, next->j, next->t, ALMOST_COMMITTED - iid, iid);
                        if (i == N - 1 && getRFAccess(next->c, next->i, next->j) == 0)
                        { // start of route and it routes to itself -> mark RF Access
                            setRFAccess(next->c, next->i, next->j, ALMOST_COMMITTED - id);
                        }
                        // Input is consumed by the target op through the LRF -> port to FU mux_in is reserved!
                        if (i == 2)
                        {
                            int rfread = reserveRFReadPort(si->c, si->i, si->j, 0, id, next->t);
                            if (rfread == 0)
                                printf("ERROR: rec path should be valid, but exceeds RF read ports to mux_in.\n");
                        }
                        else if (si->parentReg == false) // routes from this PE's RF to outside (therefore reads to the output register)
                        {
                            int rfread = reserveRFReadPort(si->c, si->i, si->j, 1, id, next->t);
                            if (rfread == 0)
                                printf("ERROR: rec path should be valid, but exceeds RF read ports to output registers.\n");
                        }
                    }
                    // Route to a different PE
                    else
                    {
                        markUncommittedOutputRegister(next->c, next->i, next->j, ALMOST_COMMITTED - iid, next->t);
                        add_conn_state(si->c, si->i * C + si->j, next->i * C + next->j, iid);
                        // New unmarked connection -> lock it to a given data value and time
                        if (getConnVal(si->c, si->i, si->j, next->i, next->j) == 0)
                        {
                            setConnValTime(si->c, si->i, si->j, next->i, next->j, id, next->t);
                        }
                        if (si->parentReg == true && getRFAccess(si->c, si->i, si->j) == 0) // routes from outside to this PE's RF
                        {
                            setRFAccess(si->c, si->i, si->j, ALMOST_COMMITTED - id);
                        }
                    }
                    si = next;
                }
            }

            if (k < get_n_recurrences(target))
            {
                /* printf("failed at mapping the recurrence of node %d :(\n", get_instr_id(target)); */
                // Remove the routes for the recurrences
                for (j = 0; j < k; j++)
                {
                    iid = get_instr_id(get_recurrence(target, j));
                    si = recurrence_paths[j][1];
                    N = recurrence_paths[j][0]->i;
                    deleteStackItem(recurrence_paths[j][0]);
                    for (i = 2; i < N; i++)
                    {
                        next = recurrence_paths[j][i];
                        // Route to itself
                        if (next->i == si->i && next->j == si->j)
                        {
                            unsignLRFEntry(next->c, next->i, next->j, next->t, ALMOST_COMMITTED - iid, iid);
                            // changeSetReservation(next->c, next->i, next->j, ALMOST_COMMITTED-iid, FREE);
                            if (i == N - 1 && getRFAccess(next->c, next->i, next->j) == ALMOST_COMMITTED - id)
                            { // start of route and it routes to itself -> mark RF Access
                                setRFAccess(next->c, next->i, next->j, 0);
                            }
                            if (i == 2)
                            {
                                // All RF RPs used to consume data for the target op are freed
                                int rfread = removeRFRPReservationMuxIn(si->c, si->i, si->j, id, next->t);
                                if (rfread == 0)
                                    printf("ERROR: failed to free a reserved RF read port for a rec.\n");
                            }
                            else if (si->parentReg == false && hasConnectedPEsWithVal(si->c, si->i, si->j, id, si->t) < 0)
                            {
                                int rfread = removeRFRPReservationOR(si->c, si->i, si->j, id, next->t);
                                if (rfread == 0)
                                    printf("ERROR: failed to free a reserved RF read port for a rec (output register).\n");
                            }
                        }
                        // Route to a different PE
                        else
                        {
                            remove_conn_state(si->c, si->i * C + si->j, next->i * C + next->j, iid);
                            if (!connInUse(si->c, si->i, si->j, next->i, next->j))
                            {
                                setConnValTime(si->c, si->i, si->j, next->i, next->j, FREE, 0);
                            }

                            if (hasConnectedPEsWithVal(next->c, next->i, next->j, id, next->t) < 0)
                            {
                                // this output register is not used anymore, so free it
                                int or = hasOutputRegister(next->c, next->i, next->j, id, next->t);
                                if (or < 0)
                                    or = hasOutputRegister(next->c, next->i, next->j, ALMOST_COMMITTED - iid, next->t);
                                if (or < 0)
                                    or = hasOutputRegister(next->c, next->i, next->j, NOT_YET_COMMITTED, next->t);
                                markOutputRegister(next->c, next->i, next->j, or, FREE, 0);
                            }
                            if (si->parentReg == true && getRFAccess(si->c, si->i, si->j) == ALMOST_COMMITTED - id) // routes from outside to this PE's RF
                            {
                                setRFAccess(si->c, si->i, si->j, 0);
                            }
                        }
                        deleteStackItem(si);
                        si = next;
                    }
                    deleteStackItem(si);
                    free(recurrence_paths[j]);
                }
                free(recurrence_paths);

                // Remove the routes for the inputs
                for (j = 0; j < get_n_inputs(target); j++)
                {
                    iid = get_instr_id(input_order[j]);
                    si = paths[j][1];
                    N = paths[j][0]->i;
                    deleteStackItem(paths[j][0]);
                    for (i = 2; i < N; i++)
                    {
                        next = paths[j][i];
                        // Route to itself
                        if (next->i == si->i && next->j == si->j)
                        {
                            unsignLRFEntry(next->c, next->i, next->j, next->t, ALMOST_COMMITTED - iid, id);
                            // changeSetReservation(next->c, next->i, next->j, ALMOST_COMMITTED-iid, FREE);
                            if (i == N - 1 && getRFAccess(next->c, next->i, next->j) == ALMOST_COMMITTED - iid)
                            { // start of route and it routes to itself -> mark RF Access
                                setRFAccess(next->c, next->i, next->j, 0);
                            }
                            if (i == 2)
                            {
                                // All RF RPs used to consume data for the target op are freed
                                int rfread = removeRFRPReservationMuxIn(si->c, si->i, si->j, iid, next->t);
                                if (rfread == 0)
                                    printf("ERROR: failed to free a reserved RF read port.\n");
                            }
                            else if (si->parentReg == false && hasConnectedPEsWithVal(si->c, si->i, si->j, iid, si->t) < 0)
                            {
                                int rfread = removeRFRPReservationOR(si->c, si->i, si->j, iid, next->t);
                                if (rfread == 0)
                                    printf("ERROR: failed to free a reserved RF read port (output register).\n");
                            }
                        }
                        // Route to a different PE
                        else
                        {
                            remove_conn_state(si->c, si->i * C + si->j, next->i * C + next->j, id);
                            if (!connInUse(si->c, si->i, si->j, next->i, next->j))
                            {
                                setConnValTime(si->c, si->i, si->j, next->i, next->j, FREE, 0);
                            }

                            if (hasConnectedPEsWithVal(next->c, next->i, next->j, iid, next->t) < 0)
                            {
                                // this output register is not used anymore, so free it
                                int or = hasOutputRegister(next->c, next->i, next->j, iid, next->t);
                                if (or < 0)
                                    or = hasOutputRegister(next->c, next->i, next->j, ALMOST_COMMITTED - iid, next->t);
                                if (or < 0)
                                    or = hasOutputRegister(next->c, next->i, next->j, NOT_YET_COMMITTED, next->t);
                                markOutputRegister(next->c, next->i, next->j, or, FREE, 0);
                            }
                            if (si->parentReg == true && getRFAccess(si->c, si->i, si->j) == ALMOST_COMMITTED - iid) // routes from outside to this PE's RF
                            {
                                setRFAccess(si->c, si->i, si->j, 0);
                            }
                        }
                        deleteStackItem(si);
                        si = next;
                    }
                    deleteStackItem(si);
                    free(paths[j]);
                }
                // Reserve the necessary LRF entries
                int consts = get_n_consts(target);
                for (int t = 0; t < II; t++)
                {
                    for (int c = 0; c < consts; c++)
                    {
                        unsignLRFEntry(first_slice, i1, j1, t, get_instr_id(get_const(target, c)), id);
                    }
                    first_slice = (get_next_slice(first_slice) == NULL ? first_slice : get_next_slice(first_slice));
                }
                free(paths);
                free(input_order);
                return 0;
            }
        }

        // Routing was successful. Commit all generated paths
        for (k = 0; k < get_n_inputs(target); k++)
        {
            si = paths[k][1];
            N = paths[k][0]->i;
            deleteStackItem(paths[k][0]);
            iid = get_instr_id(input_order[k]);
            for (i = 2; i < N; i++)
            {
                next = paths[k][i];
                // Route to itself
                if (next->i == si->i && next->j == si->j)
                {
                    changeSetReservation(next->c, next->i, next->j, ALMOST_COMMITTED - iid, iid);
                    if (i == N - 1)
                    {
                        setRFAccess(next->c, next->i, next->j, iid);
                    }
                }
                // Route to a different PE
                else
                {
                    int or = hasOutputRegister(next->c, next->i, next->j, iid, next->t);
                    if (or < 0)
                    {
                        or = hasOutputRegister(next->c, next->i, next->j, ALMOST_COMMITTED - iid, next->t);
                        if (or < 0)
                            or = hasOutputRegister(next->c, next->i, next->j, NOT_YET_COMMITTED, next->t);
                        markOutputRegister(next->c, next->i, next->j, or, iid, next->t);
                    }
                    // set_grid_state(si->c, si->i * C + si->j, next->i * C + next->j, iid);
                    if (si->parentReg == true)
                    {
                        setRFAccess(si->c, si->i, si->j, iid);
                    }
                }
                deleteStackItem(si);
                si = next;
            }
            deleteStackItem(si);
            free(paths[k]);
        }
        free(paths);
        free(input_order);

        // Commit all generated paths regarding recurrences
        for (k = 0; k < get_n_recurrences(target); k++)
        {
            iid = get_instr_id(get_recurrence(target, k));
            si = recurrence_paths[k][1];
            N = recurrence_paths[k][0]->i;
            deleteStackItem(recurrence_paths[k][0]);
            for (i = 2; i < N; i++)
            {
                next = recurrence_paths[k][i];
                // Route to itself
                if (next->i == si->i && next->j == si->j)
                {
                    changeSetReservation(next->c, next->i, next->j, ALMOST_COMMITTED - iid, id);
                    if (i == N - 1)
                    {
                        setRFAccess(next->c, next->i, next->j, id);
                    }
                }
                // Route to a different PE
                else
                {
                    int or = hasOutputRegister(next->c, next->i, next->j, id, next->t);
                    if (or < 0)
                    {
                        or = hasOutputRegister(next->c, next->i, next->j, ALMOST_COMMITTED - iid, next->t);
                        if (or < 0)
                            or = hasOutputRegister(next->c, next->i, next->j, NOT_YET_COMMITTED, next->t);
                        markOutputRegister(next->c, next->i, next->j, or, id, next->t);
                    }
                    if (si->parentReg == true)
                    {
                        setRFAccess(si->c, si->i, si->j, id);
                    }
                    // set_grid_state(si->c, si->i * C + si->j, next->i * C + next->j, iid);
                }
                deleteStackItem(si);
                si = next;
            }
            deleteStackItem(si);
            free(recurrence_paths[k]);
        }
        free(recurrence_paths);
        return 1;
    }
}

/************************************************************************************************************************************************
 * unmapOp
 * Inputs: device model (first slice), target node, the placement info array (placed), the schedule and the II
 * Unmaps the target node from the device. To achieve this, first all routes between the target and its inputs are unregistered. If those routes
 * were only reserved by the target, then they are also removed. Lastly, the target node is removed from the device.
 * Return values: unmapped ? 1 : 0
 **********************************************************************************************************************************************/
int unmapOp(cgra *first_slice, dfg *d, dfg_instr *target, int **placed, int *schedule, int II)
{

    if (target == NULL)
        return 0;

    int i, j, t, c, consts = get_n_consts(target), ti, k, maxPlacements, id = get_instr_id(target), iid, tt = schedule[id - 1], pos = placed[id - 1][1], p, ii, jj, or;
    bool parentReg;
    cgra *prev, *curr, *targetSlice = getModuloSlice(first_slice, schedule[id - 1], II);

    // Op is not placed
    if (placed[id - 1][0] == 0)
        return 1; // Automatically unmapped
                  /* printf("unmapping: %d\n",get_instr_id(target)); */
    // Unmap routes to inputs

    for (k = 0; k < get_n_inputs(target); k++)
    {
        iid = get_instr_id(get_input(target, k));
        ti = schedule[iid - 1] + get_instr_lat(get_input(target, k)) - 1; // final schedule time of the input
        t = schedule[id - 1];
        i = pos / get_cgra_C(first_slice);
        j = pos % get_cgra_C(first_slice);
        curr = targetSlice;
        parentReg = false;

        while (t > ti)
        {
            /* printf("t = %d (%d)\n", t, ti); */
            t--;
            prev = getPrevModuloSlice(curr);

            // Check for connections to itself
            if (hasLRFEntry(prev, i, j, t, iid) /*  && entrySignedBy(prev, i, j, t, iid, id) */)
            {
                /* printf("yep, %d has %d stored in the LRF (previous cycle)\n", id, iid); */
                // Unsign the target from this reservation. If no other targets are reserving this, then the reservation is freed.
                unsignLRFEntry(prev, i, j, t, iid, id);
                // Remove RF Read Reservation
                if (t == tt - 1)
                {
                    removeRFRPReservationMuxIn(curr, i, j, iid, t);
                }
                else if (parentReg == false && hasConnectedPEsWithVal(curr, i, j, iid, t + 1) < 0 && t < tt - 1)
                {
                    removeRFRPReservationOR(curr, i, j, iid, t);
                }
                parentReg = true;
            }
            // Check for connections to other PEs
            else
            {
                p = isConnectedToPE(curr, i, j, id, iid, t);
                ii = p / get_cgra_C(first_slice);
                jj = p % get_cgra_C(first_slice);
                or = hasOutputRegister(prev, ii, jj, iid, t);
                // A connection is reserved by op and output register has the intended value!
                if (p > -1 && or >= 0)
                {
                    /* printf("Oh yeah, PE(%d,%d) has the OR set to %d!\n", ii, jj, iid); */
                    // Remove our reservation to the connection
                    remove_conn_state(curr, i * get_cgra_C(first_slice) + j, ii * get_cgra_C(first_slice) + jj, id);
                    if (!connInUse(curr, i, j, ii, jj))
                    {
                        setConnValTime(curr, i, j, ii, jj, FREE, 0);
                    }
                    // If there are now now more connections linking to this output register, free it
                    // if (!connInUse(curr, i, j, ii, jj))
                    if (hasConnectedPEsWithVal(prev, ii, jj, iid, t) < 0)
                    {
                        /* printf("PE(%d,%d) isn't outputting %d anywhere else!\n", ii, jj, iid); */
                        markOutputRegister(prev, ii, jj, or, FREE, 0);
                    }
                    i = ii;
                    j = jj;
                    parentReg = false;
                }
                /* else
                    printf("found no routes to clean...\n"); */
            }

            curr = prev;
        }
    }

    // Unmap recurrence routes
    for (k = 0; k < get_n_recurrences(target); k++)
    {
        iid = get_instr_id(get_recurrence(target, k));
        ti = schedule[iid - 1] + II * get_rec_dist_from_instr(target, get_recurrence(target, k)); // final schedule time of the input
        tt = ti;
        t = schedule[id - 1] + get_instr_lat(target) - 1;
        i = placed[iid - 1][1] / get_cgra_C(first_slice);
        j = placed[iid - 1][1] % get_cgra_C(first_slice);
        curr = getModuloSlice(first_slice, schedule[iid - 1], II);
        parentReg = false;

        while (t < ti)
        {
            /* printf("t = %d (%d)\n", t, ti); */
            ti--;
            prev = getPrevModuloSlice(curr);

            // Check for connections to itself
            if (hasLRFEntry(prev, i, j, ti, id) /*  && entrySignedBy(prev, i, j, t, iid, id) */)
            {
                /* printf("yep, %d has %d stored in the LRF (previous cycle)\n", id, iid); */
                // Unsign the target from this reservation. If no other targets are reserving this, then the reservation is freed.
                unsignLRFEntry(prev, i, j, ti, id, iid);
                // Remove RF Read Reservation
                if (ti == tt - 1)
                {
                    removeRFRPReservationMuxIn(curr, i, j, id, ti);
                }
                else if (parentReg == false && hasConnectedPEsWithVal(curr, i, j, id, ti + 1) < 0 && ti < tt - 1)
                {
                    removeRFRPReservationOR(curr, i, j, id, ti);
                }
                parentReg = true;
            }
            // Check for connections to other PEs
            else
            {
                p = isConnectedToPE(curr, i, j, iid, id, ti);
                ii = p / get_cgra_C(first_slice);
                jj = p % get_cgra_C(first_slice);
                or = hasOutputRegister(prev, ii, jj, id, ti);
                // A connection is reserved by op and output register has the intended value!
                if (p > -1 && or >= 0)
                {
                    /* printf("Oh yeah, PE(%d,%d) has the OR set to %d!\n", ii, jj, iid); */
                    // Remove our reservation to the connection
                    remove_conn_state(curr, i * get_cgra_C(first_slice) + j, ii * get_cgra_C(first_slice) + jj, iid);
                    if (!connInUse(curr, i, j, ii, jj))
                    {
                        setConnValTime(curr, i, j, ii, jj, FREE, 0);
                    }
                    // If there are now now more connections linking to this output register, free it
                    // if (!connInUse(curr, i, j, ii, jj))
                    if (hasConnectedPEsWithVal(prev, ii, jj, id, ti) < 0)
                    {
                        /* printf("PE(%d,%d) isn't outputting %d anywhere else!\n", ii, jj, iid); */
                        markOutputRegister(prev, ii, jj, or, FREE, 0);
                    }
                    i = ii;
                    j = jj;
                    parentReg = false;
                }
                /* else
                    printf("found no routes to clean...\n"); */
            }

            curr = prev;
        }
    }

    // Unplace the target node
    maxPlacements = II < get_instr_lat(target) ? II : get_instr_lat(target);
    curr = targetSlice;
    i = pos / get_cgra_C(first_slice);
    j = pos % get_cgra_C(first_slice);
    for (t = 0; t < maxPlacements; t++)
    {
        if (get_cgra_tile_value(curr, i, j) == id)
        {
            set_cgra_tile(curr, i, j, NULL);
            set_cgra_value(curr, 0, i, j);
        }
        else
        {
            printf("target node %d is not placed in the supposed PE.\n", id);
            return 0;
        }
        curr = getNextModuloSlice(curr);
    }

    for (c = 0; c < consts; c++)
    {
        removeRFRPReservationMuxIn(targetSlice, i, j, get_instr_id(get_const(target, c)), 0);
    }

    // Reserve the necessary LRF entries
    curr = first_slice;
    for (t = 0; t < II; t++)
    {
        for (c = 0; c < consts; c++)
        {
            if (getCUsize(curr, i, j) > 0)
                unsignCUEntry(curr, i, j, get_instr_id(get_const(target, c)), id);
            else
            {
                // Old constant management
                unsignLRFEntry(curr, i, j, 0, get_instr_id(get_const(target, c)), id);
            }
        }
        curr = (get_next_slice(curr) == NULL ? first_slice : get_next_slice(curr));
    }

    placed[id - 1][0] = 0;
    // for now, keep the old information, so that it can be used to unmap recurrence edges
    /* placed[id - 1][1] = 0;
    placed[id - 1][2] = 0;
    placed[id - 1][3] = 0; */

    // If op was placed on a pipelined FU -> schedule of the succeeding nodes might have been affected: correct it!
    if (getPEPipelineStages(first_slice, i, j) > 1)
    {
        // some "correct scheduling" function

        // should be more or less the inverse of the pipelineRescheduling function

        // should track all nodes (DFS-style) and subtract the scheduling by [#PP - 1] (if possible - checking this should be
        //                                                                             similar to checking for "necessity" of
        //                                                                             re-scheduling in the sister function)
        invertPipelineReschedule(schedule, first_slice, d, target, placed, getPEPipelineStages(first_slice, i, j) - 1, II);
    }

    return 1;
}

/************************************************************************************************************************************************
 * unRouteOutputs
 * Inputs: device model (first slice), target node, the placement info array (placed), the schedule and the II
 * Removes the routes from the target node to its outputs. This is achieved by unmapping each mapped output and placing it again, on the same
 * tile as before.
 * Return values: void
 **********************************************************************************************************************************************/
void unRouteOutputs(cgra *first_slice, dfg *d, dfg_instr *target, int **placed, int *schedule, int II)
{

    int o, n_outputs = get_n_outputs(target), oid;
    dfg_instr *output;
    // Unmap the target and remove its outputs' routes
    for (o = 0; o < n_outputs; o++)
    {
        output = get_output(target, o);
        oid = get_instr_id(output);

        // Output has not yet been placed (this will include the target)
        if (placed[oid - 1][0] == 0)
            continue;

        // Remove the routes of the target's outputs
        unmapOp(first_slice, d, output, placed, schedule, II);
        placeOp(first_slice, placed[oid - 1][1] / get_cgra_C(first_slice), placed[oid - 1][1] % get_cgra_C(first_slice), d, output, placed, schedule, II);
    }
}

/************************************************************************************
 * Clears the current mapping
 */
void clearMapping(cgra *fs, dfg *d, dfg_instr **dfg_ops, int **placed, int *schedule, int II)
{

    int i, N = get_node_sublist_size(dfg_ops);

    for (i = 0; i < N; i++)
    {
        if (placed[get_instr_id(dfg_ops[i]) - 1][0] == 0)
            continue;
        unmapOp(fs, d, dfg_ops[i], placed, schedule, II);
    }
}
