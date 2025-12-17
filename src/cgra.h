#ifndef INFINITY
#define INFINITY __INT_MAX__
#endif

#ifndef CGRA_H
#define CGRA_H

#include "dfg.h"
#include "ops.h"

typedef struct _cgra cgra;


#define BLOCK -1
#define FREE 0
#define IN_USE 1
#define NOT_YET_COMMITTED -1

#define FUNCTS 8 // number of possible PE functions (different "PE types")


#define HORIZONTAL 0
#define VERTICAL 1
#define DIAGONAL 2
#define ADJACENT 3
#define LEFT_TO_RIGHT 4
#define RIGHT_TO_LEFT 5
#define UP_TO_DOWN 6
#define DOWN_TO_UP 7
#define DIAGONAL_SE 8
#define DIAGONAL_NE 9
#define DIAGONAL_NW 10
#define DIAGONAL_SW 11
#define WRAP_AROUND_LR 12
#define WRAP_AROUND_RL 13
#define WRAP_AROUND_UD 14
#define WRAP_AROUND_DU 15
#define STREAM_CONN 16

#define POWER_OFF 0
#define POWER_ON 1

#define ASAP 1
#define ALAP 0

cgra *create_cgra(int L, int C, int se_ld, int se_st, int dw);
void set_cgra_value(cgra* t, int val, int l, int c);
void set_cgra_tile_funct(cgra* nc, int l, int c, int funct);
dfg_instr* get_cgra_tile(cgra *t, int l, int c);
int *getPENeighbours(cgra *c, int i, int j);
void add_conn_state(cgra *c, int i, int j, int opID);
void remove_conn_state(cgra *c, int i, int j, int opID);
int connUsedBy(cgra *c, int i1, int j1, int i2, int j2, int opID);
int getConnVal(cgra *c, int i1, int j1, int i2, int j2);
int getConnTime(cgra *c, int i1, int j1, int i2, int j2);
int checkConnValTime(cgra *c, int i1, int j1, int i2, int j2, int val, int time);
void setConnValTime(cgra *c, int i1, int j1, int i2, int j2, int val, int time);
int markOutputRegister(cgra *c, int i, int j, int idx, int val, int time);
int markUncommittedOutputRegister(cgra *c, int i, int j, int val, int time);
int hasFreeOutputRegister(cgra *c, int i, int j);
int changeSetReservation(cgra *c, int i, int j, int old, int newval);
int reserveRFReadPort(cgra *c, int i, int j, int targetStructure, int val, int t);
int removeRFRPReservationMuxIn(cgra *c, int i, int j, int val, int t);
int removeRFRPReservationOR(cgra *c, int i, int j, int val, int t);
int getNFreeRFRPMuxIn(cgra *c, int i, int j);
int getNFreeRFRPOR(cgra *c, int i, int j);
int getNRFRPMuxIn(cgra *c, int i, int j);
int getNRFRPOR(cgra *c, int i, int j);
void setRFAccess(cgra *c, int i, int j, int val);
int getRFAccess(cgra *c, int i, int j);
int getRFSize(cgra *c, int i, int j);
void setPEPipelineStages(cgra *c, int i, int j, int numStages);
int getPEPipelineStages(cgra *c, int i, int j);
int getNFUInputs(cgra *c, int i, int j);
int getNumOutputRegisters(cgra *c, int i, int j);
int hasOutputRegister(cgra *c, int i, int j, int val, int time);
int getOutputRegister(cgra *c, int i, int j, int idx);
int getOutputRegisterTime(cgra *c, int i, int j, int idx);
int hasFreeOutputRegister(cgra *c, int i, int j);
int hasLRFEntry(cgra *c, int i, int j, int t, int val);
int entrySignedBy(cgra *c, int i, int j, int t, int val, int id);
int getNFreeLRFEntries(cgra *c, int i, int j);
int getNFreeCUEntries(cgra *c, int i, int j);
int hasCUEntry(cgra *c, int i, int j, int val);
int reserveRegAddr(cgra *c, int i, int j, int t, int id, int addr);
int signCUEntry(cgra *c, int i, int j, int val, int id);
int unsignCUEntry(cgra *c, int i, int j, int val, int id);
int getCUsize(cgra *c, int i, int j);
int getAddress(cgra *c, int i, int j, int t, int val);
int getAddressNoTime(cgra *c, int i, int j, int val);
int getCnstAddress(cgra *c, int i, int j, int val);
int getconnLat(cgra *c, int i1, int j1, int i2, int j2);
int isAddressable(cgra *c, int i, int j, int val, int t, int cc);
int swapRegister(cgra *c, int i, int j, int t, int val, int addr);
int getLRFVal(cgra *c, int i, int j, int addr);
int reserveConstantUnit(cgra *c, int i, int j, int id);
int signLRFEntry(cgra *c, int i, int j, int t, int val, int id);
int unsignLRFEntry(cgra *c, int i, int j, int t, int val, int id);
int entryIsSigned(cgra *c, int i, int j, int t, int val);
int isConnectedToPE(cgra *c, int i, int j, int id, int iid, int t);
int hasConnectedPEs(cgra *c, int i, int j);
int hasConnectedPEsWithVal(cgra *c, int i, int j, int val, int time);
void initOutputRegisters(cgra *c, int i, int j, int n, int rfrp);
void initLocalRegisterFile(cgra *c, int i, int j, int rfsize, int rfrp);
void initConstantUnits(cgra *c, int i, int j, int cusize);
int ioConnectsToPE(cgra *c, int i, int j);
int inputConnectedToPE(cgra *c, int i, int j, int id, int iid, int fu_t);
int hasFreeLRFEntry(cgra *c, int i, int j);
int reserveRegister(cgra *c, int i, int j, int t, int id);
int setUncommittedReservation(cgra *c, int i, int j, int t, int id);
int reserveRegistersForOp(cgra *first_slice, dfg_instr* target, dfg_instr* input, int *scheduled, int II, int **placed);
int freeRegisters(cgra *c, int i, int j, int id, int num_regs);
void resetLocalRegisterFiles(cgra *c);
int connectsToPE(cgra *c, int i, int j, int id, int iid);
void set_cgra_interconnect(cgra *nc, int i1, int j1, int i2, int j2, int lat);
void set_cgra_interconnects(cgra *nc, int side, int lat);
int get_cgra_interconnect(cgra *nc, int i1, int j1, int i2, int j2);
void set_next_slice(cgra *nc, cgra* slice);
void set_prev_slice(cgra *nc, cgra *slice);
void set_cgra_tile(cgra *t, int l, int c, dfg_instr *curr);
int get_cgra_tile_value(cgra *t, int l, int c);
int isOutputStreamPort(cgra *c, int i, int j);
int isInputStreamPort(cgra *c, int i, int j);
int isStreamPort(cgra *c, int i, int j);
int rmvStreamFuncts(cgra *c, int i, int j);
int isPE(cgra *c, int i, int j);
void set_grid_state(cgra *c, int i, int j, int val);
void set_ic_states(cgra *c, int target, int state);
void clear_ic_states(cgra *c);
void clear_ic_poweredOn_states(cgra *c);
int getEnteredPort(cgra *c, int i, int j, int address);
int get_cgra_L(cgra *c);
int get_cgra_C(cgra *c);
int getDataWidth(cgra *c);
int get_grid_lat(cgra *c, int i, int j);
int get_cgra_ld_trghpt(cgra *c);
int get_cgra_st_trghpt(cgra *c);
int get_grid_state(cgra *c, int i, int j);
int get_mapping(cgra *c);
int setDirectionOpIDs(cgra *c, int i, int j, int *directions, int idx);
int peHasFunct(cgra *c, int i, int j, int op);
int pe_in_use(cgra *c, int pos);
int pe_occupied(cgra *c, int i, int j);
int pe_occupied_by(cgra *c, int i, int j);
int connInUse(cgra *c, int i1, int j1, int i2, int j2);
void setDeviceMII(cgra *c, int MII);
int getDeviceMII(cgra *c);
int get_n_cgra_slices(cgra *nc);
int get_n_pe(cgra *nc);
int get_n_pe_w_funct(cgra *nc, int funct);
int get_pe_power_mode(cgra *c, int i, int j);
int hasInterconnects(cgra *nc, int config);
void set_pe_power_mode(cgra *c, int i, int j, int powerOn);
void set_execution_time(cgra *c, int exec_time);
void set_num_contexts_for_one_iteration(cgra *c, int num);
void set_mapping(cgra *c, int algorithm_id);
int get_execution_time(cgra *c);
int get_ic_cost(cgra* c, int i1, int j1, int i2, int j2);
void remove_pe_from_cgra(cgra* nc, int l, int c);
cgra* get_next_slice(cgra* nc);
cgra *get_prev_slice(cgra *nc);
cgra *get_slice(cgra *nc, int slice_num);
cgra *getFirstSlice(cgra *c);
cgra *getNextModuloSlice(cgra *fs);
cgra *getPrevModuloSlice(cgra *fs);
cgra *getModuloSlice(cgra *fs, int t, int II);
cgra* copy_cgra(cgra* target);
cgra *buildBaseCGRA(cgra *template, int II);
cgra *copy_all_cgra_slices(cgra *target);
void set_power_for_pe_set(cgra *c, int powerMode, int state);
void delete_cgra(cgra* c);


// Displays
void display_cgra(cgra* c, int type);
void display_config_arch(cgra *template);
void display_cgra_in_time(cgra *c, dfg *d);
void display_cgra_IOs(cgra *c);

// Scheduling
void getRequiredResources(dfg *d, int dfg_resources[OP_MAX]);
void getAvailableResources(cgra *template, int cgra_resources[OP_MAX]);
int *rasASAP(cgra *template, dfg *d);
int *rasALAP(cgra *template, dfg *d);
int *rasMixedScheduling(cgra *template, dfg *d);
int *getFixedNodeMobility(int *scheduled, dfg_instr *target);
int *modulo_scheduling(int *scheduled, dfg *d, int II);
void adjustModuloScheduling(int *s, int *sc, int *so, cgra *c, dfg *d, dfg_instr **ops, int II);
int reScheduleNode(int *scheduled, cgra *template, dfg *d, dfg_instr* target, int distance, int keepMaxLat, int II);
int pipelineReschedule(int *scheduled, cgra *template, dfg *d, dfg_instr *target, int **placed, int distance, int II);
int invertPipelineReschedule(int *scheduled, cgra *template, dfg *d, dfg_instr *target, int **placed, int distance, int II);
void topologicalSortDFG(dfg *d);

int *schedule_dfg(cgra *template, dfg *d);
int *schedule_dfg_asap(cgra *template, dfg *d);
int *schedule_dfg_alap(cgra *template, dfg *d);

// Place and Route Primitives
cgra *buildBaseCGRA(cgra *template, int II);
int getResMinII(cgra *template, dfg *d);
int getRecMinII(int *base_scheduling, dfg* d);
int getRFLimitations(cgra *c, dfg *d);
int getMII(cgra *c, dfg *d, int *schedule);
int allInstructionsPlaced(int **arr, dfg *d);
int checkStructHazard(cgra *c, dfg_instr *target, int i, int j);
int placeOp(cgra *first_slice, int i, int j, dfg *d, dfg_instr *target, int **placed, int *schedule, int II);
int routeOp(cgra *first_slice, dfg_instr *target, int **placed, int *schedule, int II);
int unmapOp(cgra *first_slice, dfg *d, dfg_instr *target, int **placed, int *schedule, int II);
void unRouteOutputs(cgra *first_slice, dfg *d ,dfg_instr *target, int **placed, int *schedule, int II);
void clearMapping(cgra *fs, dfg *d, dfg_instr **dfg_ops, int **placed, int *schedule, int II);
cgra *HandOfGod(cgra *template, dfg *d, int ***placed, int *first_mapping, int mapper, int maxII, int verbose);

//SimAnnealing
typedef struct _temp temperature;
float *generateInitialPlacement(cgra *fs, dfg *d, dfg_instr **dfg_ops, int **placed, int *schedule, int II, int *routed);
float computeMoveCostStdDev(cgra *fs, dfg *d, dfg_instr **dfg_ops, int ***placed, int *schedule, int II, float *cost, int N);
void checkValidMapping(int *routed, int N);
void ripUpOp(cgra *fs, dfg *d, dfg_instr *target, int **placed, int *schedule, int II);
float *m1(cgra *fs, dfg *d, dfg_instr ** dfg_ops, dfg_instr *target, int i_pos, int j_pos, int **placed, int *schedule, int II, float *cost, int *routed);
float *anneal_swap(cgra *fs, dfg *d, dfg_instr **dfg_ops, dfg_instr *target, int i_pos, int j_pos,
    int **placed, int *schedule, int II, float *cost, int *routed, int *swapped);
float computeCost(cgra *fs, dfg_instr *target, int **placed, int *schedule, int II, int penalty);
void updateCost(float *cost, float *newCost, int N);
int evaluateMoveCost(temperature* t, float *cost, float *newCost, int arr_size);
temperature *initTemperature(int initialTemp);
float getTemperature(temperature *t);
void updateTemperature(temperature *t, int nAccepted, int nTotal);
int checkTemperatureStopCriteria(temperature *t, float *cost, int N);
void deleteTemperature(temperature *t);

// Spatial Mapping
int __spatial__routeOp(cgra *fs, dfg_instr *target, int **placed, int *schedule, int II);

// Simulation Mapper
void __simmap__placeAndRouteNode(cgra *fs, dfg *d, dfg_instr *target, int **placed, int *schedule);

// Other Mapping
int **generatePlacementMatrix(cgra *fs, dfg_instr *target, int **placed, int *schedule, int II, int *minDist);
void deletePlacementMatrix(int **mat, cgra *c);
int **getMappablePositions(cgra *fs, dfg *d, dfg_instr *target, int **placed, int *schedule, int II);
int **getFreePositions(cgra *fs, dfg_instr *target, int **placed, int *schedule, int II);
int **getPlaceablePositions(cgra *fs, dfg_instr *target, int **placed, int *schedule, int II);
void deleteMappablePosArr(int **mp, cgra *c);
void deleteFreePosArr(int **fpos, cgra *fs);
cgra *parallelize_mapping(cgra* c, dfg *d, int ***placed, int verbose);

// Resources and Utilization
int define_exec_time(cgra *first_slice, dfg *d, int **placed, int II);
int get_exec_time_between_iters(cgra *c, dfg *d, int **placed);
int get_exec_time_one_iter(cgra *c, dfg *d, int **placed);
float get_pe_util_ratio(cgra* c);
float get_dynamic_pe_util_ratio(cgra *c);
float get_dynamic_pe_util_ratio_w_routing(cgra *c);
float output_register_util_ratio(cgra *c);
float register_file_util_ratio(cgra *c);
float most_constrained_RF_util_ratio(cgra *c);
float max_input_throughput(cgra *c);
float avg_input_throughput(cgra *c);
float max_output_throughput(cgra *c);
float avg_output_throughput(cgra *c);
float max_ipc(cgra *c);
float avg_ipc(cgra *c);
int maxVectWidth(cgra *c);
int maxVectIterPerCycle(cgra *c, int unrollingFactor);
float ratioII(cgra *c);
float get_cgra_area_estimate(cgra *c);
float get_cgra_power_estimate(cgra *c);
float get_resource_cost(cgra *c, dfg *d, int **placed);

// Other Analyses
void display_cycle_by_cycle(cgra *c, dfg *d, int ** placed);
void display_animation(cgra *c, dfg *d, int **placed);
void mapping_summary(cgra *c, dfg *d, int **placed);

// Pruning
void auto_prune(cgra **c, dfg **d, cgra *template, int N, int *prune_info);
cgra *load_mapping(cgra *template, cgra *target);
cgra *buildHmgCopy(cgra *c, int rows, int cols);
cgra *generateInitialDesignPoint(dfg **dfg_targets, int n_dfgs, char *constraints_file);
cgra *aggressiveOpt(cgra *template, cgra **mapped_devs, dfg **dfg_targets, int n_dfgs, char *opt_target, char *constraints_file);

// Exports
int exportMapping(cgra *fs, dfg *d, int ***placed, char *filename, int vectorWidth);
int exportArch(cgra *fs, char *filename, int II, int vectorWidth);

void display_conns(cgra *c);

#endif