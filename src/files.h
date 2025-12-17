#ifndef FILES_H
#define FILES_H

#include "dfg.h"

#define MAX_OP_NAME_SIZE 15

dfg *import_dfg(char* filename);
cgra* import_cgra(char* filename);
cgra *new_import_cgra(char *filename);
int import_cgra_config(char* filename, cgra* c);
void trim_whitespace(char *str);
void to_uppercase(char *str);
int max_array(int arr[], size_t size);
float max_array_flt(float arr[], size_t size);
int max_array_idx(int arr[], size_t size);
float array_sum(float arr[], size_t size);
int array_sum_int(int arr[], size_t size);
float array_avg(float arr[], size_t size);
float array_std_dev(float arr[], size_t size);
void copyArray(int copy[], int target[], size_t size);

#endif
