#ifndef __PQUEUE__H__
#define __PQUEUE__H__

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

// Function to create a new min-heap node
MinHeapNode* newMinHeapNode(int v, int dist);
void freeMinHeapNode(MinHeapNode* minHeapNode);
MinHeap* createMinHeap(int capacity);
int minHeapIsEmpty(MinHeap *minHeap);
void freeMinHeap(MinHeap* minHeap);
void swapMinHeapNode(MinHeapNode** a, MinHeapNode** b);
void minHeapify(MinHeap* minHeap, int idx);
MinHeapNode* extractMin(MinHeap* minHeap);
void decreaseKey(MinHeap* minHeap, int v, int dist);
int isInMinHeap(MinHeap* minHeap, int v);

int rootDistance(MinHeap* minHeap);
int comparePQ(const void *a, const void *b);
int dijkstra(int **graph, int** states, int V, int src, int dest);

#endif
