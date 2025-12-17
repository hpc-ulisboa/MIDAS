#ifndef DFG_H
#define DFG_H

#include "Item.h"

#define MAX_OP_NAME_LEN 20

typedef struct _dfg_instr dfg_instr;
typedef struct _dfg dfg;

// DFG Instruction
dfg_instr* create_instr(char *name, char* op, int lat, int n_inputs, int n_outputs, int n_recurrences, int n_consts, int reset_id);
dfg_instr *copy_instr(dfg_instr* target);
void set_input(dfg_instr* target, dfg_instr* dep, int idx);
void set_output(dfg_instr* target, dfg_instr* dep, int idx);
int remove_input(dfg_instr *target, int idx);
int remove_output(dfg_instr *target, int idx);
int get_n_inputs(dfg_instr* target);
int get_input_idx(dfg_instr *target, dfg_instr *input);
int get_n_outputs(dfg_instr* target);
int get_n_recurrences(dfg_instr *target);
dfg_instr* get_input(dfg_instr* target, int idx);
dfg_instr* get_output(dfg_instr* target, int idx);
int set_recurrence(dfg_instr *target, dfg_instr *rec, int idx, int dist);
int set_const(dfg_instr *target, dfg_instr *cnst, int idx);
void set_const_val(dfg_instr *cnst, int val);
int get_const_val(dfg_instr *cnst);
dfg_instr* get_recurrence(dfg_instr *target, int idx);
dfg_instr *get_const(dfg_instr *target, int idx);
int get_const_id(dfg_instr *target, int idx);
int get_n_consts(dfg_instr *target);
int get_rec_dist(dfg_instr *target, int idx);
int get_instr_id(dfg_instr* t);
char *get_instr_name(dfg_instr *t);
char* get_instr_op(dfg_instr* t);
int get_instr_lat(dfg_instr* t);
int get_input_trnsf_lat(dfg_instr *t, int i);
void set_input_trnsf_lat(dfg_instr *t, int i, int lat);
int get_input_id(dfg_instr* target, int idx);
int get_output_id(dfg_instr* target, int idx);
int get_dfg_size(dfg* d);
int get_dfg_n_consts(dfg *d);
dfg_instr* get_dfg_instr(dfg* d, int idx);
dfg_instr *get_dfg_const(dfg *d, int idx);
int get_rec_dist_from_instr(dfg_instr *target, dfg_instr *rec);
int get_dfg_instr_id(dfg* d, int idx);
int get_dfg_const_id(dfg *d, int idx);
dfg_instr *get_input_by_op_id(dfg_instr *target, int id);
int isIO(dfg_instr *target);
dfg_instr *get_instr_by_op_id(dfg *d, int id);
char* get_dfg_instr_op(dfg* d, int idx);
int *getInputRecArray(dfg *d, dfg_instr *target);
Item *get_all_recurrences(dfg *d);
void delete_instr(dfg_instr* i);


// DFG
dfg* create_dfg(dfg_instr** d, int N, dfg_instr** c, int NConsts);
void restore_dfg(dfg *d);
dfg *copy_dfg(dfg* target);
void set_dfg_sorted(dfg *d, int sorted);
int is_dfg_sorted(dfg *d);
int get_n_instrs_by_op(dfg *d, char *op);
void display_dfg(dfg* d);
dfg *get_critical_path(dfg *d);
int set_dfg_instr(dfg *d, dfg_instr* target, int idx);
int getHighestInstrLat(dfg *d);
int getLowestInstrLat(dfg *d);
int getSerialExecLat(dfg *d);
dfg_instr **get_dfg_inputs(dfg *d);
dfg_instr **get_dfg_outputs(dfg *d);
dfg_instr **get_dfg_ops(dfg *d);
int get_node_sublist_size(dfg_instr **ls);
dfg_instr **merge_sublists(dfg_instr **l1, dfg_instr **l2);
void remove_instr_from_dfg(dfg *d, int idx);
int* get_associated_nodes(dfg* d);
void delete_dfg(dfg *d, int delete_instrs);

#endif
