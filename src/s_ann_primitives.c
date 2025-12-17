#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>
#include "ops.h"
#include "dfg.h"
#include "cgra.h"
#include "Item.h"
#include "pqueue.h"
#include "stack.h"
#include "files.h"
#include <time.h>

#define MAX(a, b) a > b ? a : b

typedef struct _temp
{
    float temp;
    float alpha;
} temperature;

/********************************************************
 * Temperature / Cooling Function Primitives
 *******************************************************/

temperature *initTemperature(int initialTemp)
{
    temperature *t = (temperature *)malloc(sizeof(temperature));
    t->temp = initialTemp;
    t->alpha = 1;
    return t;
}

float getTemperature(temperature *t)
{
    return t->temp;
}

void deleteTemperature(temperature *t)
{
    free(t);
}

/*******************************************************
 * Cooling Schedule inspired by VPR
 ******************************************************/
void updateTemperature(temperature *t, int nAccepted, int nTotal)
{

    float rAccepted = (float)nAccepted / nTotal;

    if (rAccepted > 0.96)
        t->alpha = 0.5;
    else if (rAccepted > 0.8 && rAccepted <= 0.96)
        t->alpha = 0.9;
    else if (rAccepted > 0.15 && rAccepted <= 0.8)
        t->alpha = 0.95;
    else
        t->alpha = 0.8;

    t->temp *= t->alpha;
}

/*******************************************************
 * Stop criteria
 * Return values: stop criteria met ? 1 : 0
 ******************************************************/
int checkTemperatureStopCriteria(temperature *t, float *cost, int N)
{

    float total_cost = array_sum(cost, N);

    if (t->temp < 0.005 * total_cost / N)
        return 1;
    return 0;
}

/*******************************************************
 * Cost Function
 * c = a * delay + b * penalty?1:0
 * if failed to place: c = gamma (>>)
 ******************************************************/
#define ALPHA 1
#define BETA 10
#define GAMMA 50
float computeCost(cgra *fs, dfg_instr *target, int **placed, int *schedule, int II, int penalty)
{

    int k, iid, ii, ij, id = get_instr_id(target);
    int i_pos, j_pos;
    int delay = 0;

    if (placed[id - 1][0] == 0)
        return GAMMA;

    i_pos = placed[id - 1][1] / get_cgra_C(fs);
    j_pos = placed[id - 1][1] % get_cgra_C(fs);

    for (k = 0; k < get_n_inputs(target); k++)
    {
        iid = get_instr_id(get_input(target, k));

        ii = placed[iid - 1][1] / get_cgra_C(fs);
        ij = placed[iid - 1][1] % get_cgra_C(fs);

        delay = MAX(delay, abs(ii - i_pos) + abs(ij - j_pos));
    }

    return (float)(ALPHA * delay) + (float)(BETA * penalty);
}

/******************************************************
 * updateCost
 * Warning: This function deletes the newCost array!
 * Do not use the aforementioned array after calling
 * this function!
 *****************************************************/
void updateCost(float *cost, float *newCost, int N)
{

    int i;
    for (i = 0; i < N; i++)
        cost[i] = newCost[i];
    free(newCost);
}

/***************************************************************************************************************
 * evaluateMoveCost
 * Inputs: current temperature, t, current cost, the new cost caused by the move and the size of the cost arrays
 * Determines whether the move should be accepted or not, based on the cost difference and on the current
 * temperature.
 * Return values: accepted ? 1 : 0
 **************************************************************************************************************/
int evaluateMoveCost(temperature *t, float *cost, float *newCost, int arr_size)
{

    float totalCost = array_sum(cost, arr_size), newTotalCost = array_sum(newCost, arr_size), delta, P, r;
    delta = newTotalCost - totalCost;
    P = exp(-delta / t->temp);
    r = (float)rand() / RAND_MAX;

    // If the move results in a smaller cost or the temperature allows it, accept the move
    if (delta < 0 || r < P)
        return 1;
    return 0;
}

int **getFreePositions(cgra *fs, dfg_instr *target, int **placed, int *schedule, int II);
void deleteFreePosArr(int **fpos, cgra *fs);

float computeMoveCostStdDev(cgra *fs, dfg *d, dfg_instr **dfg_ops, int ***placed, int *schedule, int II, float *cost, int N)
{

    int i, currentPlacement;
    float *costArr = (float *)malloc((N + 1) * sizeof(float)), *moveCost, stddev;
    int *routed = (int *)calloc(N, sizeof(int));
    int **fpos;
    cgra *aux;

    costArr[N] = array_sum(cost, N);
    // Do N uncommited moves and compute the total cost's std deviation
    for (i = 0; i < N; i++)
    {
        fpos = getFreePositions(fs, dfg_ops[i], *placed, schedule, II);
        if (fpos == NULL || fpos[0][0] == 0)
            continue;
        // God Bless
        aux = copy_all_cgra_slices(fs);
        currentPlacement = (*placed)[get_instr_id(dfg_ops[i]) - 1][1];
        moveCost = m1(aux, d, dfg_ops, dfg_ops[i], fpos[1][0], fpos[1][1], *placed, schedule, II, cost, routed);
        costArr[i] = array_sum(moveCost, N);
        delete_cgra(aux);
        deleteFreePosArr(fpos, fs);
        // Restore old node position
        (*placed)[get_instr_id(dfg_ops[i]) - 1][1] = currentPlacement;
        free(moveCost);
    }
    stddev = array_std_dev(costArr, N + 1);
    free(routed);
    free(costArr);

    return stddev;
}

int **generatePlacementMatrixNoBudget(cgra *fs, dfg_instr *target, int **placed, int *schedule, int II)
{

    int i, j, k, id = get_instr_id(target), iid, ii, jj, dist;
    cgra *c = getModuloSlice(fs, schedule[id - 1], II);
    int **placementMatrix = (int **)malloc(get_cgra_L(c) * sizeof(int *));

    for (i = 0; i < get_cgra_L(c); i++)
    {
        placementMatrix[i] = (int *)malloc(get_cgra_C(c) * sizeof(int));
        for (j = 0; j < get_cgra_C(c); j++)
        {
            placementMatrix[i][j] = checkStructHazard(c, target, i, j);
            placementMatrix[i][j] *= !pe_occupied(c, i, j);
        }
    }

    for (i = 0; i < get_cgra_L(c); i++)
    {
        for (j = 0; j < get_cgra_C(c); j++)
        {
            if (placementMatrix[i][j] == 0)
            {
                placementMatrix[i][j] = -1;
                continue;
            }

            for (k = 0; k < get_n_inputs(target); k++)
            {
                iid = get_instr_id(get_input(target, k));
                if (placed[iid - 1][0] == 0)
                    continue;
                ii = placed[iid - 1][1] / get_cgra_C(c);
                jj = placed[iid - 1][1] % get_cgra_C(c);

                dist = abs(ii - i) + abs(jj - j);
                if (placementMatrix[i][j] > -1)
                    placementMatrix[i][j] = MAX(dist, placementMatrix[i][j]);
                else
                    placementMatrix[i][j] = -1;
            }
            for (k = 0; k < get_n_recurrences(target); k++)
            {
                iid = get_instr_id(get_recurrence(target, k));
                if (placed[iid - 1][0] == 0)
                    continue;
                ii = placed[iid - 1][1] / get_cgra_C(c);
                jj = placed[iid - 1][1] % get_cgra_C(c);
                dist = abs(ii - i) + abs(jj - j);
                if (placementMatrix[i][j] > -1)
                    placementMatrix[i][j] = MAX(dist, placementMatrix[i][j]);
                else
                    placementMatrix[i][j] = -1;
            }
        }
    }
    return placementMatrix;
}

float *generateInitialPlacement(cgra *fs, dfg *d, dfg_instr **dfg_ops, int **placed, int *schedule, int II, int *routed)
{

    int i, ic, jc, N = get_node_sublist_size(dfg_ops), sz = get_cgra_C(fs) * get_cgra_L(fs), minDist, **placementMatrix;
    int **candidate_positions = (int **)calloc(sz, sizeof(int *)), n_pos, rand_pos, penalty, status;
    float *cost = (float *)calloc(N, sizeof(float));

    for (i = 0; i < sz; i++)
        candidate_positions[i] = (int *)calloc(2, sizeof(int));

    dfg_instr *target;

    // Place each node in a valid, placeable position, without routing. Compute the estimated cost associated for each node
    for (i = 0; i < N; i++)
    {
        target = dfg_ops[i];
        placementMatrix = generatePlacementMatrix(fs, target, placed, schedule, II, &minDist);
        penalty = 0; // no penalty to give
        n_pos = 0;
        for (ic = 0; ic < get_cgra_L(fs); ic++)
        {
            for (jc = 0; jc < get_cgra_C(fs); jc++)
            {
                if (placementMatrix[ic][jc] >= 0)
                {
                    candidate_positions[n_pos][0] = ic;
                    candidate_positions[n_pos++][1] = jc;
                }
            }
        }
        deletePlacementMatrix(placementMatrix, fs);
        placementMatrix = NULL;

        // No valid position found: choose a position ignoring the time budget and incurr a penalty associated to the added delay
        if (n_pos == 0)
        {
            placementMatrix = generatePlacementMatrixNoBudget(fs, target, placed, schedule, II);
            penalty = 1; // give a penalty later, associated to how far away from the inputs and recurrences the chosen tile is
            for (ic = 0; ic < get_cgra_L(fs); ic++)
            {
                for (jc = 0; jc < get_cgra_C(fs); jc++)
                {
                    if (placementMatrix[ic][jc] >= 0)
                    {
                        candidate_positions[n_pos][0] = ic;
                        candidate_positions[n_pos++][1] = jc;
                    }
                }
            }
        }

        rand_pos = rand() % n_pos;
        if (penalty == 1)
        {
            /* printf("placement matrix @ (%d, %d) is %d. Penalty!\n", candidate_positions[rand_pos][0], candidate_positions[rand_pos][1],
                   placementMatrix[candidate_positions[rand_pos][0]][candidate_positions[rand_pos][1]]); */
            // give a penalty to this placement
        }
        status = placeOp(fs, candidate_positions[rand_pos][0], candidate_positions[rand_pos][1], d, target, placed, schedule, II);
        if (!status)
        {
            /* printf("Failed to place the node!\n"); */
            // give a penalty to this node due to placement failure (although it should not happen)
            penalty = 1;
        }
        // Try routing the operation
        if (!penalty)
        {
            status = routeOp(fs, target, placed, schedule, II);
            if (!status)
            {
                /* printf("Failed to route the node!\n"); */
                penalty = 1;
            }
        }

        cost[get_instr_id(target) - 1] = computeCost(fs, target, placed, schedule, II, penalty);
        routed[get_instr_id(dfg_ops[i]) - 1] = !penalty;
        if (placementMatrix)
            deletePlacementMatrix(placementMatrix, fs);
    }

    for (i = 0; i < sz; i++)
        free(candidate_positions[i]);
    free(candidate_positions);

    routed[N] = array_sum_int(routed, N);

    return cost;
}

int **getFreePositions(cgra *fs, dfg_instr *target, int **placed, int *schedule, int II)
{

    int i, j, r, sz = get_cgra_L(fs) * get_cgra_C(fs);
    int num_positions = 0, **free_positions = (int **)calloc(sz + 1, sizeof(int *));
    int **pos_list = (int **)calloc(sz, sizeof(int *));
    cgra *curr = getModuloSlice(fs, schedule[get_instr_id(target) - 1], II);
    for (i = 0; i < sz + 1; i++)
    {
        if (i < sz)
            pos_list[i] = (int *)calloc(2, sizeof(int));
        free_positions[i] = (int *)calloc(2, sizeof(int));
    }

    // Search for free PEs in the target slice
    for (i = 0; i < get_cgra_L(fs); i++)
    {
        for (j = 0; j < get_cgra_C(fs); j++)
        {
            if (pe_occupied(curr, i, j) || checkStructHazard(curr, target, i, j) == 0)
                continue;

            /* free_positions[f_pos][0] = i;
            free_positions[f_pos++][1] = j; */
            pos_list[num_positions][0] = i;
            pos_list[num_positions++][1] = j;
        }
    }

    // No free PEs to map to
    if (num_positions <= 0)
    {
        for (i = 0; i < sz + 1; i++)
            free(free_positions[i]);
        free(free_positions);
        for (i = 0; i < sz; i++)
            free(pos_list[i]);
        free(pos_list);
        return NULL;
    }

    free_positions[0][0] = num_positions;
    // Sort the free positions randomly
    for (i = 0; i < num_positions; i++)
    {
        r = rand() % (num_positions - i);
        free_positions[i + 1][0] = pos_list[r][0];
        free_positions[i + 1][1] = pos_list[r][1];
        // remove chosen position
        pos_list[r][0] = pos_list[num_positions - 1 - i][0];
        pos_list[r][1] = pos_list[num_positions - 1 - i][1];
    }

    for (i = 0; i < sz; i++)
        free(pos_list[i]);
    free(pos_list);

    return free_positions;
}

/**
 * Generates an array with all legal positions for the target to be placed on.
 * Effectively corresponds to all free placeable positions (getFreePositions) + the valid positions that are occupied
 * by other ops
 */
int **getPlaceablePositions(cgra *fs, dfg_instr *target, int **placed, int *schedule, int II)
{

    int i, j, r, sz = get_cgra_L(fs) * get_cgra_C(fs);
    int num_positions = 0, **free_positions = (int **)calloc(sz + 1, sizeof(int *));
    int **pos_list = (int **)calloc(sz, sizeof(int *));
    cgra *curr = getModuloSlice(fs, schedule[get_instr_id(target) - 1], II);
    for (i = 0; i < sz + 1; i++)
    {
        if (i < sz)
            pos_list[i] = (int *)calloc(2, sizeof(int));
        free_positions[i] = (int *)calloc(2, sizeof(int));
    }

    // Search for free PEs in the target slice
    for (i = 0; i < get_cgra_L(fs); i++)
    {
        for (j = 0; j < get_cgra_C(fs); j++)
        {
            if (checkStructHazard(curr, target, i, j) == 0)
                continue;

            /* free_positions[f_pos][0] = i;
            free_positions[f_pos++][1] = j; */
            pos_list[num_positions][0] = i;
            pos_list[num_positions++][1] = j;
        }
    }

    // No free PEs to map to
    if (num_positions <= 0)
    {
        for (i = 0; i < sz + 1; i++)
            free(free_positions[i]);
        free(free_positions);
        for (i = 0; i < sz; i++)
            free(pos_list[i]);
        free(pos_list);
        return NULL;
    }

    free_positions[0][0] = num_positions;
    // Sort the free positions randomly
    for (i = 0; i < num_positions; i++)
    {
        r = rand() % (num_positions - i);
        free_positions[i + 1][0] = pos_list[r][0];
        free_positions[i + 1][1] = pos_list[r][1];
        // remove chosen position
        pos_list[r][0] = pos_list[num_positions - 1 - i][0];
        pos_list[r][1] = pos_list[num_positions - 1 - i][1];
    }

    for (i = 0; i < sz; i++)
        free(pos_list[i]);
    free(pos_list);

    return free_positions;
}

void deleteFreePosArr(int **fpos, cgra *fs)
{
    for (int i = 0; i < get_cgra_L(fs) * get_cgra_C(fs) + 1; i++)
        free(fpos[i]);
    free(fpos);
}

void checkValidMapping(int *routed, int N)
{
    routed[N] = 0;
    routed[N] = array_sum_int(routed, N);
}

void ripUpOp(cgra *fs, dfg *d, dfg_instr *target, int **placed, int *schedule, int II)
{

    // Unmap the target and remove its outputs' routes
    unmapOp(fs, d, target, placed, schedule, II);
    unRouteOutputs(fs, d, target, placed, schedule, II);
}

/**************************************************
 * M1 move: try placing at a random free position
 * Warning: this function changes the device, even
 * if the move should not be accepted. It is highly
 * recommended to store a copy of the device before
 * calling this function.
 *************************************************/
float *m1(cgra *fs, dfg *d, dfg_instr **dfg_ops, dfg_instr *target, int i_pos, int j_pos, int **placed, int *schedule, int II, float *cost, int *routed)
{

    int i, o, id = get_instr_id(target), oid, p, N = get_node_sublist_size(dfg_ops), n_outputs = get_n_outputs(target), status, penalty = 0;
    dfg_instr *output;
    float *newCost = (float *)malloc(N * sizeof(float));

    int auxSch = schedule[id - 1];

    for (i = 0; i < N; i++)
        newCost[i] = cost[i];

    // Unmap the target and remove its outputs' routes
    schedule[id - 1] = placed[id - 1][2];
    ripUpOp(fs, d, target, placed, schedule, II);
    schedule[id - 1] = auxSch;

    p = 0;
    status = placeOp(fs, i_pos, j_pos, d, target, placed, schedule, II);
    if (!status)
    {
        /* printf("Failed to place the node in the new M1 position!\n"); */
        penalty = 1;
        p = 1;
    }

    if (!penalty)
    {
        // placement should be valid and routable
        status = routeOp(fs, target, placed, schedule, II);
        if (!status)
        {
            /* printf("Failed to route the node! Penalty!\n"); */
            penalty = 1;
        }
    }

    // update the cost for the target
    newCost[get_instr_id(target) - 1] = computeCost(fs, target, placed, schedule, II, penalty);
    routed[get_instr_id(target) - 1] = !penalty;

    // Only if the node was successfully placed
    if (p == 0)
    {
        // try rerouting the target's mapped outputs and update their costs. Of course, for each of them, if routing is impossible
        // a penalty will be incurred
        status = 1;
        for (o = 0; o < n_outputs; o++)
        {
            output = get_output(target, o);
            oid = get_instr_id(output);

            penalty = 0;

            // Output has not yet been placed (this will include the target)
            if (placed[oid - 1][0] == 0)
                continue;

            status = routeOp(fs, output, placed, schedule, II);
            if (!status)
            {
                penalty = 1;
            }
            newCost[oid - 1] = computeCost(fs, output, placed, schedule, II, penalty);
            routed[oid - 1] = !penalty;
        }
    }
    // Should be nearly impossible to accept this move
    else
    {
        newCost[get_instr_id(target) - 1] = INFINITY - array_sum(newCost, N) - 1;
    }

    return newCost;
}

/**************************************************
 * M2 move: try swapping the op with another one
 * Warning: this function changes the device, even
 * if the move should not be accepted. It is highly
 * recommended to store a copy of the device before
 * calling this function.
 *************************************************/
float *m2(cgra *fs, dfg *d, dfg_instr **dfg_ops, dfg_instr *target1, int i_pos1, int j_pos1,
          dfg_instr *target2, int i_pos2, int j_pos2, int **placed, int *schedule, int II, float *cost, int *routed)
{
    int id1 = get_instr_id(target1), id2 = get_instr_id(target2);
    int s1 = schedule[id1 - 1], s2 = schedule[id2 - 1];
    float *newCost1, *newCost2;
    // Rip Up both operations and swap them
    schedule[id1 - 1] = placed[id1 - 1][2];
    schedule[id2 - 1] = placed[id2 - 1][2];
    ripUpOp(fs, d, target1, placed, schedule, II);
    ripUpOp(fs, d, target2, placed, schedule, II);
    schedule[id1 - 1] = s1;
    schedule[id2 - 1] = s2;

    // First place target 1
    newCost1 = m1(fs, d, dfg_ops, target1, i_pos1, j_pos1, placed, schedule, II, cost, routed);
    // Next, place target 2
    newCost2 = m1(fs, d, dfg_ops, target2, i_pos2, j_pos2, placed, schedule, II, newCost1, routed);

    free(newCost1);
    return newCost2;
}

float *anneal_swap(cgra *fs, dfg *d, dfg_instr **dfg_ops, dfg_instr *target, int i_pos, int j_pos,
                   int **placed, int *schedule, int II, float *cost, int *routed, int* swapped)
{
    int N = get_node_sublist_size(dfg_ops), i_pos2, j_pos2, id = get_instr_id(target);
    cgra *curr = getModuloSlice(fs, schedule[get_instr_id(target) - 1], II);
    dfg_instr *target2;
    float *newCost = NULL;

    // NULL or unplaceable tile due to structural hazard
    if (checkStructHazard(curr, target, i_pos, j_pos) == 0)
    {
        // Return a total cost of infinity
        newCost = (float *)calloc(N, sizeof(float));
        newCost[0] = INFINITY;
        return newCost;
    }

    // If the selected tile is already in use by another op, swap both tiles (M2)
    if (pe_occupied(curr, i_pos, j_pos))
    {

        target2 = get_cgra_tile(curr, i_pos, j_pos);
        (*swapped) = get_instr_id(target2);
        // the placed op will be moved to the target's current position
        if (placed[id - 1][0] == 1)
        {
            i_pos2 = placed[id - 1][1] / get_cgra_C(fs);
            j_pos2 = placed[id - 1][1] % get_cgra_C(fs);
        }
        // For some reason, the target is not placed anywhere! Then,
        else
        {
            // Extract a random placeable position for the 2nd target
            int **fpos = getFreePositions(fs, target2, placed, schedule, II);
            i_pos2 = fpos[1][0];
            j_pos2 = fpos[1][1];
            deleteFreePosArr(fpos, fs);
        }
        newCost = m2(fs, d, dfg_ops, target, i_pos, j_pos, target2, i_pos2, j_pos2, placed, schedule, II, cost, routed);
        return newCost;
    }

    // Tile is free, move the op there (M1)
    newCost = m1(fs, d, dfg_ops, target, i_pos, j_pos, placed, schedule, II, cost, routed);
    return newCost;
}