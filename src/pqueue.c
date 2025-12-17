#include <stdio.h>
#include <stdlib.h>
#include <limits.h>

#define INF __INT_MAX__

// Structure to represent a node in the priority queue
typedef struct {
    int vertex;
    int distance;
} MinHeapNode;

// Structure to represent a min-heap
typedef struct {
    int size;
    int capacity;
    int *pos;             // To store position of vertex in heap
    MinHeapNode **array;  // Array of pointers to heap nodes
} MinHeap;

// Compar function for a qsort
int comparePQ(const void *a, const void *b) {
    MinHeapNode *na = *(MinHeapNode **)a;
    MinHeapNode *nb = *(MinHeapNode **)b;
    return na->distance - nb->distance;
}

// Function to create a new min-heap node
MinHeapNode* newMinHeapNode(int v, int dist) {
    MinHeapNode* minHeapNode = (MinHeapNode*) malloc(sizeof(MinHeapNode));
    minHeapNode->vertex = v;
    minHeapNode->distance = dist;
    return minHeapNode;
}

void freeMinHeapNode(MinHeapNode* minHeapNode){
    free(minHeapNode);
}

int minHeapIsEmpty(MinHeap *minHeap){
    return minHeap->size <= 0;
}

// Function to create a min-heap
MinHeap* createMinHeap(int capacity) {
    MinHeap* minHeap = (MinHeap*) malloc(sizeof(MinHeap));
    minHeap->pos = (int*) malloc(capacity * sizeof(int));
    minHeap->size = 0;
    minHeap->capacity = capacity;
    minHeap->array = (MinHeapNode**) malloc(capacity * sizeof(MinHeapNode*));
    return minHeap;
}

// Function to free memory of the min-heap
void freeMinHeap(MinHeap* minHeap) {
    // Free all the individual heap nodes
    for (int i = 0; i < minHeap->size; i++) {
        if (minHeap->array[i] != NULL) {
            free(minHeap->array[i]);
        }
    }

    // Free the heap arrays and the heap structure
    free(minHeap->array);
    free(minHeap->pos);
    free(minHeap);
}

// Function to swap two nodes of the heap
void swapMinHeapNode(MinHeapNode** a, MinHeapNode** b) {
    MinHeapNode* temp = *a;
    *a = *b;
    *b = temp;
}

// Heapify a given node
void minHeapify(MinHeap* minHeap, int idx) {
    int smallest, left, right;
    smallest = idx;
    left = 2 * idx + 1;
    right = 2 * idx + 2;

    if (left < minHeap->size && minHeap->array[left]->distance < minHeap->array[smallest]->distance)
        smallest = left;

    if (right < minHeap->size && minHeap->array[right]->distance < minHeap->array[smallest]->distance)
        smallest = right;

    if (smallest != idx) {
        // Swap positions
        MinHeapNode* smallestNode = minHeap->array[smallest];
        MinHeapNode* idxNode = minHeap->array[idx];

        // Swap positions
        minHeap->pos[smallestNode->vertex] = idx;
        minHeap->pos[idxNode->vertex] = smallest;

        // Swap nodes
        swapMinHeapNode(&minHeap->array[smallest], &minHeap->array[idx]);

        // Recursively heapify the affected subtree
        minHeapify(minHeap, smallest);
    }
}

int rootDistance(MinHeap* minHeap) {
    return minHeap->array[0]->distance;
}

// Extract the vertex with the minimum distance value from the heap
MinHeapNode* extractMin(MinHeap* minHeap) {
    if (minHeap->size == 0)
        return NULL;

    // Store the root node
    MinHeapNode* root = minHeap->array[0];

    // Replace root node with the last node
    MinHeapNode* lastNode = minHeap->array[minHeap->size - 1];
    minHeap->array[0] = lastNode;

    // Update position of the last node
    minHeap->pos[root->vertex] = minHeap->size - 1;
    minHeap->pos[lastNode->vertex] = 0;

    // Reduce heap size and heapify the root
    minHeap->size--;
    minHeapify(minHeap, 0);

    return root;
}

// Decrease distance value of a given vertex v
void decreaseKey(MinHeap* minHeap, int v, int dist) {
    // Get the index of v in the heap
    int i = minHeap->pos[v];

    // Update the distance value
    minHeap->array[i]->distance = dist;

    // Travel up while the heap property is violated
    while (i && minHeap->array[i]->distance < minHeap->array[(i - 1) / 2]->distance) {
        // Swap the node with its parent
        minHeap->pos[minHeap->array[i]->vertex] = (i - 1) / 2;
        minHeap->pos[minHeap->array[(i - 1) / 2]->vertex] = i;
        swapMinHeapNode(&minHeap->array[i], &minHeap->array[(i - 1) / 2]);

        // Move to the parent index
        i = (i - 1) / 2;
    }
}

// Check if a given vertex is in the min-heap
int isInMinHeap(MinHeap* minHeap, int v) {
    return minHeap->pos[v] < minHeap->size;
}

// Dijkstra's algorithm using a priority queue (min-heap)
int dijkstra(int **graph, int **states, int V, int src, int dest) {

    int dist[V]; // dist[i] will hold the shortest distance from src to i
    int pred[V]; // pred[i] will hold the the previous node

    // Create a min heap
    MinHeap* minHeap = createMinHeap(V);

    // Initialize min heap with all vertices and distances as infinite
    for (int v = 0; v < V; v++) {
        dist[v] = INF;
        pred[v] = -1;
        if (v == src)
            continue;
        minHeap->array[v] = newMinHeapNode(v, dist[v]);
        minHeap->pos[v] = v;
    }

    // Make distance of the source vertex 0
    minHeap->array[src] = newMinHeapNode(src, 0);
    minHeap->pos[src] = src;
    dist[src] = 0;
    decreaseKey(minHeap, src, 0);

    // Initially, the size of the heap is equal to the number of vertices
    minHeap->size = V;

    // Loop until the min heap is empty
    while (minHeap->size != 0) {
        // Extract the vertex with the minimum distance value
        MinHeapNode* minHeapNode = extractMin(minHeap);
        int u = minHeapNode->vertex;

        if (u == dest){
            freeMinHeapNode(minHeapNode);
            break;
        }

        // Traverse through all adjacent vertices of u
        for (int v = 0; v < V; v++) {
            // Update dist[v] only if it's not in the min-heap, there is an edge from u to v,
            // and the total weight of the path from src to v through u is smaller than the current value of dist[v]
            if (graph[u][v] == INF || states[u][v] >= 1)
                continue;

            if (isInMinHeap(minHeap, v) && dist[u] != INF && dist[u] + graph[u][v] < dist[v]) {
                dist[v] = dist[u] + graph[u][v];
                decreaseKey(minHeap, v, dist[v]);
                pred[v] = u;
            }
        }
        free(minHeapNode);
    }
    
    int i = dest;
    while(i != -1){        
        if (pred[i] != -1){
            // set states as "to be used, but not yet commited" (state 2)
            states[i][pred[i]] = 2;
            states[pred[i]][i] = 2;
        }
        i = pred[i];

    }
    //printf("Vertex \t Distance from Source to Dest\n");
    //    printf("[%d] -> [%d] = %d\n", src, dest, dist[dest]);

    freeMinHeap(minHeap);    
    return dist[dest]; // 1 if a path was found, 0 otherwise
}
