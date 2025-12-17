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

typedef struct a_star_node
{
    int i;
    int j;
} astr_node;

int spatial_heuristic(int i1, int j1, int i2, int j2)
{
    return abs(i1 - i2) + abs(j1 - j2);
}

/***************
 * Route a target instruction to one of its inputs
 * Routing is done using the A star algorithm
 * Cost function: manhattan distance between the PE and the PE with the mapped input
 */
int __spatial__routeToInput(cgra *fs, dfg_instr *target, int i1, int j1, dfg_instr *input, int i2, int j2)
{

    int V = get_cgra_L(fs) * get_cgra_C(fs), src = i1 * get_cgra_C(fs) + j1, dest = i2 * get_cgra_C(fs) + j2, f_score, g_score, v;
    int id = get_instr_id(target), iid = get_instr_id(input), i_prev, j_prev, i, j, *neighbours;
    int dist[V]; // g-score (actual cost from start)
    int pred[V]; // Predecessor array for path reconstruction

    MinHeap *minHeap = createMinHeap(V);

    f_score = spatial_heuristic(i1, j1, i2, j2); // f score for the source node: f = g + h, g = 0
    // Initialize all nodes with infinite distance and add to heap
    for (int v = 0; v < V; v++)
    {
        dist[v] = v == src ? 0 : INFINITY;
        pred[v] = -1;
        minHeap->array[v] = newMinHeapNode(v, v == src ? f_score : INFINITY);
        minHeap->pos[v] = v;
    }
    decreaseKey(minHeap, src, f_score); // Update the distance of the source in the heap
    minHeap->size = V;

    int retraced_existing_path = 0;

    while (!minHeapIsEmpty(minHeap))
    {
        // No more nodes to explore
        if (rootDistance(minHeap) == INFINITY)
        {
            break;
        }

        MinHeapNode *minHeapNode = extractMin(minHeap);
        int u = minHeapNode->vertex;

        if (u == dest || retraced_existing_path > 0)
        {
            freeMinHeapNode(minHeapNode);
            break;
        }

        i_prev = u / get_cgra_C(fs);
        j_prev = u % get_cgra_C(fs);

        neighbours = getPENeighbours(fs, i_prev, j_prev);

        for (int k = 1; k <= neighbours[0]; k++)
        {

            v = neighbours[k];
            i = v / get_cgra_C(fs);
            j = v % get_cgra_C(fs);

            if (connInUse(fs, i_prev, j_prev, i, j) || get_pe_power_mode(fs, i, j) == POWER_OFF)
                continue;
            
            if (!hasFreeOutputRegister(fs, i, j) && getOutputRegister(fs, i, j, 0) != iid)
                continue;

            g_score = dist[u] + get_ic_cost(fs, i_prev, j_prev, i, j); // g = cost from start to current node

            // Traced an already existing path
            if (getOutputRegister(fs, i, j, 0) == iid)
            {
                decreaseKey(minHeap, v, 0);
                pred[v] = u; 
                retraced_existing_path = v + 1;
                break;
            }

            if (g_score < dist[v])
            {
                dist[v] = g_score;
                pred[v] = u;                                         // Update predecessor
                f_score = g_score + spatial_heuristic(i, j, i2, j2); // f = g + h

                // Update priority queue
                if (isInMinHeap(minHeap, v))
                {
                    decreaseKey(minHeap, v, f_score);
                }
            }
        }
        free(neighbours);
        freeMinHeapNode(minHeapNode);
    }

    if (pred[dest] == -1 && retraced_existing_path == 0)
    {
        freeMinHeap(minHeap);
        return 0; // No path found
    }

    // Reconstruct the path from the predecessor array
    i = dest;
    if (retraced_existing_path > 0)
        i = retraced_existing_path - 1;

    while (i != -1 && pred[i] != -1)
    {
        add_conn_state(fs, pred[i], i, id);
        markOutputRegister(fs, i / get_cgra_C(fs), i % get_cgra_C(fs), 0, iid, 0);
        i = pred[i];
    }
    freeMinHeap(minHeap);
    return 1;
}

/*******
 * Route the target instruction to all of its inputs + recurrences
 */
int __spatial__routeOp(cgra *fs, dfg_instr *target, int **placed, int *schedule, int II)
{
    int k, id = get_instr_id(target), i = placed[id-1][1] / get_cgra_C(fs), j = placed[id-1][1] % get_cgra_C(fs), iid, i_in, j_in;

    for (k = 0; k < get_n_inputs(target); k++){
        iid = get_instr_id(get_input(target, k));
        i_in = placed[iid-1][1] / get_cgra_C(fs);
        j_in = placed[iid-1][1] % get_cgra_C(fs);
        if (!__spatial__routeToInput(fs, target, i, j, get_input(target, k), i_in, j_in))
            return 0;
    }
    return 1;
}