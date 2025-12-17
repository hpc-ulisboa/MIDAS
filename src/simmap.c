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

void __simmap__placeAndRouteNode(cgra *fs, dfg *d, dfg_instr *target, int **placed, int *schedule)
{

    int II = get_n_cgra_slices(fs);
    int minDist, **placementMatrix;
    int i, j, mapped = 0, num_positions = 0, sz = get_cgra_L(fs) * get_cgra_C(fs), priority, candidate_i, candidate_j, status;

    do
    {
        placementMatrix = generatePlacementMatrix(fs, target, placed, schedule, II, &minDist);

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

        while (!mapped && num_positions > 0)
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
                break;
            }
        }

        if (mapped)
            break;
        
        // Could not map target to this slice: try mapping it to the next slice
        reScheduleNode(schedule, fs, d, target, 1, 0, II);

    } while (!mapped);
}