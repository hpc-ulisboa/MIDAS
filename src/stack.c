#include <stdlib.h>
#include <limits.h>
#include "Item.h"

typedef struct _stack {
    int top;
    unsigned capacity;
    Item* array;
} stack;

// function to create a stack of given capacity. It initializes size of
// stack as 0
stack* createStack(unsigned capacity)
{
    stack* s = (stack*)malloc(sizeof(stack));
    s->capacity = capacity;
    s->top = -1;
    s->array = (Item*)malloc(s->capacity * sizeof(Item));
    return s;
}

void deleteStack(stack* s)
{
    if (s) {
        free(s->array);
        free(s);
    }
}
 
unsigned getStackSize(stack* s)
{
    return s->capacity;
}

// Stack is full when top is equal to the last index
int isFull(stack* s)
{
    return s->top == s->capacity - 1;
}
 
// Stack is empty when top is equal to -1
int isEmpty(stack* s)
{
    return s->top == -1;
}
 
// Function to add an item to stack.  It increases top by 1
void push(stack* s, Item item)
{
    if (isFull(s))
        return;
    s->array[++s->top] = item;
    //printf("%d pushed to stack\n", item);
}
 
// Function to remove an item from stack.  It decreases top by 1
Item* pop(stack* s)
{
    if (isEmpty(s))
        return NULL;
    return s->array[s->top--];
}
 
// Function to return the top from stack without removing it
Item* peek(stack* s)
{
    if (isEmpty(s))
        return NULL;
    return s->array[s->top];
}