#ifndef STACK_H
#define STACK_H

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include "Item.h"

typedef struct _stack stack;

stack* createStack(unsigned capacity);
void deleteStack(stack* s);
unsigned getStackSize(stack* s);
Item* isFull(stack*);
Item* isEmpty(stack*);
void push(stack*, Item*);
Item* pop(stack*);
Item* peek(stack*);

#endif