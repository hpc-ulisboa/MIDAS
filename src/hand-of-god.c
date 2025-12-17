#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <time.h>
#include "ops.h"
#include "dfg.h"
#include "cgra.h"
#include "pqueue.h"
#include "stack.h"
#include "files.h"
#include <omp.h>
#include <time.h>

#define ABS(a) a >= 0 ? a : -a
#define MAX(a, b) a > b ? a : b
#define MIN(a, b) a < b ? a : b

#define STATUS_OK 0
#define ERR_NO_PLACE 1
#define ERR_NO_ROUTE 2
#define ERR_TIME_BUDGET 3

#define CST_SIZE 2
#define CST_ERRCODE 0
#define CST_DIST 1

#define CST_N_ROUTED 0

#define MAPPER_FINETUNING 1
#define MAPPER_ITERATIVE 2
#define MAPPER_SIM_ANNEALING 3

/***************************************************************************************************
 * parallelize_mapping
 * Inputs: mapped device, target DFG and the placement info array
 * Parallelizes the DFG by mapping it additional times. Uses only the remaining vacant slots to map.
 * Attempts to map as many times as possible, stopping only once it fails.
 * Return values: mapped device, with the extra DFG mappings.
 ***************************************************************************************************/
cgra *parallelize_mapping(cgra *c, dfg *d, int ***placed, int verbose)
{
    // Create a new placed array, to be used as a new copy for every iteration
    int i, j, parallelizations = 0, parallelization_result, ***new_placed = (int ***)calloc(1, sizeof(int **));
    *new_placed = (int **)calloc(get_dfg_size(d), sizeof(int *));
    int mapper = get_mapping(c);

    for (i = 0; i < get_dfg_size(d); i++)
        (*new_placed)[i] = (int *)calloc(5, sizeof(int)); // [placed?, line, column, first_slice, last_slice]

    do
    {
        parallelization_result = 0;
        set_power_for_pe_set(c, POWER_OFF, IN_USE);
        c = HandOfGod(c, d, new_placed, &parallelization_result, mapper, INFINITY, 0);

        // Reset this new placed array
        for (i = 0; i < get_dfg_size(d); i++)
            for (j = 0; j < 5; j++)
                (*new_placed)[i][j] = 0;

        if (parallelization_result == 1)
            parallelizations++;
    } while (parallelization_result == 1);
    if (verbose)
        printf("Number of times DFG was parallelized: %d\n", parallelizations);
    set_power_for_pe_set(c, POWER_ON, IN_USE);
    for (j = 0; j < get_dfg_size(d); j++)
    {
        free(new_placed[0][j]);
    }

    free(new_placed[0]);
    free(new_placed);

    return c;
}

/***************************************************************************************************
 * generatePlacementMatrix
 * Inputs: device model, target DFG, placement info array, schedule and II
 * Generates a matrix with possible placements, according to the distances to the target's inputs
 * Tiles in use are marked with 0. Other tiles are marked with the maximum distance to an input.
 * The distance is calculated as the sum of unitary distances between neighbouring PEs.
 * Return values: placement matrix
 ***************************************************************************************************/
int **generatePlacementMatrix(cgra *fs, dfg_instr *target, int **placed, int *schedule, int II, int *minDist)
{

    int i, j, k, id = get_instr_id(target), iid, ii, jj, dist, t = schedule[id - 1], time_budget;
    (*minDist) = INFINITY;
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
                time_budget = t - (schedule[iid - 1] + get_instr_lat(get_input(target, k)) - 1);
                dist = abs(ii - i) + abs(jj - j);

                if (getconnLat(c, i, j, ii, jj) > 0 && getconnLat(c, i, j, ii, jj) < INFINITY)
                    dist = dist < getconnLat(c, i, j, ii, jj) ? dist : getconnLat(c, i, j, ii, jj);

                if (dist <= time_budget && placementMatrix[i][j] > -1)
                    placementMatrix[i][j] = MAX(dist, placementMatrix[i][j]);
                else
                {
                    if (placementMatrix[i][j] > -1)
                        (*minDist) = MIN((*minDist), dist - time_budget); // minimum distance, in time, that you could add to get a new position
                    placementMatrix[i][j] = -1;
                }
                // placementMatrix[i][j] *= (dist <= time_budget); // if dist > time budget then it will be impossible to route from this pos
                // placementMatrix[i][j] = MAX(dist, placementMatrix[i][j]);
            }
            for (k = 0; k < get_n_recurrences(target); k++)
            {
                iid = get_instr_id(get_recurrence(target, k));
                if (placed[iid - 1][0] == 0)
                    continue;
                ii = placed[iid - 1][1] / get_cgra_C(c);
                jj = placed[iid - 1][1] % get_cgra_C(c);
                time_budget = (schedule[iid - 1] + get_instr_lat(get_recurrence(target, k)) - 1 + II * get_rec_dist(target, k)) - t;
                // printf("time budget for recurrence to node %d = %d (%d, %d, %d)\n",iid, time_budget, (schedule[iid - 1] + get_instr_lat(get_recurrence(target, k)) - 1, II, get_rec_dist(target, k))); exit(0);
                dist = abs(ii - i) + abs(jj - j);
                if (dist <= time_budget && placementMatrix[i][j] > -1)
                    placementMatrix[i][j] = MAX(dist, placementMatrix[i][j]);
                else
                {
                    if (placementMatrix[i][j] > -1)
                        (*minDist) = MIN((*minDist), dist - time_budget); // minimum distance, in time, that you could add to get a new position
                    placementMatrix[i][j] = -1;
                }
            }
        }
    }

    /*
    printf("Placement Matrix for node %d:\n", id);
    for (i = 0; i < get_cgra_L(c); i++)
    {
        for (j = 0; j < get_cgra_C(c); j++)
        {
            printf("%d ", placementMatrix[i][j]);
        }
        printf("\n");
    }
    printf("\n");
    } */
    /* printf("Mindist is %d\n",(*minDist)); */
    return placementMatrix;
}

void deletePlacementMatrix(int **mat, cgra *c)
{

    int i;
    if (mat == NULL)
        return;
    for (i = 0; i < get_cgra_L(c); i++)
    {
        if (mat[i] != NULL)
            free(mat[i]);
    }
    free(mat);
}

int getReschedulingTimeDistance(cgra *fs, dfg_instr *target, int **placed, int *schedule, int II, int cst_dist)
{

    int reschTimeDistance, minDist;
    /* printf("ok. mindist is %d.\n", cst_dist); */
    cgra *target_slice = getModuloSlice(fs, schedule[get_instr_id(target) - 1], II);
    int i, **placementsNextSlice, currSched = schedule[get_instr_id(target) - 1];
    for (i = 0; i < cst_dist; i++)
    {
        target_slice = getNextModuloSlice(target_slice);
        schedule[get_instr_id(target) - 1] += 1;
        placementsNextSlice = generatePlacementMatrix(fs, target, placed, schedule, II, &(minDist));
        deletePlacementMatrix(placementsNextSlice, fs);
        if (minDist > 0)
            break;
    }
    schedule[get_instr_id(target) - 1] = currSched;
    if (i >= cst_dist)
        reschTimeDistance = 0;
    else
    {
        reschTimeDistance = i + 1;
    }
    /* printf("rescheduling time-distance: %d\n", reschTimeDistance); */
    return reschTimeDistance;
}

/********************************
 * attemptPRNode
 * Attempts to perform the placement and routing of a node
 */
int attemptPRNode(cgra *fs, dfg *d, dfg_instr *target, int **placed, int *schedule, int II, int *cst)
{
    /*********************
     * Attempt to place and route the target node
     * First, generate the placement matrix
     * then, while unable to place and route the node successfully, choose a random valid and untaken position from the matrix
     * and try placing and routing the node. if it succeeds, hooray, return 1
     * if it fails, keep trying the remaining valid untaken positions. if no valid position from the matrix results in a valid mapping of
     * the node, then it is impossible to place and route it as is. try EITHER replacing one of its inputs or rescheduling the node.
     * but for now, on a simpler side, we can just say "oh yea, it impossible chief, my bad :(" just to verify that everything so far works
     */

    int minDist, **placementMatrix = generatePlacementMatrix(fs, target, placed, schedule, II, &minDist);
    int i, j, mapped = 0, num_positions = 0, sz = get_cgra_L(fs) * get_cgra_C(fs), priority, candidate_i, candidate_j, status;

    /***********************************************************************************************
     * 0: All is good;
     * 1: No placement candidates existed (must replace and reroute at least one of the inputs);
     * 2: Placement candidates existed, but no routes existed due to the interconnect architecture
     * (must reroute at least one of the inputs)
     * 3: No placement candidates were generated due to the time budget (maybe reschedule the node?)
     **********************************************************************************************/
    int errcode = 0;

    MinHeap *pq = createMinHeap(sz);
    // Initialize all nodes with INF distance
    for (int i = 0; i < sz; i++)
    {
        pq->array[i] = newMinHeapNode(i, INFINITY);
        pq->pos[i] = i;
    }
    pq->size = sz;

    for (i = 0; i < get_cgra_L(fs); i++)
    {
        for (j = 0; j < get_cgra_C(fs); j++)
        {
            if (placementMatrix[i][j] >= 0)
            {
                num_positions++;
                priority = rand() % sz;
                // Push to priority queue
                decreaseKey(pq, i * get_cgra_C(fs) + j, priority);
            }
        }
    }

    if (num_positions == 0)
    {
        // printf("ERROR[%d]: No positions to map to. MinDist is %d\n", get_instr_id(target), minDist);
        if (minDist == INFINITY)
            errcode = ERR_NO_PLACE;
        else
        {
            if (cst != NULL)
                cst[CST_DIST] = minDist;
            errcode = ERR_TIME_BUDGET;
        }
        /* switch ((errcode))
        {
        case 1:
            printf("No valid placements exist! Must reroute at least one of the target's inputs!\n");
            break;
        case 3:
            printf("It may be possible to P&R the target, but it will require rescheduling it by at least %d cycles!\n", minDist - time_budget);
            break;
        default:
            break;
        } */
        freeMinHeap(pq);
        deletePlacementMatrix(placementMatrix, fs);
        return errcode;
    }

    while (!mapped)
    {
        MinHeapNode *best = extractMin(pq);
        if (best && num_positions > 0)
        {
            num_positions--;
            candidate_i = best->vertex / get_cgra_C(fs);
            candidate_j = best->vertex % get_cgra_C(fs);
            freeMinHeapNode(best);

            // Place the Operation
            status = placeOp(fs, candidate_i, candidate_j, d, target, placed, schedule, II);
            if (!status)
            {
                /* printf("Failed to place node. (?)\n"); */
                continue;
            }
            // Successful Placement
            else
            {
                /************************************
                 * Try routing the node to its inputs
                 ************************************/
                status = routeOp(fs, target, placed, schedule, II);

                if (!status)
                {
                    /* printf("Failed to route the node to its inputs.\n"); */
                    unmapOp(fs, d, target, placed, schedule, II);
                    continue;
                }
                // Successful Routing
                else
                {
                    mapped = 1;
                }
            }
        }
        // No more placements to try. It is impossible to map this node right now.
        else
        {
            /* printf("Failed to map the node %d.\n", get_instr_id(target)); */
            freeMinHeapNode(best);
            freeMinHeap(pq);
            deletePlacementMatrix(placementMatrix, fs);
            errcode = ERR_NO_ROUTE;
            return errcode;
        }
    }
    freeMinHeap(pq);
    deletePlacementMatrix(placementMatrix, fs);
    return STATUS_OK;
}

int attemptPRHandOfGod(cgra *fs, dfg *d, dfg_instr *target, int **placed, int *schedule, int II, int ***pms, int *minDist, int *cst)
{
    int **placementMatrix;
    int i, j, mapped = 0, num_positions = 0, sz = get_cgra_L(fs) * get_cgra_C(fs), priority, candidate_i, candidate_j, status;

    // First time calling this node
    if (pms[get_instr_id(target) - 1] == NULL)
    {
        placementMatrix = generatePlacementMatrix(fs, target, placed, schedule, II, &(minDist[get_instr_id(target) - 1]));
        pms[get_instr_id(target) - 1] = placementMatrix;
    }
    else
        placementMatrix = pms[get_instr_id(target) - 1];
    /* printf("time budget is %d\n",time_budget); */

    /***********************************************************************************************
     * 0: All is good;
     * 1: No placement candidates existed (must replace and reroute at least one of the inputs);
     * 2: Placement candidates existed, but no routes existed due to the interconnect architecture
     * (must reroute at least one of the inputs)
     * 3: No placement candidates were generated due to the time budget (maybe reschedule the node?)
     **********************************************************************************************/
    int errcode = 0;

    MinHeap *pq = createMinHeap(sz);
    // Initialize all nodes with INF distance
    for (int i = 0; i < sz; i++)
    {
        pq->array[i] = newMinHeapNode(i, INFINITY);
        pq->pos[i] = i;
    }
    pq->size = sz;

    for (i = 0; i < get_cgra_L(fs); i++)
    {
        for (j = 0; j < get_cgra_C(fs); j++)
        {
            if (placementMatrix[i][j] >= 0)
            {
                num_positions++;
                priority = rand() % sz;
                // Push to priority queue
                decreaseKey(pq, i * get_cgra_C(fs) + j, priority);
            }
        }
    }

    if (num_positions == 0)
    {
        if (minDist[get_instr_id(target) - 1] == INFINITY)
            errcode = ERR_NO_PLACE;
        else
            errcode = ERR_TIME_BUDGET;
        if (cst != NULL)
        {
            cst[CST_DIST] = minDist[get_instr_id(target) - 1];
        }
        // printf("ERROR[%d]: No positions to map to. MinDist is %d\n", get_instr_id(target), minDist);
        freeMinHeap(pq);
        deletePlacementMatrix(placementMatrix, fs);
        pms[get_instr_id(target) - 1] = NULL;
        minDist[get_instr_id(target) - 1] = INFINITY; // if the mapping fails after rescheduling the first time, don't reschedule anymore
        return errcode;
    }

    while (!mapped)
    {
        MinHeapNode *best = extractMin(pq);
        if (best && num_positions > 0)
        {
            num_positions--;
            candidate_i = best->vertex / get_cgra_C(fs);
            candidate_j = best->vertex % get_cgra_C(fs);
            freeMinHeapNode(best);

            // Place the Operation
            status = placeOp(fs, candidate_i, candidate_j, d, target, placed, schedule, II);
            if (!status)
            {
                /* printf("Failed to place node. (?)\n"); */
                continue;
            }
            // Successful Placement
            else
            {
                /************************************
                 * Try routing the node to its inputs
                 ************************************/
                status = routeOp(fs, target, placed, schedule, II);
                placementMatrix[candidate_i][candidate_j] = -2; // in case of backtracking, prevent from retrying the same spot

                if (!status)
                {
                    /* printf("Failed to route the node to its inputs.\n"); */
                    unmapOp(fs, d, target, placed, schedule, II);
                    continue;
                }
                // Successful Routing
                else
                {
                    mapped = 1;
                    minDist[get_instr_id(target) - 1] = INFINITY; // no more rescheduling for this node
                }
            }
        }
        // No more placements to try. It is impossible to map this node right now.
        else
        {
            /* printf("Failed to map the node %d.\n", get_instr_id(target)); */
            freeMinHeapNode(best);
            freeMinHeap(pq);
            deletePlacementMatrix(placementMatrix, fs);
            pms[get_instr_id(target) - 1] = NULL;
            errcode = ERR_NO_ROUTE;
            return errcode;
        }
    }
    freeMinHeap(pq);
    return STATUS_OK;
}

/**
 * Returns an array with all mappable positions for the target node, sorted in a random order
 */
int **getMappablePositions(cgra *fs, dfg *d, dfg_instr *target, int **placed, int *schedule, int II)
{
    int minDist, **placementMatrix = generatePlacementMatrix(fs, target, placed, schedule, II, &minDist);
    int i, j, num_positions = 0, sz = get_cgra_L(fs) * get_cgra_C(fs), priority, candidate_i, candidate_j, status;

    int m_pos = 1, **mappable_positions = (int **)calloc(sz + 1, sizeof(int *));
    for (i = 0; i < sz + 1; i++)
    {
        mappable_positions[i] = (int *)calloc(2, sizeof(int));
    }

    MinHeap *pq = createMinHeap(sz);
    // Initialize all nodes with INF distance
    for (int i = 0; i < sz; i++)
    {
        pq->array[i] = newMinHeapNode(i, INFINITY);
        pq->pos[i] = i;
    }
    pq->size = sz;

    for (i = 0; i < get_cgra_L(fs); i++)
    {
        for (j = 0; j < get_cgra_C(fs); j++)
        {
            if (placementMatrix[i][j] >= 0)
            {
                num_positions++;
                priority = rand() % sz;
                // Push to priority queue
                decreaseKey(pq, i * get_cgra_C(fs) + j, priority);
            }
        }
    }

    if (num_positions == 0)
    {
        freeMinHeap(pq);
        deletePlacementMatrix(placementMatrix, fs);
        for (i = 0; i < sz + 1; i++)
            free(mappable_positions[i]);
        free(mappable_positions);
        return NULL;
    }

    // from the filtered candidates, find the ones that result in valid node mappings
    while (num_positions > 0)
    {
        MinHeapNode *best = extractMin(pq);
        num_positions--;
        candidate_i = best->vertex / get_cgra_C(fs);
        candidate_j = best->vertex % get_cgra_C(fs);
        freeMinHeapNode(best);

        // Place the Operation
        status = placeOp(fs, candidate_i, candidate_j, d, target, placed, schedule, II);
        if (!status)
            continue;
        // Successful Placement
        else
        {
            /************************************
             * Try routing the node to its inputs
             ************************************/
            status = routeOp(fs, target, placed, schedule, II);
            /************************************
             * Unmap the node afterwards
             ************************************/
            unmapOp(fs, d, target, placed, schedule, II);

            if (status)
            {
                mappable_positions[m_pos][0] = candidate_i;
                mappable_positions[m_pos++][1] = candidate_j;
            }
        }
    }

    // the first element of the array stores the number of positions
    mappable_positions[0][0] = m_pos - 1;

    freeMinHeap(pq);
    deletePlacementMatrix(placementMatrix, fs);
    return mappable_positions;
}

void deleteMappablePosArr(int **mp, cgra *c)
{

    int i, sz = get_cgra_C(c) * get_cgra_L(c);
    for (i = 0; i < sz + 1; i++)
        free(mp[i]);
    free(mp);
}

/************************************************************************************************************************
 * localized_search
 * Inputs: device model, target instruction, placement info array, schedule, II, placement matrices and minimum distances
 * Employs a localized search strategy to try and reach a valid mapping for the target. Adapted from the localized search
 * discussed in the paper 'Pathseeker'. For each of the target's inputs, unmap it and remove its mapped outputs' routes.
 * Then, try mapping the input to other mappable positions, checking if its outputs can be rerouted and if the target can
 * be mapped. If the target can be mapped, update the placement matrices to reflect the new mappable positions.
 * Return values: search successful ? 1 : 0
 ***********************************************************************************************************************/
int localized_search(cgra **fs, dfg *d, dfg_instr *target, int **placed, int *schedule, int II, int ***pms, int *minDist)
{

    int i, o, p, **mp, io_id, n_pos;
    int n_inputs = get_n_inputs(target), n_input_outputs, status;
    dfg_instr *input, *input_output;
    cgra *backup = copy_all_cgra_slices(*fs);

    // Auxiliary array to store the old positions of the inputs
    int *input_pos = (int *)malloc(n_inputs * sizeof(int));
    for (i = 0; i < n_inputs; i++)
    {
        input_pos[i] = placed[get_instr_id(get_input(target, i)) - 1][1];
    }

    // For each input: try searching for other mappable positions that might result in a valid mapping
    for (i = 0; i < n_inputs; i++)
    {
        input = get_input(target, i);
        n_input_outputs = get_n_outputs(input);

        // For now, to avoid problems with pipelined nodes
        if (getPEPipelineStages(*fs, placed[get_instr_id(input) - 1][1] / get_cgra_C(*fs), placed[get_instr_id(input) - 1][1] % get_cgra_C(*fs)))
            continue;

        // Unmap the input and remove its outputs' routes
        unmapOp(*fs, d, input, placed, schedule, II);
        for (o = 0; o < n_input_outputs; o++)
        {
            input_output = get_output(input, o);
            io_id = get_instr_id(input_output);

            // Output has not yet been placed (this will include the target)
            if (placed[io_id - 1][0] == 0)
                continue;

            // Remove the routes of the input's outputs
            unmapOp(*fs, d, input_output, placed, schedule, II);
            placeOp(*fs, placed[io_id - 1][1] / get_cgra_C(*fs), placed[io_id - 1][1] % get_cgra_C(*fs), d, input_output, placed, schedule, II);
        }

        // Get all of the input's mappable positions. On a worst case, it will contain only the position for which it was mapped
        mp = getMappablePositions(*fs, d, input, placed, schedule, II);
        if (mp != NULL)
        {
            n_pos = mp[0][0];

            // For each mappable position, map the input and check if its outputs can all be routed and if the target can be mapped
            for (p = 0; p < n_pos; p++)
            {

                // Map the input
                placeOp(*fs, mp[p + 1][0], mp[p + 1][1], d, input, placed, schedule, II);
                routeOp(*fs, input, placed, schedule, II);

                // Try rerouting the mapped outputs
                status = 1;
                for (o = 0; o < n_input_outputs; o++)
                {
                    input_output = get_output(input, o);
                    io_id = get_instr_id(input_output);

                    // Output has not yet been placed (this will include the target)
                    if (placed[io_id - 1][0] == 0)
                        continue;

                    status = routeOp(*fs, input_output, placed, schedule, II);
                    if (!status)
                        break;
                }

                // Failed to reroute the mapped outputs: remove all routes and try the next mappable position
                if (!status)
                {
                    for (o = 0; o < n_input_outputs; o++)
                    {
                        input_output = get_output(input, o);
                        io_id = get_instr_id(input_output);

                        // Output has not yet been placed (this will include the target)
                        if (placed[io_id - 1][0] == 0)
                            continue;

                        // Remove the routes of the input's outputs
                        unmapOp(*fs, d, input_output, placed, schedule, II);
                        placeOp(*fs, placed[io_id - 1][1] / get_cgra_C(*fs), placed[io_id - 1][1] % get_cgra_C(*fs), d, input_output, placed, schedule, II);
                    }
                    unmapOp(*fs, d, input, placed, schedule, II);
                    continue;
                }
                // All outputs could be rerouted: try mapping the target now
                else
                {
                    status = attemptPRHandOfGod(*fs, d, target, placed, schedule, II, pms, minDist, NULL);
                    // Failed to map the target
                    if (status != STATUS_OK)
                    {
                        for (o = 0; o < n_input_outputs; o++)
                        {
                            input_output = get_output(input, o);
                            io_id = get_instr_id(input_output);

                            // Output has not yet been placed (this will include the target)
                            if (placed[io_id - 1][0] == 0)
                                continue;

                            // Remove the routes of the input's outputs
                            unmapOp(*fs, d, input_output, placed, schedule, II);
                            placeOp(*fs, placed[io_id - 1][1] / get_cgra_C(*fs), placed[io_id - 1][1] % get_cgra_C(*fs), d, input_output, placed, schedule, II);
                        }
                        unmapOp(*fs, d, input, placed, schedule, II);
                        continue;
                    }
                    // Successfully found a new configuration that allows for the mapping of the target!
                    else
                    {
                        /* printf("Hooray!\n"); */
                        pms[get_instr_id(input) - 1][mp[p + 1][0]][mp[p + 1][1]] = -2;
                        deleteMappablePosArr(mp, *fs);
                        free(input_pos);
                        delete_cgra(backup);
                        return 1;
                    }
                }
            }
            deleteMappablePosArr(mp, *fs);
        }

        // No positions to map the input to such that the target can be mapped. Remap the input to its old position
        placeOp(*fs, input_pos[i] / get_cgra_C(*fs), input_pos[i] % get_cgra_C(*fs), d, input, placed, schedule, II);
        placed[get_instr_id(input) - 1][1] = input_pos[i];
        delete_cgra(*fs);
        *fs = copy_all_cgra_slices(backup);
    }
    free(input_pos);
    delete_cgra(backup);
    /* printf("Localized search failed to find a new mappable position for the target.\n"); */
    return 0;
}

/**********************************************************************************************
 * mapper_fineTuning
 * Inputs: device model, target dfg, placement info array, minimum II, first time mapping flag
 * Simple mapper that re-schedules the DFG if the distance  between a target  node's inputs is
 * too long.  Attempts to place and route one node at a time. For each node, try all placeable
 * positions.   If none exist / have valid routings,  backtrack to the previously mapped node.
 * Backtracking operations are limited  (give  up  on  backtracking  after a given  number  of
 * backtracking operations).  If backtracking does not solve the mapping, clear everything and
 * try a different position for the first node. If all positions for the first node fail, clear
 * everything and restart the mapping a new, up to a limited number of tries. If, after all of
 * this, no solution is found, clear everything and increase the II. If II becomes larger than
 * the number of dfg operations, mapping is considered impossible.
 * Return values: mapped device
 **********************************************************************************************/
#define MAX_BACKTRACKS 100
cgra *mapper_fineTuning(cgra *template, dfg *d, int ***placed, int MII, int *first_mapping, int maxII, int verbose)
{
    if (!getRFLimitations(template, d))
        return NULL;
    int *schedule = rasMixedScheduling(template, d), *scheduleCopy = (int *)malloc(get_dfg_size(d) * sizeof(int));
    int *scheduleOriginal = (int *)malloc(get_dfg_size(d) * sizeof(int));
    topologicalSortDFG(d);
    dfg_instr **dfg_ins = get_dfg_inputs(d);
    dfg_instr **dfg_ops = get_dfg_ops(d);
    dfg_instr **dfg_outs = get_dfg_outputs(d);
    dfg_ops = merge_sublists(dfg_ins, dfg_ops);
    dfg_ops = merge_sublists(dfg_ops, dfg_outs);

    int i, II, N = get_node_sublist_size(dfg_ops);
    int num_contexts_for_one_iter, failed_to_map = 0, search;
    int ***placementMatrices = (int ***)calloc(get_node_sublist_size(dfg_ops), sizeof(int **));
    int *minDist = (int *)malloc(N * sizeof(int));
    int *reSchedules = (int *)calloc(N, sizeof(int));

    bool *hasDoneLocalizedSearch = (bool *)calloc(get_node_sublist_size(dfg_ops), sizeof(bool));

    cgra *fs;
    int cst[CST_SIZE] = {0};
    int n_backtracks = 0;

    for (i = 0; i < get_dfg_size(d); i++)
    {
        scheduleCopy[i] = schedule[i];
        scheduleOriginal[i] = schedule[i];
    }

    if ((*first_mapping) == 1)
        fs = buildBaseCGRA(template, MII);
    else
        fs = template;

    II = MII;
    int attempts = 0;
    // Main Mapping Loop
    while (!allInstructionsPlaced(*placed, d))
    {

        // Adjust the scheduling for this iteration (schedule and scheduleCopy) to account for BW limitations!
        adjustModuloScheduling(schedule, scheduleCopy, scheduleOriginal, template, d, dfg_ops, II);

        if (failed_to_map)
        {
            attempts++;
            if (attempts > get_cgra_C(fs) * get_cgra_L(fs))
            {
                attempts = 0;
                if (verbose)
                    printf("Failed to map with II = %d\n", II);
                II++;
                if (II > N + 1 || (*first_mapping) == 0 || II > maxII)
                    break;
            }
            failed_to_map = 0;
            if ((*first_mapping) == 1)
            {
                delete_cgra(fs);
                fs = buildBaseCGRA(template, II);
            }
        }

        // Iterate through all DFG Ops
        for (i = 0; i < N; i++)
        {
            int status = attemptPRHandOfGod(fs, d, dfg_ops[i], *placed, schedule, II, placementMatrices, minDist, cst);

            if (status != STATUS_OK)
            {
                /*******************************************************************
                 * At this point, it is impossible to map the target node without
                 * remapping at least one of the previously mapped nodes or changing
                 * the target node's schedule. Perform a localized search first. If
                 * it fails, check for the possibility of re-scheduling the target
                 * or backtrack to the last mapped nodes.
                 ***************************************************************/
                search = 0;
                if (hasDoneLocalizedSearch[i] == false)
                    search = localized_search(&fs, d, dfg_ops[i], *placed, schedule, II, placementMatrices, minDist);

                // Search success
                if (search == 1)
                {
                    hasDoneLocalizedSearch[i] = true;
                    continue;
                }

                // Check for the possibility of re-scheduling the target node
                if (status == ERR_TIME_BUDGET)
                {
                    int timeDist = getReschedulingTimeDistance(fs, dfg_ops[i], *placed, schedule, II, cst[CST_DIST]);
                    // timeDist can be swapped for cst[CST_DIST] for a faster search
                    int rescheduled = reScheduleNode(schedule, template, d, dfg_ops[i], timeDist, 0, II);
                    reSchedules[i] = timeDist;
                    // Could not reschedule the node
                    if (rescheduled > 0)
                    {
                        i--;
                        continue;
                    }
                    status = ERR_NO_PLACE;
                }
                if (status == ERR_NO_PLACE)
                {
                    if (i == 0)
                    { // backtracked all the way to the first node and no placement worked, even for this first node
                        failed_to_map = 1;
                        break;
                    }
                    deletePlacementMatrix(placementMatrices[get_instr_id(dfg_ops[i]) - 1], fs);
                    placementMatrices[get_instr_id(dfg_ops[i]) - 1] = NULL;
                }
                hasDoneLocalizedSearch[i] = false;

                // Reset the reScheduling performed for this node
                if (reSchedules[i] > 0)
                {
                    reScheduleNode(schedule, template, d, dfg_ops[i], -1 * reSchedules[i], 0, II);
                    reSchedules[i] = 0;
                }
                // Backtrack to the last mapped node
                unmapOp(fs, d, dfg_ops[i - 1], *placed, schedule, II);
                i -= 2;
                n_backtracks++;
                if (n_backtracks > MAX_BACKTRACKS)
                {
                    n_backtracks = 0;
                    for (i = 0; i < get_node_sublist_size(dfg_ops); i++)
                    {
                        // delete all matrices except for the first node
                        if (i == get_instr_id(dfg_ops[0]) - 1)
                            continue;
                        deletePlacementMatrix(placementMatrices[i], fs);
                        placementMatrices[i] = NULL;
                        hasDoneLocalizedSearch[i] = false;
                    }
                    clearMapping(fs, d, dfg_ops, *placed, schedule, II);
                    for (i = 0; i < get_dfg_size(d); i++)
                        schedule[i] = scheduleCopy[i];
                    i = -1;
                }
                continue;
            }
        }
        if (i < N)
        {
            for (i = 0; i < get_node_sublist_size(dfg_ops); i++)
            {
                deletePlacementMatrix(placementMatrices[i], fs);
                placementMatrices[i] = NULL;
                hasDoneLocalizedSearch[i] = false;
            }
            failed_to_map = 1;
            clearMapping(fs, d, dfg_ops, *placed, schedule, II);
            for (i = 0; i < get_dfg_size(d); i++)
                schedule[i] = scheduleCopy[i];
        }
    }

    num_contexts_for_one_iter = max_array(schedule, get_dfg_size(d)) + get_instr_lat(get_dfg_instr(d, max_array_idx(schedule, get_dfg_size(d)))) - 1;

    free(schedule);
    free(scheduleCopy);
    free(scheduleOriginal);
    for (i = 0; i < get_node_sublist_size(dfg_ops); i++)
        deletePlacementMatrix(placementMatrices[i], fs);
    free(placementMatrices);
    free(dfg_ops);
    free(minDist);
    free(reSchedules);
    free(hasDoneLocalizedSearch);

    if ((II > N + 1 || II > maxII) && (*first_mapping) == 1)
    {
        if (verbose)
            printf("Failed to map the target DFG to the target device.\n");
        delete_cgra(fs);
        return NULL;
    }
    if (*first_mapping == 1)
    {
        set_mapping(fs, MAPPER_FINETUNING);
        define_exec_time(fs, d, *placed, II);
        set_num_contexts_for_one_iteration(fs, num_contexts_for_one_iter + 1);
    }
    if ((*first_mapping) == 0 && II <= N && !failed_to_map)
        (*first_mapping) = 1;

    return fs;
}

/*******************************************************************************************
 * mapper_iterative_kernel
 * Inputs: device model, target dfg and list of operations, previously calculated schedule,
 * placement info array, minimum II and first time mapping flag.
 * Kernel for the iterative mapper, representing one mapping attempt. Consists in a basic
 * mapper that tries placing and routing one node at a time and allows for re-scheduling, if
 * deemed necessary.
 * Return values: mapped device
 ******************************************************************************************/
cgra *mapper_iterative_kernel(cgra *template, dfg_instr **dfg_ops, dfg *d, int *schedule, int ***placed, int MII, int *first_mapping)
{

    int *scheduleCopy = (int *)malloc(get_node_sublist_size(dfg_ops) * sizeof(int));

    /**
     * Array for control and status
     * cst[0] = distance needed for rescheduling
     */
    int cst[CST_SIZE] = {0};

    int i, attempts = 0, II, N = get_node_sublist_size(dfg_ops);
    cgra *fs;

    for (i = 0; i < get_node_sublist_size(dfg_ops); i++)
        scheduleCopy[i] = schedule[i];

    if ((*first_mapping) == 1)
        fs = buildBaseCGRA(template, MII);
    else
        fs = template;

    II = MII;
    // Main Mapping Loop
    do
    {
        if (attempts > 100)
        {
            II++;
            /* printf("could not map with II = %d\n",II-1); */
            if (II > N || (*first_mapping) == 0)
                break;
            attempts = 0;
            delete_cgra(fs);
            fs = buildBaseCGRA(template, II);
        }
        // Iterate through all DFG Ops
        for (i = 0; i < N; i++)
        {
            int status = attemptPRNode(fs, d, dfg_ops[i], *placed, schedule, II, cst);

            if (status == STATUS_OK)
                continue;
            if (status == ERR_TIME_BUDGET)
            {
                int timeDist = getReschedulingTimeDistance(fs, dfg_ops[i], *placed, schedule, II, cst[CST_DIST]);
                int rescheduled = reScheduleNode(schedule, template, d, dfg_ops[i], timeDist, 0, II);
                // Could not reschedule the node
                if (rescheduled > 0)
                {
                    i--;
                    continue;
                }
                status = ERR_NO_PLACE;
            }
            if (status != ERR_TIME_BUDGET)
                break;
        }
        if (i < N)
        {
            attempts++;
            clearMapping(fs, d, dfg_ops, *placed, schedule, II);
            // Return to the original schedule
            for (i = 0; i < get_node_sublist_size(dfg_ops); i++)
                schedule[i] = scheduleCopy[i];
            i = 0;
        }

    } while (i < N); // while (!allInstructionsPlaced(*placed, d));

    free(scheduleCopy);

    if (II > N && (*first_mapping) == 1)
    {
        printf("Failed to map the target DFG to the target device.\n");
        return NULL;
    }

    return fs;
}

/**********************************************************************************************
 * mapper_iterative
 * Inputs: device model, target dfg, placement info array, minimum II, first time mapping flag
 * Iterative mapper. Runs a simple mapping kernel (mapper_iterative_kernel) on multiple
 * iterations and stores the best solution.
 * Return values: mapped device
 *********************************************************************************************/
cgra *mapper_iterative(cgra *template, dfg *d, int ***placed, int MII, int *first_mapping)
{

    int max_attempts = 20;
    int i, num_contexts_for_one_iter, II = INFINITY, ***curr_placed, **aux;

    int *schedule = rasMixedScheduling(template, d), *scheduleCopy = (int *)malloc(get_dfg_size(d) * sizeof(int));
    topologicalSortDFG(d);
    dfg_instr **dfg_ins = get_dfg_inputs(d);
    dfg_instr **dfg_ops = get_dfg_ops(d);
    dfg_instr **dfg_outs = get_dfg_outputs(d);
    dfg_ops = merge_sublists(dfg_ins, dfg_ops);
    dfg_ops = merge_sublists(dfg_ops, dfg_outs);

    cgra *curr_attempt, *fs = NULL;

    curr_placed = (int ***)calloc(1, sizeof(int **));
    *curr_placed = (int **)calloc(get_dfg_size(d), sizeof(int *));
    for (i = 0; i < get_dfg_size(d); i++)
        (*curr_placed)[i] = (int *)calloc(4, sizeof(int)); // [placed?, line & column, first_slice, last_slice]

    for (i = 0; i < max_attempts; i++)
    {
        printf("attempt %d.\n", i);
        curr_attempt = mapper_iterative_kernel(template, dfg_ops, d, schedule, placed, MII, first_mapping);

        if (curr_attempt == NULL)
            continue;

        // Found a better solution
        if (get_n_cgra_slices(curr_attempt) < II)
        {
            delete_cgra(fs);
            fs = curr_attempt;
            II = get_n_cgra_slices(fs);

            printf("attempt %d, reached a new best II of %d.\n", i, II);
            aux = *curr_placed;
            *curr_placed = *placed;
            *placed = aux;

            if (II == MII)
                break;
        }
        else
        {
            printf("attempt %d, reached an II of %d.\n", i, get_n_cgra_slices(curr_attempt));
            delete_cgra(curr_attempt);
        }

        // Reset the old placement info array
        for (int j = 0; j < get_dfg_size(d); j++)
        {
            (*placed)[j][0] = 0;
            (*placed)[j][1] = 0;
            (*placed)[j][2] = 0;
            (*placed)[j][3] = 0;
        }
    }

    aux = *curr_placed;
    *curr_placed = *placed;
    *placed = aux;

    num_contexts_for_one_iter = max_array(schedule, get_dfg_size(d)) + get_instr_lat(get_dfg_instr(d, max_array_idx(schedule, get_dfg_size(d)))) - 1;

    free(schedule);
    free(scheduleCopy);
    free(dfg_ops);

    if (*first_mapping == 1)
    {
        set_mapping(fs, MAPPER_ITERATIVE);
        define_exec_time(fs, d, *placed, II);
        set_num_contexts_for_one_iteration(fs, num_contexts_for_one_iter + 1);
    }
    if ((*first_mapping) == 0 && fs != NULL)
        (*first_mapping) = 1;

    for (int k = 0; k < get_dfg_size(d); k++)
        free(curr_placed[0][k]);
    free(curr_placed[0]);
    free(curr_placed);
    return fs;
}

/**********************************************************************************************
 * mapper_simAnnealing
 * Inputs: device model, target dfg, placement info array, minimum II, first time mapping flag
 * Simulated annealing mapper. Runs a simulated annealing algorithm to map the target DFG to the
 * target device. Algorithm structure inspired by the paper 'DRESC'. Contrary to the paper, however,
 * the cost function is based on routability, instead of resource oversuse. This is inspired by the
 * cost function employed by paper 'SPR'. Also inspired by SPR, the cooling schedule is based on
 * the paper 'VPR' (as shown by 'SPR'). The temperature initialization, update and annealing end
 * conditions are all based on that paper.
 * Return values: mapped device
 *********************************************************************************************/

cgra *mapper_simAnnealing(cgra *template, dfg *d, int ***placed, int MII, int *first_mapping, int swap_EN)
{

    int *schedule = rasMixedScheduling(template, d), *scheduleCopy = (int *)malloc(get_dfg_size(d) * sizeof(int));
    topologicalSortDFG(d);
    dfg_instr **dfg_ins = get_dfg_inputs(d);
    dfg_instr **dfg_ops = get_dfg_ops(d);
    dfg_instr **dfg_outs = get_dfg_outputs(d);
    dfg_ops = merge_sublists(dfg_ins, dfg_ops);
    dfg_ops = merge_sublists(dfg_ops, dfg_outs);
    cgra *fs, *sacrificial_lamb;
    temperature *t;
    int i, j, s, II = MII, N = get_node_sublist_size(dfg_ops), totalMoves = 0, acceptedMoves = 0;
    int maxII = getSerialExecLat(d), num_contexts_for_one_iter;
    float *cost, *moveCost, initTempValue, moves;

    // Array that tracks which nodes were successfully routed
    int *routed = (int *)calloc(N + 1, sizeof(int)), *routedBackup = (int *)calloc(N + 1, sizeof(int));
    int **placedBackup = (int **)malloc(N * sizeof(int *));
    int **fpos, Npos, rnode;
    int *reScheduled = (int *)calloc(N, sizeof(int));

    for (i = 0; i < N; i++)
    {
        placedBackup[i] = malloc(4 * sizeof(int));
    }

    for (i = 0; i < get_dfg_size(d); i++)
        scheduleCopy[i] = schedule[i];

    while (II < maxII)
    {
        fs = buildBaseCGRA(template, II);
        cost = generateInitialPlacement(fs, d, dfg_ops, *placed, schedule, II, routed);

        // Compute an initial temperature value equal to 20x the move cost's std deviation
        initTempValue = 20 * computeMoveCostStdDev(fs, d, dfg_ops, placed, schedule, II, cost, N);
        /* printf("Init temperature is: %.2f\n",initTempValue); */
        t = initTemperature(initTempValue); // placeholder value
        copyArray(routedBackup, routed, N + 1);
        for (i = 0; i < N; i++)
            copyArray(placedBackup[i], (*placed)[i], 4);
        moves = 0;
        while (routed[N] < N)
        {
            // Choose a random operation in the schedule
            rnode = rand() % N;

            /* for (i = 0; i < 4; i++)
                placedBackup[get_instr_id(dfg_ops[rnode]) - 1][i] = (*placed)[get_instr_id(dfg_ops[rnode]) - 1][i]; */

            int *mob = getFixedNodeMobility(schedule, dfg_ops[rnode]);
            int acceptance, swapped_id = 0;

            for (s = mob[0]; s < mob[1] + 1; s++)
            {
                schedule[get_instr_id(dfg_ops[rnode]) - 1] = s;
                if (swap_EN == 1)
                    fpos = getPlaceablePositions(fs, dfg_ops[rnode], *placed, schedule, II);
                else
                    fpos = getFreePositions(fs, dfg_ops[rnode], *placed, schedule, II);
                if (fpos == NULL)
                    continue;
                Npos = fpos[0][0];

                // Random positions to try
                for (j = 0; j < Npos; j++)
                {
                    // God bless
                    sacrificial_lamb = copy_all_cgra_slices(fs);
                    if (swap_EN == 1)
                    {
                        swapped_id = 0;
                        moveCost = anneal_swap(sacrificial_lamb, d, dfg_ops, dfg_ops[rnode], fpos[j + 1][0], fpos[j + 1][1],
                                               *placed, schedule, II, cost, routed, &swapped_id);
                    }
                    else
                    {
                        moveCost = m1(sacrificial_lamb, d, dfg_ops, dfg_ops[rnode], fpos[j + 1][0], fpos[j + 1][1], *placed, schedule, II, cost, routed);
                    }

                    acceptance = evaluateMoveCost(t, cost, moveCost, N);

                    totalMoves++;
                    if (acceptance == 1)
                    {
                        /* printf("Accepted Move!\n"); */
                        acceptedMoves++;
                        delete_cgra(fs);
                        fs = sacrificial_lamb;
                        updateCost(cost, moveCost, N);
                        for (i = 0; i < 4; i++)
                            placedBackup[get_instr_id(dfg_ops[rnode]) - 1][i] = (*placed)[get_instr_id(dfg_ops[rnode]) - 1][i];
                        // In case of an M2 operation, update the second target node
                        if (swapped_id > 0)
                        {
                            for (i = 0; i < 4; i++)
                                placedBackup[swapped_id - 1][i] = (*placed)[swapped_id - 1][i];
                        }
                        // Update routed backup
                        copyArray(routedBackup, routed, N + 1);
                        // Move was accepted, move on to the next node
                        break;
                    }
                    else
                    {
                        /* printf("Rejected move.\n"); */
                        delete_cgra(sacrificial_lamb);
                        // Restore old node position
                        for (i = 0; i < 4; i++)
                            (*placed)[get_instr_id(dfg_ops[rnode]) - 1][i] = placedBackup[get_instr_id(dfg_ops[rnode]) - 1][i];
                        schedule[get_instr_id(dfg_ops[rnode]) - 1] = placedBackup[get_instr_id(dfg_ops[rnode]) - 1][2];
                        // In case of an M2 operation, restore the second target node
                        if (swapped_id > 0)
                        {
                            for (i = 0; i < 4; i++)
                                (*placed)[swapped_id - 1][i] = placedBackup[swapped_id - 1][i];
                            schedule[swapped_id - 1] = placedBackup[swapped_id - 1][2];
                        }
                        // Restore old routed
                        copyArray(routed, routedBackup, N + 1);
                        free(moveCost);
                    }
                }
                deleteFreePosArr(fpos, fs);

                if (acceptance == 1)
                    break;
            }

            free(mob);

            // Check if all nodes have successfully been now mapped
            checkValidMapping(routed, N);
            if (routed[N] == N)
                break;

            moves++;
            if (moves >= N)
            {
                moves = 0;
                updateTemperature(t, acceptedMoves, totalMoves);
                printf("temperature is %.2f, total cost is %.2f\n", getTemperature(t), array_sum(cost, N));

                if (checkTemperatureStopCriteria(t, cost, N) == 1)
                {
                    display_cgra_in_time(fs, d);
                    break;
                }
            }
        }

        deleteTemperature(t);
        free(cost);

        if (routed[N] == N)
        {
            printf("Successfully routed all %d DFG nodes. Mapping successfully complete.\n", N);
            break;
        }

        // Check for schedule padding candidates
        int padScheduling = 0;
        for (i = 0; i < N; i++)
        {
            if (routed[i] == 0 && reScheduled[i] == 0)
            {
                padScheduling = 1;
                reScheduled[i] = 1;
                int timeDist = getReschedulingTimeDistance(fs, dfg_ops[i], *placed, schedule, II,
                                                           computeCost(fs, dfg_ops[i], *placed, schedule, II, 0));
                reScheduleNode(schedule, template, d, dfg_ops[i], timeDist, 0, II);
            }
        }
        delete_cgra(fs);

        if (padScheduling == 0)
        {
            copyArray(schedule, scheduleCopy, N);
            for (i = 0; i < N; i++)
                reScheduled[i] = 0;
            printf("Failed to map with II = %d.\n", II);
            II++;
        }
        else
            printf("Padding the scheduling.\n");
    }

    num_contexts_for_one_iter = max_array(schedule, get_dfg_size(d)) + get_instr_lat(get_dfg_instr(d, max_array_idx(schedule, get_dfg_size(d)))) - 1;

    if (*first_mapping == 1)
    {
        set_mapping(fs, MAPPER_SIM_ANNEALING);
        define_exec_time(fs, d, *placed, II);
        set_num_contexts_for_one_iteration(fs, num_contexts_for_one_iter + 1);
    }
    if ((*first_mapping) == 0 && fs != NULL)
        (*first_mapping) = 1;

    for (i = 0; i < N; i++)
        free(placedBackup[i]);
    free(placedBackup);
    free(schedule);
    free(scheduleCopy);
    free(routedBackup);
    free(routed);
    free(dfg_ops);
    free(reScheduled);

    return fs;
}

/*****************************************************************************************************
 * HandOfGod
 * Inputs: device model, target dfg, placement info array, first time mapping flag and a mapper select
 * Top function for mapping. Calls the desired mapping function, according to the mapper select
 * variable (mapper).
 * Return values: mapped device
 ****************************************************************************************************/
cgra *HandOfGod(cgra *template, dfg *d, int ***placed, int *first_mapping, int mapper, int maxII, int verbose)
{
    /**
     * placed[id-1] = {placed ? 1:0, pos = iC + j, sched first context, sched last context}
     */
    int *schedule = rasMixedScheduling(template, d);
    int MII = getMII(template, d, schedule);

    free(schedule);
    int seed;
    cgra *fs;
    double start, end;
    double cpu_time_used;

    seed = time(NULL);
    //seed = 1747756595;

    srand(seed);
    
    if (verbose)
        start = omp_get_wtime();

    // printf("MII is %d\n", MII);

    /* // Simmap test
    schedule = rasMixedScheduling(template, d);
    MII = max_array(schedule, get_dfg_size(d)) + 1; // "MII" is literally the number of slices here
    fs = buildBaseCGRA(template, MII);
    dfg_instr **dfg_ops = get_dfg_ops(d);
    __simmap__placeAndRouteNode(fs, d, dfg_ops[0], *placed, schedule);
    __simmap__placeAndRouteNode(fs, d, dfg_ops[1], *placed, schedule);
    __simmap__placeAndRouteNode(fs, d, dfg_ops[2], *placed, schedule);
    __simmap__placeAndRouteNode(fs, d, dfg_ops[3], *placed, schedule);
    return fs; */
    /*
        fs = buildBaseCGRA(template, 1);
        schedule = rasMixedScheduling(fs, d);
        dfg_instr **dfg_ops = get_dfg_ops(d);

        placeOp(fs, 1, 1, dfg_ops[0], *placed, schedule, 1);
        placeOp(fs, 3, 2, dfg_ops[1], *placed, schedule, 1);
        placeOp(fs, 2, 4, dfg_ops[2], *placed, schedule, 1);
        placeOp(fs, 3, 4, dfg_ops[3], *placed, schedule, 1);

        int status;
        status = __spatial__routeOp(fs, dfg_ops[1], *placed, schedule, 1);
        if (status == 0)
            printf("failed @ routing op 2\n");

        status = __spatial__routeOp(fs, dfg_ops[2], *placed, schedule, 1);
        if (status == 0)
            printf("failed @ routing op 3\n");

        status = __spatial__routeOp(fs, dfg_ops[3], *placed, schedule, 1);
        if (status == 0)
            printf("failed @ routing op 4\n");

        display_cgra_in_time(fs, d);
        exit(0);
     */
    switch (mapper)
    {
    // Mapper with localized search, rescheduling, and complimented by some backtracking
    case MAPPER_FINETUNING:
        if (verbose)
            printf("Mapper: Fine Tuning\n");
        fs = mapper_fineTuning(template, d, placed, MII, first_mapping, maxII, verbose);
        break;
    // Iterative Mapper. Each iteration consists in a basic mapper with dynamic node rescheduling
    case MAPPER_ITERATIVE:
        if (verbose)
            printf("Mapper: Iterative\n");
        fs = mapper_iterative(template, d, placed, MII, first_mapping);
        break;
    case MAPPER_SIM_ANNEALING:
        if (verbose)
            printf("Mapper: Simulated Annealing\n");
        // fs = mapper_simAnnealing(template, d, placed, MII, first_mapping, 1);
        fs = mapper_simAnnealing(template, d, placed, MII, first_mapping, 1);
        break;
    default:
        if (verbose)
            printf("Default Mapper (Fine Tuning)\n");
        fs = mapper_fineTuning(template, d, placed, MII, first_mapping, maxII, verbose);
        break;
    }

    // For the first mapping, set the MII
    if (fs != NULL && (*first_mapping == 1))
    {
        setDeviceMII(fs, MII);
    }

    // end = clock();
    if (verbose)
    {
        end = omp_get_wtime();
        cpu_time_used = ((double)(end - start)); // / CLOCKS_PER_SEC;
        printf("execution time: %lf\n", cpu_time_used);
    }

    return fs;
}