#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "Item.h"

#define SUBLIST_STREAM_IN 0
#define SUBLIST_STREAM_OUT 1
#define SUBLIST_OP 2

#define MAX_OP_NAME_LEN 20

typedef struct _dfg_instr
{
    int id;
    char *op;
    char name[MAX_OP_NAME_LEN];
    int lat;
    int const_val; // if this "dfg_instr" is a constant, const_val stores its value
    int n_inputs;
    int n_outputs;
    int n_recurrences;
    int n_consts;
    struct _dfg_instr **inputs;
    struct _dfg_instr **outputs;
    struct _dfg_instr **recurrences;
    struct _dfg_instr **consts; // constants associated with this instruction
    int *rec_distances;
    int *trnsf_lat; // transf latency from the inputs
} dfg_instr;

typedef struct _dfg
{
    dfg_instr **d;  // dfg instruction array
    dfg_instr **consts; // dfg constants array
    dfg_instr **backup_instr_arr; // auxiliary array (unsorted instruction array)
    int N;          // size of the dfg
    int NConsts; // number of constants (don't count as instructions for the dfg, hence the seperate array)
    int sorted; // auxiliary variable to check if the dfg was topologically sorted or not
} dfg;

/**
 * Creates an Instruction
 * Inputs: operation, latency, number of inputs and outputs
 */
dfg_instr *create_instr(char *name, char *op, int lat, int n_inputs, int n_outputs, int n_recurrences, int n_consts, int reset_id)
{

    dfg_instr *new = (dfg_instr *)malloc(sizeof(dfg_instr));

    static int id = 1;
    if (reset_id == 1)
        id = 1;
    new->id = id++;
    new->lat = lat;

    new->op = (char *)calloc((strlen(op) + 1), sizeof(char));
    strcpy(new->op, op);

    strncpy(new->name, name, MAX_OP_NAME_LEN - 1);
    new->name[MAX_OP_NAME_LEN - 1]= '\0';

    new->n_inputs = n_inputs;
    new->n_outputs = n_outputs;
    new->n_recurrences = n_recurrences;
    new->n_consts = n_consts;
    new->inputs = (dfg_instr **)calloc(n_inputs, sizeof(dfg_instr *));
    new->outputs = (dfg_instr **)calloc(n_outputs, sizeof(dfg_instr *));
    new->recurrences = (dfg_instr **)calloc(n_recurrences, sizeof(dfg_instr *));
    new->rec_distances = (int *)calloc(n_recurrences, sizeof(int));
    new->consts = (dfg_instr **)calloc(n_consts, sizeof(dfg_instr *));

    new->trnsf_lat = (int*)calloc(n_inputs, sizeof(int));

    return new;
}

dfg_instr *copy_instr(dfg_instr *target)
{

    dfg_instr *copy = create_instr(target->name, target->op, target->lat, target->n_inputs, target->n_outputs, target->n_recurrences, target->n_consts, 0);

    int i;

    copy->id = target->id;

    for (i = 0; i < target->n_inputs; i++)
    {
        copy->inputs[i] = target->inputs[i];
    }
    for (i = 0; i < target->n_outputs; i++)
    {
        copy->outputs[i] = target->outputs[i];
    }
    for (i = 0; i < target->n_recurrences; i++){
        copy->recurrences[i] = target->recurrences[i];
        copy->rec_distances[i] = target->rec_distances[i];
    }
    for (i = 0; i < target->n_consts; i++){
        copy->consts[i] = target->consts[i];
    }

    return copy;
}

/**
 * sets a dependence to an input, dep
 */
int set_input(dfg_instr *target, dfg_instr *dep, int idx)
{

    if (idx < target->n_inputs)
    {
        target->inputs[idx] = dep;
        return 1;
    }
    return 0;
}

/**
 * sets a dependence to an output, dep
 */
int set_output(dfg_instr *target, dfg_instr *dep, int idx)
{

    if (idx < target->n_outputs)
    {
        target->outputs[idx] = dep;
        return 1;
    }
    return 0;
}

int remove_input(dfg_instr *target, int idx)
{

    int i;

    if (target->n_inputs <= 0)
        return 0;

    target->n_inputs--;

    dfg_instr **new_inputs = (dfg_instr **)malloc(target->n_inputs * sizeof(dfg_instr *));
    int *new_trnsf_lat = (int*)calloc(target->n_inputs, sizeof(int));

    if (new_inputs == NULL)
        return 0;

    for (i = 0; i < idx; i++)
    {
        new_inputs[i] = target->inputs[i];
        new_trnsf_lat[i] = target->trnsf_lat[i];
    }
    for (i = idx; i < target->n_inputs; i++)
    {
        new_inputs[i] = target->inputs[i + 1];
        new_trnsf_lat[i] = target->trnsf_lat[i + 1];
    }

    if (target->inputs != NULL){
        free(target->inputs);
        free(target->trnsf_lat);
    }
    
    target->inputs = new_inputs;
    target->trnsf_lat = new_trnsf_lat;

    return 1;
}

int remove_output(dfg_instr *target, int idx)
{

    int i;

    if (target->n_outputs <= 0)
        return 0;

    target->n_outputs--;

    dfg_instr **new_outputs = (dfg_instr **)malloc(target->n_outputs * sizeof(dfg_instr *));
    if (new_outputs == NULL)
        return 0;

    for (i = 0; i < idx; i++)
    {
        new_outputs[i] = target->outputs[i];
    }
    for (i = idx; i < target->n_outputs; i++)
    {
        new_outputs[i] = target->outputs[i + 1];
    }

    if (target->outputs != NULL)
        free(target->outputs);
    
    target->outputs = new_outputs;

    return 1;
}

int set_recurrence(dfg_instr *target, dfg_instr *rec, int idx, int dist)
{
    int i;
    for (i = 0; i < target->n_recurrences; i++){
        if (target->recurrences[i] == NULL)
        {
            target->recurrences[i] = rec;
            target->rec_distances[i] = dist;
            return 1;
        }
    }
    return 0;
}

int set_const(dfg_instr *target, dfg_instr *cnst, int idx){
    if (idx >= 0 && idx < target->n_consts)
    {
        target->consts[idx] = cnst;
        return 1;
    }
    return 0;
}

void set_const_val(dfg_instr *cnst, int val)
{
    cnst->const_val = val;
}

int get_const_val(dfg_instr *cnst)
{
    return cnst->const_val;
}

dfg_instr* get_recurrence(dfg_instr *target, int idx)
{
    return target->recurrences[idx];
}

dfg_instr *get_const(dfg_instr *target, int idx){
    return target->consts[idx];
}

int get_const_id(dfg_instr *target, int idx){
    return target->consts[idx]->id;
}

int get_n_consts(dfg_instr *target){
    return target->n_consts;
}

int get_n_recurrences(dfg_instr *target)
{
    return target->n_recurrences;
}

int get_rec_dist(dfg_instr *target, int idx)
{
    return target->rec_distances[idx];
}

int get_rec_dist_from_instr(dfg_instr *target, dfg_instr *rec){
    for (int i = 0; i < target->n_recurrences; i++){
        if (target->recurrences[i] == rec)
            return target->rec_distances[i];
    }
    return -1;
}

int isIO(dfg_instr *target){
    return (strncmp(target->op, "STREAM_IN", strlen(target->op)) == 0 || strncmp(target->op, "STREAM_OUT",strlen(target->op)) == 0);
}

int get_instr_id(dfg_instr *t)
{
    return t->id;
}

char *get_instr_op(dfg_instr *t)
{
    return t->op;
}

char *get_instr_name(dfg_instr *t)
{
    return t->name;
}

int get_instr_lat(dfg_instr *t)
{
    return t->lat;
}

int get_input_trnsf_lat(dfg_instr *t, int i){
    return t->trnsf_lat[i];
}

void set_input_trnsf_lat(dfg_instr *t, int i, int lat){
    t->trnsf_lat[i] = lat;
}

/**
 * Returns target's parameter: n_inputs (number of inputs)
 */
int get_n_inputs(dfg_instr *target)
{
    return target->n_inputs;
}

/**
 * Returns target's parameter: n_inputs (number of inputs)
 */
int get_n_outputs(dfg_instr *target)
{
    return target->n_outputs;
}

dfg_instr *get_input(dfg_instr *target, int idx)
{
    return target->inputs[idx];
}

int get_input_idx(dfg_instr *target, dfg_instr *input)
{
    int i;
    for (i = 0; i < target->n_inputs; i++)
        if (target->inputs[i] == input)
            return i;
    return -1;
}

dfg_instr *get_input_by_op_id(dfg_instr *target, int id){
    int i;
    for (i = 0; i < target->n_inputs; i++)
        if (target->inputs[i]->id == id)
            return target->inputs[i];
    return NULL;    
}

dfg_instr *get_output(dfg_instr *target, int idx)
{
    return target->outputs[idx];
}

int get_input_id(dfg_instr *target, int idx)
{
    return target->inputs[idx]->id;
}

int get_output_id(dfg_instr *target, int idx)
{
    return target->outputs[idx]->id;
}

int get_dfg_size(dfg *d)
{
    return d->N;
}

int get_dfg_n_consts(dfg *d)
{
    return d->NConsts;
}

dfg_instr *get_instr_by_op_id(dfg *d, int id){
    int i;
    for (i = 0; i < d->N; i++)
    {
        if (d->d[i]->id == id)
            return d->d[i];
    }
    for (i = 0; i < d->NConsts; i++)
    {
        if (d->consts[i]->id == id)
            return d->consts[i];
    }
    return NULL;
}

dfg_instr *get_dfg_instr(dfg *d, int idx)
{
    if (idx >= 0 && idx < d->N)
        return d->d[idx];
    return NULL;
}

dfg_instr *get_dfg_const(dfg *d, int idx){
    if (idx >= 0 && idx < d->NConsts)
        return d->consts[idx];
    return NULL;
}

int get_dfg_instr_id(dfg *d, int idx)
{
    if (idx >= 0 && idx < d->N)
        return d->d[idx]->id;
    return -1;
}

int get_dfg_const_id(dfg *d, int idx)
{
    if (idx >= 0 && idx < d->NConsts)
        return d->consts[idx]->id;
    return -1;
}

char *get_dfg_instr_op(dfg *d, int idx)
{
    return d->d[idx]->op;
}

void display_dfg(dfg *d)
{

    int i, j;
    for (i = 0; i < d->N; i++)
    {
        printf("Instruction %d:\n", d->d[i]->id);
        printf("\tOP: ");
        if (!strcmp(d->d[i]->op, "LOAD"))
            printf("\033[0;32m");
        else if (!strcmp(d->d[i]->op, "STORE"))
            printf("\033[0;35m");
        else if (!strncmp(d->d[i]->op, "STREAM",6))
            printf("\033[1;33m");
        else
            printf("\033[0;36m");

        printf("%s\n\t\033[0;0m", d->d[i]->op);
        printf("Latency: %d\n\tInputs (%d): ", d->d[i]->lat, d->d[i]->n_inputs);
        for (j = 0; j < d->d[i]->n_inputs; j++)
            printf("%d ", d->d[i]->inputs[j]->id);
        printf("\n\tOuputs (%d): ", d->d[i]->n_outputs);
        for (j = 0; j < d->d[i]->n_outputs; j++)
            printf("%d ", d->d[i]->outputs[j]->id);
        printf("\n\n");
    }
}

/**
 * Deletes an Instruction (frees all dynamically allocated memory for it)
 */
void delete_instr(dfg_instr *i)
{
    free(i->inputs);
    free(i->outputs);
    free(i->trnsf_lat);
    free(i->recurrences);
    free(i->rec_distances);
    free(i->op);
    free(i->consts);
    free(i);
}

/**
 * Creates a DFG from an array of instructions
 */
dfg *create_dfg(dfg_instr **d, int N, dfg_instr **c, int NConsts)
{
    dfg *new = (dfg *)malloc(sizeof(dfg));
    int i;

    new->d = d;
    new->N = N;
    new->consts = c;
    new->NConsts = NConsts;
    new->sorted = 0; // initially not sorted

    new->backup_instr_arr = (dfg_instr**)malloc(new->N * sizeof(dfg_instr*));
    for (i = 0; i < new->N; i++)
        new->backup_instr_arr[i] = new->d[i];

    return new;
}

void restore_dfg(dfg *d){
    
    int i;
    
    if (d->sorted == 0)
        return;
    
    for (i = 0; i < d->N; i++)
        d->d[i] = d->backup_instr_arr[i];
    d->sorted = 0;
}

dfg *copy_dfg(dfg *target)
{

    dfg *copy = create_dfg(NULL, target->N, NULL, target->NConsts);
    int i, j;

    dfg_instr **d = (dfg_instr **)malloc(target->N * sizeof(dfg_instr *));
    dfg_instr **bckup = (dfg_instr **)malloc(target->N * sizeof(dfg_instr *));
    dfg_instr **c = (dfg_instr **)malloc(target->NConsts * sizeof(dfg_instr *));

    for (i = 0; i < target->N; i++)
    {
        d[i] = copy_instr(target->d[i]);
        bckup[i] = d[i];
    }
    
    for (i = 0; i < target->N; i++)
    {
        for (j = 0; j < d[i]->n_inputs; j++){
            d[i]->inputs[j] = d[d[i]->inputs[j]->id - 1];
        }
        for (j = 0; j < d[i]->n_outputs; j++){
            d[i]->outputs[j] = d[d[i]->outputs[j]->id - 1];
        }
    }

    for (i = 0; i < target->NConsts; i++)
    {
        c[i] = copy_instr(target->consts[i]);
    }
    
    for (i = 0; i < target->NConsts; i++)
    {
        for (j = 0; j < c[i]->n_inputs; j++){
            c[i]->inputs[j] = c[c[i]->inputs[j]->id - 1];
        }
        for (j = 0; j < c[i]->n_outputs; j++){
            c[i]->outputs[j] = c[c[i]->outputs[j]->id - 1];
        }
    }

    copy->d = d;
    copy->consts = c;

    copy->sorted = target->sorted;

    return copy;
}

void set_dfg_sorted(dfg *d, int sorted)
{
    if (d != NULL)
        d->sorted = sorted;
}

int is_dfg_sorted(dfg *d)
{
    if (d != NULL)
        return d->sorted;
    return 0;
}

/**
 * Set target instr to index idx in the dfg
 */
int set_dfg_instr(dfg *d, dfg_instr* target, int idx)
{
    if (idx >= 0 && idx < d->N)
    {
        d->d[idx] = target;
        return 1;
    }
    return 0;
}



int get_n_instrs_by_op(dfg *d, char *op){

    int i, cnt = 0;

    for (i = 0; i < d->N; i++)
        if (!strcmp(d->d[i]->op, op))
            cnt++;
    return cnt;

}

int search_instr_lat(dfg_instr *curr, dfg_instr **path, int *sz)
{

    int j;
    int lat = get_instr_lat(curr);
    dfg_instr *next;
    int submax = -1;
    int subsz = *sz;

    for (j = 0; j < get_n_outputs(curr); j++)
    {
        next = get_output(curr, j);
        path[subsz++] = next;

        lat = get_instr_lat(curr) + search_instr_lat(next, path, &subsz);

        if (lat > submax)
        {
            submax = lat;
            *sz = subsz;
        }
        subsz--;
    }
    if (lat > submax)
        submax = lat;

    return submax;
}

void remove_instr_from_dfg(dfg *d, int idx)
{
    int i;

    if (d->N <= 0)
        return;

    d->N--;

    dfg_instr **new_d = (dfg_instr **)malloc(d->N * sizeof(dfg_instr *));

    for (i = 0; i < idx; i++)
    {
        new_d[i] = d->d[i];
    }
    for (i = idx; i < d->N; i++)
    {
        new_d[i] = d->d[i + 1];
    }

    if (d->d != NULL)
        free(d->d);

    d->d = new_d;
}

int getHighestInstrLat(dfg *d)
{
    register int i, lat = -1;
    for (i = 0; i < d->N; i++)
    {
        if (lat < d->d[i]->lat)
            lat = d->d[i]->lat;
    }
    return lat;
}

int getLowestInstrLat(dfg *d)
{
    register int i, lat = __INT_MAX__;
    for (i = 0; i < d->N; i++)
    {
        if (lat > d->d[i]->lat)
            lat = d->d[i]->lat;
    }
    return lat;
}

int getSerialExecLat(dfg *d)
{
    register int i, lat = 0;
    for (i = 0; i < d->N; i++)
    {
        lat += d->d[i]->lat;
    }
    return lat;
}

int get_node_sublist_size(dfg_instr **ls){

    int sz = 0;
    while (ls[sz] != NULL){
        sz++;
    }
    return sz;
}

int *getInputRecArray(dfg *d, dfg_instr *target)
{
    int N = get_dfg_size(d), n, recCount = 0, r, id, rid;
    int *recArr = (int *)malloc(sizeof(int) * (N + 1));
    dfg_instr *rec;

    id = get_instr_id(target);

    for (n = 0; n < N; n++)
    {
        rec = get_dfg_instr(d, n);
        rid = get_instr_id(rec);
        for (r = 0; r < get_n_recurrences(rec); r++)
        {
            if (get_instr_id(get_recurrence(rec, r)) == id)
            {
                recArr[++recCount] = rid;
                break;
            }
        }
    }
    recArr[0] = recCount;
    return recArr;
}

/**
 * Returns a sublist of nodes from the DFG, either being the inputs, outputs or the instructions
 */
dfg_instr **get_node_sublist(dfg *d, int type)
{

    int i, idx = 0, N = get_dfg_size(d);
    dfg_instr **sublist = (dfg_instr **)calloc((N+1), sizeof(dfg_instr *)); // last element will always be null

    for (i = 0; i < N; i++)
    {
        if (type == SUBLIST_STREAM_IN && !strcmp(get_dfg_instr_op(d, i), "STREAM_IN"))
        {
            sublist[idx++] = get_dfg_instr(d, i);
        }
        else if (type == SUBLIST_STREAM_OUT && !strcmp(get_dfg_instr_op(d, i), "STREAM_OUT"))
        {
            sublist[idx++] = get_dfg_instr(d, i);
        }
        else if (type == SUBLIST_OP && strcmp(get_dfg_instr_op(d, i), "STREAM_IN") != 0 && strcmp(get_dfg_instr_op(d, i), "STREAM_OUT") != 0)
        {
            sublist[idx++] = get_dfg_instr(d, i);
        }
    }

    return sublist;
}

dfg_instr **get_dfg_inputs(dfg *d)
{
    return get_node_sublist(d, SUBLIST_STREAM_IN);
}

dfg_instr **get_dfg_outputs(dfg *d)
{
    return get_node_sublist(d, SUBLIST_STREAM_OUT);
}

dfg_instr **get_dfg_ops(dfg *d)
{
    return get_node_sublist(d, SUBLIST_OP);
}

dfg_instr **merge_sublists(dfg_instr **l1, dfg_instr **l2){
    int sz1 = get_node_sublist_size(l1), sz2 = get_node_sublist_size(l2);

    dfg_instr **merged = (dfg_instr**)calloc((sz1 + sz2 + 1), sizeof(dfg_instr*));

    for (int i = 0; i < sz1; i++){
        merged[i] = l1[i];
    }
    for (int i = 0; i < sz2; i++){
        merged[sz1 + i] = l2[i];
    }
    free(l1);
    free(l2);
    return merged;
}


Item *get_all_recurrences(dfg *d){
    int *n_recurrences = (int*)calloc(1, sizeof(int)), n = 1;
    Item *rec_list;

    for (int i = 0; i < d->N; i++){
            *n_recurrences += d->d[i]->n_recurrences;
    }

    if (*n_recurrences == 0){
        free(n_recurrences);
        return NULL;
    }

    rec_list = (Item*)calloc(*n_recurrences * 2 + 1, sizeof(Item));
    rec_list[0] = (Item)n_recurrences;

    for (int i = 0; i < d->N; i++){
        dfg_instr *curr = d->d[i];
        for (int j = 0; j < curr->n_recurrences; j++){
            rec_list[n++] = (Item)curr;
            rec_list[n++] = (Item)curr->recurrences[j];
        }
    }   
    return rec_list;
}

int *get_associated_nodes(dfg *d){

    int i, k, id, iid, *associated_tree = (int*)calloc(d->N, sizeof(int));
    dfg_instr *curr, *iin;

    for (i = 0; i < d->N; i++){
        curr = d->d[i];
        id = curr->id;
        if (associated_tree[id - 1] == 0)
            associated_tree[id - 1] = id;
        
        if (curr->n_outputs > 1)
            continue;

        for (k = 0; k < curr->n_inputs; k++){
            iin = curr->inputs[k];
            iid = iin->id;
            if (iin->n_inputs < 2 && iin->n_outputs < 2)
                associated_tree[iid - 1] = id;
        }
    }
    
    for (i = 0; i < d->N; i++){
        printf("[%d]: %d\n",i+1,associated_tree[i]);
    }
    return associated_tree;
}

/**
 * Deletes a DFG
 */
void delete_dfg(dfg *d, int delete_instrs)
{
    int i;
    /* int *a = get_associated_nodes(d);
    free(a); */
    if (!d)
        return;
    
    if (delete_instrs != 0)
    {
        for (i = 0; i < d->N; i++)
        {
            if (d->d[i])
                delete_instr(d->d[i]);
        }
        for (i = 0; i < d->NConsts; i++)
        {
            if (d->consts[i])
                delete_instr(d->consts[i]);
        }
    }
    free(d->d);
    free(d->consts);
    free(d->backup_instr_arr);
    free(d);
}
