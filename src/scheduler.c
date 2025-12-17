#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <time.h>
#include "ops.h"
#include "dfg.h"
#include "cgra.h"
#include "Item.h"
#include "pqueue.h"
#include "stack.h"
#include "files.h"
#include <omp.h>
#include <time.h>

#define MAX(a, b) a > b ? a : b
#define MIN(a, b) a < b ? a : b

typedef struct _sortItem
{
    int id;
    int height;
} sortItem;

/** Comparator for qsort */
int compareSch(const void *a, const void *b)
{
    return ((sortItem *)a)->height - ((sortItem *)b)->height;
}

/** Comparator for qsort */
int compareTS(const void *a, const void *b)
{
    return ((sortItem *)b)->height - ((sortItem *)a)->height;
}

void getRequiredResources(dfg *d, int dfg_resources[OP_MAX])
{

    int i, N = get_dfg_size(d);
    dfg_instr *curr;

    // Calculate the DFG Resources Required
    for (i = 0; i < N; i++)
    {
        curr = get_dfg_instr(d, i);
        dfg_resources[get_operation_index(get_instr_op(curr))]++;
    }
}

void getAvailableResources(cgra *template, int cgra_resources[OP_MAX])
{
    int i, j, k;
    // Calculate the CGRA Resources Avaliable
    for (i = 0; i < get_cgra_L(template); i++)
    {
        for (j = 0; j < get_cgra_C(template); j++)
        {
            for (k = 0; k < OP_MAX; k++)
            {
                if (peHasFunct(template, i, j, k))
                {
                    cgra_resources[k]++;
                    // break;
                }
            }
        }
    }
    if (cgra_resources[OP_STREAM_IN] > get_cgra_ld_trghpt(template))
        cgra_resources[OP_STREAM_IN] = get_cgra_ld_trghpt(template);
    if (cgra_resources[OP_STREAM_OUT] > get_cgra_st_trghpt(template))
        cgra_resources[OP_STREAM_OUT] = get_cgra_st_trghpt(template);
}

int allScheduled(int *scheduled, int N, int asap_or_alap)
{
    int i;

    for (i = 0; i < N; i++)
    {
        if ((scheduled[i] == -1 && asap_or_alap == 1) || (scheduled[i] == 1 && asap_or_alap == 0))
            return 0;
    }
    return 1;
}

/* ***************************************************************************************************************
 * getNodeMobility
 * Inputs: ASAP scheduling of the nodes, ALAP scheduling of the nodes and the number of nodes, N
 * Returns an array with the mobility of each node.
 *****************************************************************************************************************/
// Returns the mobility of the nodes in the DFG, taking the ASAP and ALAP schedulings into account
int *getNodeMobility(int *asap, int *alap, int N)
{
    int i;
    int *mobility = (int *)malloc(N * sizeof(int));
    for (i = 0; i < N; i++)
    {
        mobility[i] = alap[i] - asap[i];
    }
    return mobility;
}

/* ***************************************************************************************************************
 * getNodeMobility
 * Inputs: scheduling of the nodes and the target node
 * Returns returns the minimum and maximum schedule time of the target node
 *****************************************************************************************************************/
int *getFixedNodeMobility(int *scheduled, dfg_instr *target)
{
    int k, t = scheduled[get_instr_id(target) - 1], minT = t, maxT = t;

    for (k = 0; k < get_n_inputs(target); k++)
        minT = MIN(minT, t - (scheduled[get_instr_id(get_input(target, k)) - 1] + get_instr_lat(get_input(target, k))));

    if (k == 0)
        minT = 0;

    for (k = 0; k < get_n_outputs(target); k++)
        maxT = MIN(maxT, scheduled[get_instr_id(get_output(target, k)) - 1] - (t + get_instr_lat(target)));

    if (k == 0)
        maxT = 0;

    int *scheduleArr = (int *)malloc(2 * sizeof(int));
    scheduleArr[0] = t - minT;
    scheduleArr[1] = t + maxT;

    return scheduleArr;
}

/* ***************************************************************************************************************
 * Resource Aware Scheduling (RAS): ASAP. Accepts operations with varying latencies.
 * Returns the scheduling of all the nodes in the DFG
 *****************************************************************************************************************/
int *rasASAP(cgra *template, dfg *d)
{

    int i, j, k, max, lat = getHighestInstrLat(d), id, iid, N = get_dfg_size(d), inputs, op_idx, resSz = getSerialExecLat(d);
    dfg_instr *curr;
    int *scheduled = (int *)malloc(N * sizeof(int));
    int *endOfSchedule = (int *)malloc(N * sizeof(int)); // has end of the schedule of each operation
    int **cgra_resources = (int **)malloc(resSz * sizeof(int *));

    for (i = 0; i < N; i++)
    {
        scheduled[i] = -1;
        endOfSchedule[i] = -1;
    }
    for (i = 0; i < resSz; i++)
    {
        cgra_resources[i] = (int *)calloc(OP_MAX, sizeof(int));
        // Get the CGRA Resources Available
        getAvailableResources(template, cgra_resources[i]);
    }

    // Schedule the instructions:
    // all instructions with no inputs to the first cycle
    // all other instructions to the first cycle AFTER all its inputs
    do
    {
        for (i = 0; i < N; i++)
        {
            curr = get_dfg_instr(d, i);
            id = get_instr_id(curr) - 1;
            op_idx = get_operation_index(get_instr_op(curr));
            lat = get_instr_lat(curr);

            if (scheduled[id] >= 0)
                continue;

            inputs = get_n_inputs(curr);

            if (inputs == 0)
            {
                // Schedule for the first cycle with available resources
                for (j = 0; j < resSz; j++)
                {
                    if (cgra_resources[j][op_idx] > 0)
                    {
                        scheduled[id] = j;
                        cgra_resources[j][op_idx]--;
                        break;
                    }
                    if (cgra_resources[j][OP_ALU] > 0 && (op_idx >= OP_ADD && op_idx < OP_LOAD))
                    {
                        scheduled[id] = j;
                        cgra_resources[j][OP_ALU]--;
                        break;
                    }
                    if (cgra_resources[j][OP_LSU] > 0 && (op_idx >= OP_LOAD && op_idx <= OP_STORE))
                    {
                        scheduled[id] = j;
                        cgra_resources[j][OP_LSU]--;
                        break;
                    }
                    if (cgra_resources[j][OP_FULL] > 0 && (op_idx >= OP_ADD && op_idx < OP_MAX))
                    {
                        scheduled[id] = j;
                        cgra_resources[j][OP_FULL]--;
                        break;
                    }
                }
                // No resources available at all!
                if (j == resSz)
                {
                    printf("No resources available for instruction %d. Cannot schedule the target DFG.\n", id + 1);
                    for (i = 0; i < resSz; i++)
                        free(cgra_resources[i]);
                    free(cgra_resources);
                    free(scheduled);
                    return NULL;
                }
                // Mark the end of the schedule
                endOfSchedule[id] = scheduled[id] + lat;
            }
            else
            {
                max = -1;
                for (k = 0; k < inputs; k++)
                {
                    iid = get_instr_id(get_input(curr, k)) - 1;
                    if (scheduled[iid] == -1)
                        break;
                    /* if (max < scheduled[iid])
                        max = scheduled[iid]; */
                    if (max < endOfSchedule[iid])
                    {
                        max = endOfSchedule[iid];
                    }
                }
                // All inputs were already scheduled
                if (k == inputs)
                {
                    // Schedule for the first cycle with available resources
                    for (j = max /* + 1 */; j < resSz; j++)
                    {
                        if (cgra_resources[j][op_idx] > 0)
                        {
                            scheduled[id] = j;
                            cgra_resources[j][op_idx]--;
                            break;
                        }
                        if (cgra_resources[j][OP_ALU] > 0 && (op_idx >= OP_ADD && op_idx < OP_LOAD))
                        {
                            scheduled[id] = j;
                            cgra_resources[j][OP_ALU]--;
                            break;
                        }
                        if (cgra_resources[j][OP_LSU] > 0 && (op_idx >= OP_LOAD && op_idx <= OP_STORE))
                        {
                            scheduled[id] = j;
                            cgra_resources[j][OP_LSU]--;
                            break;
                        }
                        if (cgra_resources[j][OP_FULL] > 0 && (op_idx >= OP_ADD && op_idx < OP_MAX))
                        {
                            scheduled[id] = j;
                            cgra_resources[j][OP_FULL]--;
                            break;
                        }
                    }
                    // No resources available at all!
                    if (j == resSz)
                    {
                        printf("No resources available for instruction %d. Cannot schedule the target DFG.\n", id + 1);
                        for (i = 0; i < resSz; i++)
                            free(cgra_resources[i]);
                        free(cgra_resources);
                        free(scheduled);
                        return NULL;
                    }
                    // Mark the end of the schedule
                    endOfSchedule[id] = scheduled[id] + lat;
                }
            }
        }
    } while (!allScheduled(scheduled, N, ASAP));

    /*     printf("\nScheduling:\n");
        for (i = 0; i < N; i++)
        {
            printf("[%d]: cycle %d (-> %d)\n", get_instr_id(get_dfg_instr(d, i)), scheduled[get_instr_id(get_dfg_instr(d, i)) - 1], endOfSchedule[get_instr_id(get_dfg_instr(d, i)) - 1]);
        }
        printf("------------\n\n"); */
    for (i = 0; i < resSz; i++)
        free(cgra_resources[i]);
    free(cgra_resources);
    free(endOfSchedule);
    return scheduled;
}

/* ***************************************************************************************************************
 * Resource Aware Scheduling (RAS): ALAP. Accepts operations with varying latencies.
 * Returns the scheduling of all the nodes in the DFG
 *****************************************************************************************************************/
int *rasALAP(cgra *template, dfg *d)
{
    int i, j, k, min, min_sched = 1, lat = getHighestInstrLat(d), id, oid, N = get_dfg_size(d), outputs, op_idx, resSz = getSerialExecLat(d);
    dfg_instr *curr;
    int *scheduled = (int *)malloc(N * sizeof(int));
    int *endOfSchedule = (int *)malloc(N * sizeof(int)); // has end of the schedule of each operation
    int **cgra_resources = (int **)malloc(resSz * sizeof(int *));

    for (i = 0; i < N; i++)
    {
        scheduled[i] = 1;
        endOfSchedule[i] = 1;
    }
    for (i = 0; i < resSz; i++)
    {
        cgra_resources[i] = (int *)calloc(OP_MAX, sizeof(int));
        // Get the CGRA Resources Available
        getAvailableResources(template, cgra_resources[i]);
    }

    // Schedule the instructions:
    // all instructions with no outputs to cycle 0
    // all other instructions to the first cycle BEFORE all its outputs
    // the result will have negative cycle values in the schedule, to which an offset can be added
    do
    {

        for (i = 0; i < N; i++)
        {
            curr = get_dfg_instr(d, i);
            id = get_instr_id(curr) - 1;
            op_idx = get_operation_index(get_instr_op(curr));
            lat = get_instr_lat(curr);

            if (scheduled[id] <= 0)
                continue;

            outputs = get_n_outputs(curr);

            if (outputs == 0)
            {
                // Schedule for the last cycle with available resources
                for (j = 0; j < resSz; j++)
                {
                    if (cgra_resources[j][op_idx] > 0)
                    {
                        scheduled[id] = -j;
                        cgra_resources[j][op_idx]--;
                        break;
                    }
                    if (cgra_resources[j][OP_ALU] > 0 && (op_idx >= OP_ADD && op_idx < OP_LOAD))
                    {
                        scheduled[id] = -j;
                        cgra_resources[j][OP_ALU]--;
                        break;
                    }
                    if (cgra_resources[j][OP_LSU] > 0 && (op_idx >= OP_LOAD && op_idx <= OP_STORE))
                    {
                        scheduled[id] = -j;
                        cgra_resources[j][OP_LSU]--;
                        break;
                    }
                    if (cgra_resources[j][OP_FULL] > 0 && (op_idx >= OP_ADD && op_idx < OP_MAX))
                    {
                        scheduled[id] = -j;
                        cgra_resources[j][OP_FULL]--;
                        break;
                    }
                }
                // No resources available at all!
                if (j == -N)
                {
                    printf("No resources available for instruction %d. Cannot schedule the target DFG.\n", id + 1);
                    for (i = 0; i < resSz; i++)
                        free(cgra_resources[i]);
                    free(cgra_resources);
                    free(scheduled);
                    return NULL;
                }
                // Mark the end of the schedule
                endOfSchedule[id] = scheduled[id] - lat;
            }
            else
            {
                min = 1;
                for (k = 0; k < outputs; k++)
                {
                    oid = get_instr_id(get_output(curr, k)) - 1;
                    if (scheduled[oid] == 1)
                        break;
                    if (min > endOfSchedule[oid])
                        min = endOfSchedule[oid];
                }
                // All outputs were already scheduled
                if (k == outputs)
                {
                    // Schedule for the first cycle with available resources
                    for (j = /* 1 */ -min; j < resSz; j++)
                    {
                        if (cgra_resources[j][op_idx] > 0)
                        {
                            scheduled[id] = -j;
                            cgra_resources[j][op_idx]--;
                            break;
                        }
                        if (cgra_resources[j][OP_ALU] > 0 && (op_idx >= OP_ADD && op_idx < OP_LOAD))
                        {
                            scheduled[id] = -j;
                            cgra_resources[j][OP_ALU]--;
                            break;
                        }
                        if (cgra_resources[j][OP_LSU] > 0 && (op_idx >= OP_LOAD && op_idx <= OP_STORE))
                        {
                            scheduled[id] = -j;
                            cgra_resources[j][OP_LSU]--;
                            break;
                        }
                        if (cgra_resources[j][OP_FULL] > 0 && (op_idx >= OP_ADD && op_idx < OP_MAX))
                        {
                            scheduled[id] = -j;
                            cgra_resources[j][OP_FULL]--;
                            break;
                        }
                    }
                    // No resources available at all!
                    if (j == -N)
                    {
                        printf("No resources available for instruction %d. Cannot schedule the target DFG.\n", id + 1);
                        for (i = 0; i < resSz; i++)
                            free(cgra_resources[i]);
                        free(cgra_resources);
                        free(scheduled);
                        return NULL;
                    }
                    // Mark the end of the schedule
                    endOfSchedule[id] = scheduled[id] - lat;
                }
            }
        }

    } while (!allScheduled(scheduled, N, ALAP));

    for (i = 0; i < resSz; i++)
    {
        free(cgra_resources[i]);
    }
    for (i = 0; i < N; i++)
    {
        if (endOfSchedule[i] < min_sched)
            min_sched = endOfSchedule[i];
    }
    free(cgra_resources);
    for (i = 0; i < N; i++)
    {
        scheduled[i] -= min_sched;
        endOfSchedule[i] -= min_sched;
        register int a = scheduled[i];
        scheduled[i] = endOfSchedule[i];
        endOfSchedule[i] = a;
    }
    /*     printf("\nScheduling:\n");
        for (i = 0; i < N; i++)
        {
            printf("[%d]: cycle %d (-> %d)\n", get_instr_id(get_dfg_instr(d, i)), scheduled[get_instr_id(get_dfg_instr(d, i)) - 1], endOfSchedule[get_instr_id(get_dfg_instr(d, i)) - 1]);
        }
        printf("------------\n\n"); */
    free(endOfSchedule);
    return scheduled;
}

/* ***************************************************************************************************************
 * Resource Aware Scheduling (RAS): Mixed Scheduling. Accepts operations with varying latencies.
 * Returns the scheduling of all the nodes in the DFG
 *****************************************************************************************************************/
int *rasMixedScheduling(cgra *template, dfg *d)
{
    // If topological sort has been run before on this DFG, restore it
    if (is_dfg_sorted(d))
        restore_dfg(d);

    int *asap = rasASAP(template, d), *asapEnd;
    int *alap = rasALAP(template, d), *alapEnd;
    int *mobility = getNodeMobility(asap, alap, get_dfg_size(d));
    int i, j, lat, N = get_dfg_size(d), ongoing, start;
    int *schedule = (int *)malloc(N * sizeof(int));
    asapEnd = (int *)malloc(N * sizeof(int));
    alapEnd = (int *)malloc(N * sizeof(int));

    int resSz = getSerialExecLat(d), **cgra_resources = (int **)malloc(resSz * sizeof(int *));
    for (i = 0; i < resSz; i++)
    {
        cgra_resources[i] = (int *)calloc(OP_MAX, sizeof(int));
        // Get the CGRA Resources Available
        getAvailableResources(template, cgra_resources[i]);
    }

    for (i = 0; i < N; i++)
    {
        schedule[i] = -1;
        asapEnd[i] = asap[i] + get_instr_lat(get_dfg_instr(d, i));
        alapEnd[i] = alap[i] + get_instr_lat(get_dfg_instr(d, i));
    }

    sortItem *si = (sortItem *)malloc(get_dfg_size(d) * sizeof(sortItem));
    for (i = 0; i < N; i++)
    {
        si[i].id = i;
        si[i].height = mobility[i];
    }
    qsort(si, N, sizeof(sortItem), compareSch);

    for (i = 0; i < N; i++)
    {
        if (mobility[si[i].id] > 0)
            break;
        // Schedule nodes that are part of the critical path (mobility 0)
        schedule[si[i].id] = asap[si[i].id]; // both asap and alap will give the same value
        cgra_resources[asap[si[i].id]][get_operation_index(get_instr_op(get_dfg_instr(d, si[i].id)))]--;
    }
    start = i;

    // Assume ASAP for output nodes
    for (i = 0; i < N; i++)
    {
        if (mobility[si[i].id] > 0 && get_n_outputs(get_dfg_instr(d, si[i].id)) == 0)
        {
            schedule[si[i].id] = asap[si[i].id];
            cgra_resources[asap[si[i].id]][get_operation_index(get_instr_op(get_dfg_instr(d, si[i].id)))]--;
        }
    }

    // For the rest assume as late as possible, respecting the dependencies with the already placed nodes
    do
    {
        ongoing = 0;
        for (i = start; i < N; i++)
        {

            if (schedule[si[i].id] != -1)
            {
                if (i == start)
                    start++;
                continue;
            }

            // start with the node's ALAP
            int pos = alap[si[i].id];
            // Resource aware scheduling of the node
            while (pos >= 0 && cgra_resources[pos][get_operation_index(get_instr_op(get_dfg_instr(d, si[i].id)))] <= 0)
            {
                /* printf("not enough resources to schedule node %d @ pos %d\n",si[i].id, pos); */
                pos--;
                if (pos == 0)
                    break;
            }

            lat = get_instr_lat(get_dfg_instr(d, si[i].id));
            for (j = 0; j < get_n_outputs(get_dfg_instr(d, si[i].id)); j++)
            {
                if (schedule[get_instr_id(get_output(get_dfg_instr(d, si[i].id), j)) - 1] == -1)
                {
                    ongoing = 2;
                    break;
                }

                // if an output is scheduled earlier, such that it would conflict with this node's ALAP, then schedule this node earlier
                if (schedule[get_instr_id(get_output(get_dfg_instr(d, si[i].id), j)) - 1] < pos + lat)
                {
                    pos = schedule[get_instr_id(get_output(get_dfg_instr(d, si[i].id), j)) - 1] - lat;
                    // Resource aware scheduling of the node
                    while (pos >= 0 && cgra_resources[pos][get_operation_index(get_instr_op(get_dfg_instr(d, si[i].id)))] <= 0)
                    {
                        /* printf("2not enough resources to schedule node %d @ pos %d\n",si[i].id, pos); */
                        pos--;
                        if (pos <= 0)
                            break;
                    }
                }
                if (pos < 0)
                {
                    printf("Failed to create a valid schedule for the DFG (pos =%d).\n", pos);
                    free(si);
                    free(asap);
                    free(alap);
                    free(mobility);
                    free(schedule);
                    for (i = 0; i < resSz; i++)
                        free(cgra_resources[i]);
                    free(cgra_resources);
                    return NULL;
                }
            }
            // node cannot be scheduled yet
            if (ongoing == 2)
            {
                ongoing = 1;
                continue;
            }
            schedule[si[i].id] = pos;
            cgra_resources[pos][get_operation_index(get_instr_op(get_dfg_instr(d, si[i].id)))]--; // update available resources
        }

    } while (ongoing);

    free(si);
    free(asap);
    free(alap);
    free(asapEnd);
    free(alapEnd);
    free(mobility);
    for (i = 0; i < resSz; i++)
        free(cgra_resources[i]);
    free(cgra_resources);
    return schedule;
}

/* ***************************************************************************************************************
 * Modulo Scheduling: Returns the target schedule adapted to a target modulus, II
 *****************************************************************************************************************/
int *modulo_scheduling(int *scheduled, dfg *d, int II)
{
    int i, N = get_dfg_size(d);
    int *out = (int *)malloc(N * sizeof(int));

    for (i = 0; i < N; i++)
    {
        out[i] = scheduled[i] % II;
    }

    return out;
}

void adjustModuloScheduling(int *s, int *sc, int *so, cgra *c, dfg *d, dfg_instr **ops, int II)
{
    dfg_instr **ins = get_dfg_inputs(d), **outs = get_dfg_outputs(d);
    int *inSched = (int *)calloc(II, sizeof(int)), *outSched = (int *)calloc(II, sizeof(int));
    int i, k, n_ins = get_node_sublist_size(ins), n_outs = get_node_sublist_size(outs), sched, ms;
    int max_ins = get_cgra_ld_trghpt(c), max_outs = get_cgra_st_trghpt(c);

    for (i = 0; i < get_dfg_size(d); i++)
    {
        s[i] = so[i];
    }

    // Try rescheduling the node forward (might try backward as well, in the future)
    for (i = 0; i < n_ins; i++)
    {
        sched = so[get_instr_id(ins[i]) - 1];
        // printf("input %d: %d\n", i, sched);

        for (k = 0; k < II; k++)
        {
            ms = (sched + k) % II;
            if (inSched[ms] >= max_ins)
            {
                // printf("cannot schedule input %d onto cycle %d!\n", i, sched + k);
                continue;
            }
            // printf("can indeed schedule input %d onto cycle %d (context %d)!\n", i, sched + k, ms);
            inSched[ms]++;
            if (k > 0)
            {
                reScheduleNode(s, c, d, ins[i], k, 0, II);
            }
            break;
        }
    }

    for (i = 0; i < n_outs; i++)
    {
        sched = so[get_instr_id(outs[i]) - 1];
        // printf("output %d: %d\n", i, sched);

        for (k = 0; k < II; k++)
        {
            ms = (sched + k) % II;
            if (outSched[ms] >= max_outs)
            {
                // printf("cannot schedule output %d onto cycle %d!\n", i, sched + k);
                continue;
            }
            // printf("can indeed schedule output %d onto cycle %d (context %d)!\n", i, sched + k, ms);
            outSched[ms]++;
            if (k > 0)
            {
                reScheduleNode(s, c, d, outs[i], k, 0, II);
            }
            break;
        }
    }

    // Copy the schedule to scheduleCopy aswell
    for (k = 0; k < get_dfg_size(d); k++)
    {
        sc[k] = s[k];
    }

    free(inSched);
    free(outSched);
    free(ins);
    free(outs);
}

/* **************************************************************************************************************
 * searchPath: Primitive function for isInRecurrenceCycle
 * Inputs: target dfg, target start and end nodes of the path
 * Searches for a path between the two nodes, using DFS.
 * Return values: path exists ? 1 : 0
 ****************************************************************************************************************/
int searchPath(dfg *d, dfg_instr *first, dfg_instr *last)
{
    int k, path_exists = 0;
    stack *s = createStack(get_dfg_size(d));
    dfg_instr *curr, *out;
    push(s, (Item)first);
    while (!isEmpty(s))
    {
        curr = (dfg_instr *)pop(s);
        for (k = 0; k < get_n_outputs(curr); k++)
        {
            out = get_output(curr, k);
            if (out == last)
            {
                path_exists = 1;
                break;
            }
            push(s, (Item)out);
        }
    }
    deleteStack(s);
    return path_exists;
}

/* **************************************************************************************************************
 * isInRecurrenceCycle: Primitive function for reScheduleNode
 * Inputs: target dfg, target node
 * Checks wheter the target node is part of a recurrence cycle. This is done by search for paths between the start
 * node and the target, and between the target and the end node, for each pair of start and end nodes that form a
 * recurrence edge.
 * Return values: target node is in a recurrence cycle ? MII : 0, where MII denotes the Minimum II imposed by this
 * recurrence
 ****************************************************************************************************************/
int isInRecurrenceCycle(dfg *d, dfg_instr *target, int *schedule)
{

    int path, *n_rec;
    Item *rec_list = get_all_recurrences(d);
    if (rec_list == NULL)
        return 0;

    n_rec = (int *)(rec_list[0]);
    if (n_rec == NULL)
    {
        free(rec_list);
        return 0;
    }

    for (int i = 0; i < *n_rec; i++)
    {
        dfg_instr *start = rec_list[2 * i + 2];
        dfg_instr *end = rec_list[2 * i + 1];

        // Trivially part of the cycle
        if (target == end)
        {
            free(rec_list[0]);
            free(rec_list);
            return schedule[get_instr_id(end) - 1] - schedule[get_instr_id(start) - 1] + 1;
        }

        // For now, at least, allow for the start of the recurrence cycle to be re-scheduled, as the distance between start and end
        // will not change
        if (target == start)
            continue;

        path = searchPath(d, start, target);
        // There is a path from start to target
        if (path == 1)
        {
            path = searchPath(d, target, end);
            // There is also a path from target to end, therefore the target is part of this recurrence cycle
            if (path == 1)
            {
                free(rec_list[0]);
                free(rec_list);
                return schedule[get_instr_id(end) - 1] - schedule[get_instr_id(start) - 1] + 1;
            }
        }
    }
    free(rec_list[0]);
    free(rec_list);
    return 0;
}

/* ***************************************************************************************************************
 * reScheduleNode: Re-schedules the target node by a given distance
 *****************************************************************************************************************/
int reScheduleNode(int *scheduled, cgra *template, dfg *d, dfg_instr *target, int distance, int keepMaxLat, int II)
{

    int id = get_instr_id(target), *alap = rasALAP(template, d);
    // The actual mobility of each node: how much can they move down, compared to their current schedule?
    int *mobility = getNodeMobility(scheduled, alap, get_dfg_size(d));
    int k;

    int recurrenceCycle = isInRecurrenceCycle(d, target, scheduled);

    if (recurrenceCycle < II)
        recurrenceCycle = 0;
    else
        recurrenceCycle = 1;

    // Node can be re-scheduled only if it is not part of a recurrence cycle and either has the mobility for it or
    // the DFG is rescheduled to take longer
    if (!recurrenceCycle && (mobility[id - 1] >= distance || (!keepMaxLat)))
    {
        /* printf("node can be rescheduled!\n"); */

        bool *visited = (bool *)calloc(get_dfg_size(d), sizeof(bool));
        stack *s = createStack(get_dfg_size(d));
        push(s, (Item)target);

        while (!isEmpty(s))
        {
            dfg_instr *curr = (dfg_instr *)pop(s);
            id = get_instr_id(curr);
            // Reschedule this node
            scheduled[id - 1] += distance;
            visited[id - 1] = true;

            for (k = 0; k < get_n_outputs(curr); k++)
            {
                dfg_instr *o = get_output(curr, k);
                if (visited[get_instr_id(o) - 1] == false)
                    push(s, (Item)o);
            }
        }
        deleteStack(s);
        free(visited);
    }
    k = !recurrenceCycle && (mobility[id - 1] >= distance || (!keepMaxLat));

    free(alap);
    free(mobility);
    return k ? distance : 0;
}

int pipelineReschedule(int *scheduled, cgra *template, dfg *d, dfg_instr *target, int **placed, int distance, int II)
{

    int id = get_instr_id(target);
    int k, sd, m;

    int recurrenceCycle = isInRecurrenceCycle(d, target, scheduled);

    if (recurrenceCycle < II)
        recurrenceCycle = 0;
    else
        recurrenceCycle = 1;

    // Node can be re-scheduled only if it is not part of a recurrence cycle and either has the mobility for it or
    // the DFG is rescheduled to take longer
    if (!recurrenceCycle)
    {
        /* printf("node can be rescheduled!\n"); */

        bool *visited = (bool *)calloc(get_dfg_size(d), sizeof(bool));
        stack *s = createStack(get_dfg_size(d));
        push(s, (Item)target);
        sd = scheduled[id - 1];

        while (!isEmpty(s))
        {
            dfg_instr *curr = (dfg_instr *)pop(s);
            id = get_instr_id(curr);
            // Reschedule this node
            visited[id - 1] = true;

            m = 0;
            for (int i = 0; i < get_n_inputs(curr); i++)
            {
                if (m < scheduled[get_instr_id(get_input(curr, i)) - 1])
                    m = scheduled[get_instr_id(get_input(curr, i)) - 1];
            }
            if (scheduled[id - 1] <= m || curr == target)
            {
                scheduled[id - 1] += distance;
                if (curr != target)
                    placed[id - 1][4] += distance; // store the amount of cycles padded due to pipelining
                for (k = 0; k < get_n_outputs(curr); k++)
                {
                    dfg_instr *o = get_output(curr, k);
                    if (visited[get_instr_id(o) - 1] == false)
                        push(s, (Item)o);
                }
            }
        }
        deleteStack(s);
        free(visited);
        scheduled[get_instr_id(target) - 1] = sd;
    }
    k = !recurrenceCycle;

    return k ? distance : 0;
}

int invertPipelineReschedule(int *scheduled, cgra *template, dfg *d, dfg_instr *target, int **placed, int distance, int II)
{

    int id = get_instr_id(target);
    int k, sd, m, ed, ii, jj, p;

    int recurrenceCycle = isInRecurrenceCycle(d, target, scheduled);

    if (recurrenceCycle < II)
        recurrenceCycle = 0;
    else
        recurrenceCycle = 1;

    // Node can be re-scheduled only if it is not part of a recurrence cycle and either has the mobility for it or
    // the DFG is rescheduled to take longer
    if (!recurrenceCycle)
    {
        /* printf("node can be rescheduled!\n"); */

        bool *visited = (bool *)calloc(get_dfg_size(d), sizeof(bool));
        stack *s = createStack(get_dfg_size(d));
        push(s, (Item)target);
        sd = scheduled[id - 1];

        while (!isEmpty(s))
        {
            dfg_instr *curr = (dfg_instr *)pop(s);
            id = get_instr_id(curr);
            // Reschedule this node
            visited[id - 1] = true;

            m = 0;
            for (int i = 0; i < get_n_inputs(curr); i++)
            {
                int iid = get_instr_id(get_input(curr, i));
                if (placed[iid - 1][0] == 1)
                {
                    ii = placed[iid - 1][1] / get_cgra_C(template);
                    jj = placed[iid - 1][1] % get_cgra_C(template);
                    p = scheduled[iid - 1] + getPEPipelineStages(template, ii, jj) - 1;
                }
                else
                    p = scheduled[iid - 1];
                if (m < p)
                    m = p;
            }
            if (curr == target)
            {
                placed[id - 1][3] = placed[id - 1][2];
            }
            else if (scheduled[id - 1] > m + 1)
            {
                // ed = distance > scheduled[id - 1] - m ? scheduled[id - 1] - m : distance; // effective distance to rollback
                ed = scheduled[id - 1] - (m + 1);
                scheduled[id - 1] -= ed;
                placed[id - 1][4] -= ed; // store the amount of cycles unpadded from pipelining
            }
            else
                continue;

            for (k = 0; k < get_n_outputs(curr); k++)
            {
                dfg_instr *o = get_output(curr, k);
                if (visited[get_instr_id(o) - 1] == false)
                    push(s, (Item)o);
            }
        }
        deleteStack(s);
        free(visited);
        scheduled[get_instr_id(target) - 1] = sd;
    }
    k = !recurrenceCycle;

    return k ? distance : 0;
}

/* ***************************************************************************************************************
 * topologicalSortDFG
 * Inputs: the target DFG
 * Sorts the DFG topologically
 *****************************************************************************************************************/
void topologicalSortDFG(dfg *d)
{
    int i, j, ongoing, h, *height = malloc(get_dfg_size(d) * sizeof(int));
    dfg_instr **critical_nodes;
    sortItem *sc;

    // DFG is already sorted, nothing to do (also avoid potential infinite loops)
    if (is_dfg_sorted(d) == 1)
        return;

    for (i = 0; i < get_dfg_size(d); i++)
        height[i] = -1;

    // Compute the heights of all nodes in the DFG
    do
    {
        ongoing = 0;
        for (i = 0; i < get_dfg_size(d); i++)
        {
            if (get_n_outputs(get_dfg_instr(d, i)) == 0)
            {
                height[i] = 0;
            }
            else
            {
                h = 0;
                for (j = 0; j < get_n_outputs(get_dfg_instr(d, i)); j++)
                {
                    if (height[get_instr_id(get_output(get_dfg_instr(d, i), j)) - 1] == -1)
                    {
                        ongoing = 1;
                        break;
                    }
                    h = h > height[get_instr_id(get_output(get_dfg_instr(d, i), j)) - 1] ? h : height[get_instr_id(get_output(get_dfg_instr(d, i), j)) - 1];
                }
                if (j == get_n_outputs(get_dfg_instr(d, i)))
                {
                    height[i] = h + 1;
                }
            }
        }
    } while (ongoing);

    /* printf("Heights:\n");
    for (i = 0; i < get_dfg_size(d); i++){
        printf("Instruction %d (%s): %d\n", get_instr_id(get_dfg_instr(d, i)), get_instr_op(get_dfg_instr(d, i)), height[i]);
    } */

    /** Create a dfg composed of only the nodes on the critical path */
    critical_nodes = (dfg_instr **)malloc(get_dfg_size(d) * sizeof(dfg_instr *));
    sc = (sortItem *)malloc(get_dfg_size(d) * sizeof(sortItem));

    for (i = 0; i < get_dfg_size(d); i++)
    {
        sc[i].id = get_instr_id(get_dfg_instr(d, i));
        sc[i].height = height[i];
    }
    qsort(sc, get_dfg_size(d), sizeof(sortItem), compareTS);
    for (i = 0; i < get_dfg_size(d); i++)
    {
        critical_nodes[i] = get_dfg_instr(d, sc[i].id - 1);
        /* printf("Instruction %d: %d |%d\n", sc[i].id, sc[i].height, get_instr_id(critical_nodes[i])); */
    }

    /* printf("Sorted nodes:\n"); */
    for (i = 0; i < get_dfg_size(d); i++)
    {
        set_dfg_instr(d, critical_nodes[i], i);
        /* printf("Instruction %d (%s): %d |%d\n", get_instr_id(get_dfg_instr(d, i)), get_instr_op(get_dfg_instr(d, i)), height[i], get_instr_id(critical_nodes[i])); */
    }

    // Mark DFG as sorted
    set_dfg_sorted(d, 1);

    free(sc);
    free(height);
    free(critical_nodes);
}

/****************************************************************************************************************/