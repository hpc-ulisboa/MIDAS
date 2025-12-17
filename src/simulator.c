#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include "dfg.h"
#include "cgra.h"
#include "files.h"

#define INPUT_FILES 1
#define MAX_COMMAND_SIZE 200

#define RESULT_FIFO_SIZE 10

// Structure to map command names to functions
typedef struct
{
    char *name;
    char *description;

} Command;

void display_sim_ver()
{
    printf("MIDAS - Mapping Infrastructure for Data Streaming-based DSAs, ver. 1.0\n");
}

/**
 * Displays the Simulator's command list
 */
void help(Command command[])
{
    int i;
    display_sim_ver();
    printf("These commands are defined internally. Type 'help' to see this list.\n\n");

    for (i = 0; command[i].name != NULL; i++)
    {
        printf("\033[1;36m%s\033[0;0m: \033[1;34m%s\033[1;0m\n", command[i].name, command[i].description);
    }
}

int main(int argc, char *argv[])
{
    if (argc > 1 + INPUT_FILES)
    {
        printf("Incorrect number of input files.\n");
        exit(0);
    }

    int scriptProvided = argv[1] != NULL;
    FILE *script = fopen(argv[1], "r+");
    if (script == NULL && scriptProvided == 1)
    {
        printf("ERROR: Invalid script file.\n");
        return 0;
    }

    dfg *d = NULL, **dfg_targets = NULL;
    cgra *c = NULL, *template = NULL;

    // Structure to store several mapped devices
    cgra **result_fifo = NULL;
    dfg **mapped_dfgs = NULL;

    int ***placed = NULL, quit = 0, fifo_ctr = 0, fifo_ptr1 = -1, fifo_ptr2 = -1, vectorwidth = 1, dfg_targets_idx = 0;
    char line[MAX_COMMAND_SIZE], command[MAX_COMMAND_SIZE], arg[MAX_COMMAND_SIZE], default_dfg_string[11];
    char constraints_file[MAX_COMMAND_SIZE] = "constraints.json";
    strncpy(default_dfg_string, "kernel.dfg\0", 11);
    memset(line, 0, MAX_COMMAND_SIZE);
    memset(command, 0, MAX_COMMAND_SIZE);
    memset(arg, 0, MAX_COMMAND_SIZE);

    // Array of command mappings
    Command commands[] = {
        {"quit", "\t\t\tcloses the program."},
        {"help", "\t\t\tdisplays the command list."},

        // Imports
        {"import_dfg", "\t\timports a dfg file (.dfg or .dot)."},
        {"import_cgra", "\t\timports a cgra architecture file."},
        //{"import_constraints", "\timports a HW DSE constraints file (.json)."},

        // Initial Design Point (Co-DSE)
        //{"generate_idp", "\t\tgenerates an initial architectural design point, based on the imported DFGs and the constraints file."},

        // Mapping
        {"place_and_route", "\tmaps the dfg to the cgra, with a heuristic-based algorithm."},

        // Displays
        {"display_dfg", "\t\tdisplays the dfg."},
        {"display_cgra", "\t\tdisplays the cgra."},
        {"display_arch", "\t\tdisplays the cgra's interconnect structure."},
        {"pr_summary", "\t\tdisplays the summary of the place and route."},
        //{"display_by_cycle", "\tdisplays the cgra, cycle by cycle."},
        //{"display_animation", "\tdisplays the cgra as a dataflow animation."},
        {"display_IOs", "\t\tdisplays the cgra IO Streams."},
        {"exec_time", "\t\tdisplays the total execution time."},
        {"util_ratio", "\t\tdisplays the utilization ratios."},
        {"throughput_analysis", "\tdisplays the throughput analyses."},
        {"ipc_analysis", "\t\tdisplays the analyses regarding the instructions per cycle."},
        {"vector_analysis", "\tdisplays the analyses regarding kernel vectorization. Argument: Unrolling Factor Considered (Default: 1)."},
        {"ii_analysis", "\t\tdisplays the analyses regarding the Initiation Interval (II)."},
        {"area_estimate", "\t\tdisplays an estimate for the area of the device, in squared microns, considering the UMC 28nm tech."},
        {"power_estimate", "\t\tdisplays an aestimate for the power consumption of the device, in micro watts, considering the UMC 28nm tech."},
        //{"resource_cost", "\t\tdisplays the resource cost function."},
        {"resource_analysis", "\tdisplays the analyses regarding resources and utilization."},
        {"turn_off_unused", "\tturns off all unused PEs."},
        //{"parallelize_mapping", "\tmaps as many copies of the DFG as possible to the mapped device."},
        //{"store_mapping", "\t\tstores the mapped device in a result FIFO, within the program."},
        //{"load_mapping", "\t\tloads a mapped device from the result FIFO onto the current device template. Argument: Mapping Result Index (0 - 9)."},
        {"auto_prune", "\t\tautomatically prunes the device, according to the mapped kernel. Argument: Number of devices to include (0 - 9, or 'all', Default: 1)."},
        //{"aggressive_prune", "\tapplies aggressive optimization strategies to prune the device model for the imported kernels."},
        {"export_mapping", "\texports the mapping results to a JSON file."},
        //{"set_arch_vector_width", "\tsets the vector width of the architecture. Argument: <n> = Vector Width (Default: 1)."},
        //{"export_arch", "\t\texports the CGRA architecture to a JSON file."},
        //{"export_all", "\t\tperforms all exports simultaneously, assuming the default arguments"},

        //{"custom", "\t\tcustom command"}, // ?
        {NULL}};

    /* Program Kernel */
    display_sim_ver();
    printf("Type 'help' to see the list of available commands.\n\n\n\n");
    while (quit == 0)
    {

        memset(line, 0, MAX_COMMAND_SIZE);
        memset(command, 0, MAX_COMMAND_SIZE);
        memset(arg, 0, MAX_COMMAND_SIZE);
        if (scriptProvided)
        {
            fgets(line, sizeof(line), script);
            // Skip commented lines
            if (strlen(line) != 0 && line[0] == '#')
                continue;
        }
        else
        {
            printf("Enter command: ");
            fgets(line, sizeof(line), stdin);
        }

        // Parse command
        if (sscanf(line, "%99s %99[^\n]", command, arg) >= 1)
        {
            trim_whitespace(arg);

            int found = 0;

            // Check for predefined commands
            for (int i = 0; commands[i].name != NULL; i++)
            {
                // Command Abbreviatures
                if (!strcmp(command, "q"))
                {
                    strncpy(command, "quit\0", 5);
                }

                if (strcmp(command, commands[i].name) == 0)
                {

                    /*************************************************************
                     * General
                     *************************************************************/

                    // Quit the program
                    if (!strcmp(command, "quit"))
                    {
                        delete_cgra(c);
                        if (placed != NULL)
                        {
                            if (placed[0] != NULL)
                            {
                                for (int k = 0; k < get_dfg_size(d); k++)
                                    free(placed[0][k]);
                                free(placed[0]);
                            }
                            free(placed);
                        }
                        /* if (d != NULL && fifo_ctr <= 0)
                            delete_dfg(d, 1); */
                        if (template != NULL)
                            delete_cgra(template);

                        quit = 1;
                        found = 1;
                        break;
                    }

                    else if (!strcmp(command, "help"))
                    {
                        help(commands);
                    }

                    else if (!strcmp(command, "custom"))
                    {
                        /* if (d != NULL)
                            c = pr_simple(template, d); */

                        /* if (c != NULL)
                            c = pr_min_ii(c, template, d, placed, 4); */
                    }

                    /*************************************************************
                     * Imports
                     *************************************************************/

                    // Import a DFG File
                    else if (!strcmp(command, "import_dfg"))
                    {
                        char *kernel = arg;
                        // Check if arg ends with ".dot"
                        size_t len = strlen(arg);
                        if (len >= 4 && strcmp(arg + len - 4, ".dot") == 0)
                        {
                            // Construct the system command
                            char sys_cmd[256];
                            snprintf(sys_cmd, sizeof(sys_cmd), "python3 ./src/dfg_parser.py \"%s\"", arg);

                            // Call the Python script
                            int ret = system(sys_cmd);

                            if (ret == -1)
                            {
                                perror("system call failed");
                            }
                            else
                            {
                                kernel = default_dfg_string;
                            }
                        }

                        if (d != NULL)
                        {
                            if (placed != NULL)
                            {
                                if (placed[0] != NULL)
                                {
                                    for (int k = 0; k < get_dfg_size(d); k++)
                                        free(placed[0][k]);
                                    free(placed[0]);
                                }

                                free(placed);
                            }
                            if (!((fifo_ctr > 0 && d == mapped_dfgs[(fifo_ptr2 == 0 ? RESULT_FIFO_SIZE - 1 : fifo_ptr2 - 1)]) || (dfg_targets_idx > 0 && d == dfg_targets[dfg_targets_idx - 1])))
                                delete_dfg(d, 1);
                        }
                        // d = import_dfg(arg);
                        d = import_dfg(kernel);
                        if (d == NULL)
                        {
                            printf("Could not open DFG file.\n");
                            found = 1;
                            continue;
                        }
                        placed = (int ***)calloc(1, sizeof(int **));

                        // Create the DFG targets list if it does not yet exist
                        if (dfg_targets == NULL)
                        {
                            dfg_targets = (dfg **)calloc(5, sizeof(dfg *));
                            dfg_targets[dfg_targets_idx++] = d;
                        }
                        else if (dfg_targets_idx < 5)
                            dfg_targets[dfg_targets_idx++] = d;
                    }

                    // Import a CGRA Architecture File
                    else if (!strcmp(command, "import_cgra"))
                    {
                        if (template != NULL)
                            delete_cgra(template);
                        template = new_import_cgra(arg);
                        if (template == NULL)
                            printf("Could not open CGRA Architecture file.\n");
                    }

                    else if (!strcmp(command, "import_constraints"))
                    {
                        if (strlen(arg) > 0)
                        {
                            strncpy(constraints_file, arg, MAX_COMMAND_SIZE - 1);
                        }
                    }

                    /*************************************************************
                     * Initial Design Point (Co-DSE)
                     *************************************************************/
                    else if (!strcmp(command, "generate_idp"))
                    {
                        if (dfg_targets_idx <= 0)
                        {
                            printf("No DFGs imported.");
                            found = 1;
                            continue;
                        }
                        if (template != NULL)
                            delete_cgra(template);
                        template = generateInitialDesignPoint(dfg_targets, dfg_targets_idx, constraints_file);
                    }
                    /*************************************************************
                     * Mapping
                     *************************************************************/

                    // Map the input DFG to the input CGRA
                    else if (!strcmp(command, "place_and_route"))
                    {
                        if (d == NULL || placed == NULL)
                        {
                            printf("No valid DFG imported.\n");
                            found = 1;
                            continue;
                        }

                        if (c != NULL)
                            delete_cgra(c);

                        if (placed != NULL)
                        {
                            if (placed[0] != NULL)
                            {
                                for (int k = 0; k < get_dfg_size(d); k++)
                                    free(placed[0][k]);
                                free(placed[0]);
                            }
                        }
                        int mapper = atoi(arg);
                        *placed = (int **)calloc(get_dfg_size(d), sizeof(int *));

                        for (i = 0; i < get_dfg_size(d); i++)
                            (*placed)[i] = (int *)calloc(5, sizeof(int)); // [placed?, line & column, first_slice, last_slice, pipeline-rescheduled]
                        int fm = 1;
                        c = HandOfGod(template, d, placed, &fm, mapper, INFINITY, 1);
                    }

                    /*************************************************************
                     * Displays
                     *************************************************************/

                    // Display the DFG
                    else if (!strcmp(command, "display_dfg"))
                    {
                        if (d != NULL)
                            display_dfg(d);
                    }

                    // Display the CGRA in all active clock cycles
                    else if (!strcmp(command, "display_cgra"))
                    {
                        if (c != NULL && d != NULL)
                            display_cgra_in_time(c, d);
                    }

                    // Display the CGRA's Internal Architecture
                    else if (!strcmp(command, "display_arch"))
                    {
                        if (template != NULL)
                            display_config_arch(template);
                    }

                    // Display the Summary of the Place and Route
                    else if (!strcmp(command, "pr_summary"))
                    {
                        if (d == NULL || placed == NULL)
                        {
                            printf("No valid DFG imported.\n");
                            found = 1;
                            continue;
                        }
                        if (c != NULL)
                            mapping_summary(c, d, *placed);
                    }

                    // Display the Input and Output Streams in all active clock cycles
                    else if (!strcmp(command, "display_IOs"))
                    {
                        if (c != NULL)
                            display_cgra_IOs(c);
                    }

                    else if (!strcmp(command, "exec_time"))
                    {
                        if (c != NULL)
                        {
                            printf("Execution Time between iterations: \033[1;32m%d clock cycles\033[0;0m.\n", get_exec_time_between_iters(c, d, *placed));
                            printf("Execution Time for one iteration: \033[1;32m%d clock cycles\033[0;0m.\n", get_exec_time_one_iter(c, d, *placed));
                        }
                    }

                    else if (!strcmp(command, "turn_off_unused"))
                    {
                        if (c != NULL)
                            set_power_for_pe_set(c, POWER_OFF, FREE);
                    }

                    else if (!strcmp(command, "parallelize_mapping"))
                    {
                        if (c != NULL)
                            c = parallelize_mapping(c, d, placed, 1);
                    }

                    else if (!strcmp(command, "store_mapping"))
                    {
                        if (c != NULL)
                        {

                            if (fifo_ctr == 0)
                            {
                                result_fifo = (cgra **)calloc(RESULT_FIFO_SIZE, sizeof(cgra *));
                                mapped_dfgs = (dfg **)calloc(RESULT_FIFO_SIZE, sizeof(dfg *));
                                fifo_ptr1 = 0;
                                fifo_ptr2 = 0;
                            }

                            if (result_fifo[fifo_ptr2] != NULL)
                                delete_cgra(result_fifo[fifo_ptr2]);
                            if (mapped_dfgs[fifo_ptr2] != NULL)
                                delete_dfg(mapped_dfgs[fifo_ptr2], 1);
                            mapped_dfgs[fifo_ptr2] = d;
                            result_fifo[fifo_ptr2++] = copy_all_cgra_slices(c);
                            fifo_ctr++;
                            if (fifo_ptr2 >= RESULT_FIFO_SIZE)
                                fifo_ptr2 = 0;
                            if (fifo_ctr >= RESULT_FIFO_SIZE && fifo_ptr2 < RESULT_FIFO_SIZE - 1)
                                fifo_ptr1 = fifo_ptr1 + 1;
                            else if (fifo_ctr >= RESULT_FIFO_SIZE)
                                fifo_ptr1 = 0;
                        }
                    }

                    else if (!strcmp(command, "load_mapping"))
                    {
                        if (result_fifo != NULL && template != NULL)
                        {
                            if ((atoi(arg) >= 0 && atoi(arg) < RESULT_FIFO_SIZE && fifo_ctr >= RESULT_FIFO_SIZE) || (atoi(arg) >= 0 && atoi(arg) < fifo_ptr2))
                            {
                                if (c != NULL)
                                {
                                    delete_cgra(c);
                                }
                                c = load_mapping(template, result_fifo[atoi(arg)]);
                                d = mapped_dfgs[atoi(arg)];
                            }
                            else
                            {
                                printf("No mapped device present at the requested index.\n");
                            }
                        }
                    }

                    else if (!strcmp(command, "auto_prune"))
                    {
                        if (result_fifo != NULL)
                        {
                            // Pruning Info: [conns removed, registers removed, PEs removed, SPs removed, output registers, fu_ops, rf ports, fu inputs]
                            int Nprune, pruning_info[] = {0, 0, 0, 0, 0, 0, 0, 0};
                            if (!strcmp(arg, "all"))
                                Nprune = RESULT_FIFO_SIZE > fifo_ctr ? fifo_ptr2 : RESULT_FIFO_SIZE;
                            else if (atoi(arg) >= 0 && atoi(arg) < RESULT_FIFO_SIZE && strlen(arg) > 0)
                                Nprune = atoi(arg);
                            else
                                Nprune = 1;

                            if (fifo_ctr == 0){
                                auto_prune(result_fifo, dfg_targets, template, dfg_targets_idx, pruning_info);
                            }
                            else
                            {
                                // auto_prune_single(c, template, d, *placed);
                                auto_prune(result_fifo, mapped_dfgs, template, Nprune, pruning_info);
                            }

                            printf("\033[1;33mPruning Results:\033[0;0m\n");
                            printf("\tConnections Removed: \033[1;34m%d\033[0;0m\n", pruning_info[0]);
                            printf("\tOutput Registers Removed: \033[1;34m%d\033[0;0m\n", pruning_info[4]);
                            printf("\tRegisters Removed: \033[1;34m%d\033[0;0m\n", pruning_info[1]);
                            printf("\tRegister File R/W Ports Removed: \033[1;34m%d\033[0;0m\n", pruning_info[6]);
                            printf("\tFU Inputs Removed: \033[1;34m%d\033[0;0m\n", pruning_info[7]);
                            printf("\tFU Operations Removed: \033[1;34m%d\033[0;0m\n", pruning_info[5]);
                            printf("\tPEs removed: \033[1;34m%d\033[0;0m\n", pruning_info[2]);
                            printf("\tStreaming I/O Ports Removed: \033[1;34m%d\033[0;0m\n", pruning_info[3]);
                        }
                    }

                    else if (!strcmp(command, "aggressive_prune"))
                    {
                        if (dfg_targets_idx <= 0)
                        {
                            printf("No DFGs imported.\n");
                        }
                        else
                        {
                            if (result_fifo == NULL)
                            {
                                result_fifo = (cgra **)calloc(RESULT_FIFO_SIZE, sizeof(cgra *));
                            }
                            template = aggressiveOpt(template, result_fifo, dfg_targets, dfg_targets_idx, arg, constraints_file);
                        }
                    }

                    // Display the CGRA, cycle by cycle
                    else if (!strcmp(command, "display_by_cycle"))
                    {
                        if (c != NULL)
                            display_cycle_by_cycle(c, d, *placed);
                    }

                    // Display the CGRA, as an animation
                    else if (!strcmp(command, "display_animation"))
                    {
                        if (c != NULL)
                            display_animation(c, d, *placed);
                    }

                    // Displays the Utilization Ratios
                    else if (!strcmp(command, "util_ratio"))
                    {
                        if (c == NULL)
                        {
                            printf("CGRA not yet mapped.\n");
                            found = 1;
                            continue;
                        }

                        printf("\033[1;33mUtilization Ratios:\033[0;0m\n");

                        float util_ratio = get_dynamic_pe_util_ratio(c), routing_ratio = get_dynamic_pe_util_ratio_w_routing(c);

                        // PE Util Ratios
                        printf("\tStatic PE Utilization:\t");
                        if (get_pe_util_ratio(c) < 0.33)
                            printf("\033[1;31m");
                        else if (get_pe_util_ratio(c) < 0.67)
                            printf("\033[1;33m");
                        else
                            printf("\033[1;32m");
                        printf("%.2f%%\033[0;0m\n", 100.0 * get_pe_util_ratio(c));
                        printf("\tDynamic PE Utilization:\t");
                        if (util_ratio < 0.33)
                            printf("\033[1;31m");
                        else if (util_ratio < 0.67)
                            printf("\033[1;33m");
                        else
                            printf("\033[1;32m");
                        printf("%.2f%%\033[0;0m\n", 100.0 * util_ratio);
                        printf("\tDynamic PE Utilization, including routing:\t");
                        if (routing_ratio - util_ratio < 0.10)
                            printf("\033[1;32m");
                        else if (routing_ratio - util_ratio < 0.25)
                            printf("\033[1;33m");
                        else
                            printf("\033[1;31m");
                        printf("%.2f%%\033[0;0m\n", 100.0 * routing_ratio);
                        printf("\tOutput Register Utilization:\t");
                        if (output_register_util_ratio(c) < 0.33)
                            printf("\033[1;31m");
                        else if (output_register_util_ratio(c) < 0.67)
                            printf("\033[1;33m");
                        else
                            printf("\033[1;32m");
                        printf("%.2f%%\033[0;0m\n", 100.0 * output_register_util_ratio(c));
                        printf("\tRegister File Allocation:\t");
                        if (register_file_util_ratio(c) < 0.33)
                            printf("\033[1;31m");
                        else if (register_file_util_ratio(c) < 0.67)
                            printf("\033[1;33m");
                        else
                            printf("\033[1;32m");
                        printf("%.2f%%\033[0;0m\n", 100.0 * register_file_util_ratio(c));
                        printf("\tAllocation Ratio of the most constrained Register File:\t");
                        if (most_constrained_RF_util_ratio(c) < 0.33)
                            printf("\033[1;31m");
                        else if (most_constrained_RF_util_ratio(c) < 0.67)
                            printf("\033[1;33m");
                        else
                            printf("\033[1;32m");
                        printf("%.2f%%\033[0;0m\n\n", 100.0 * most_constrained_RF_util_ratio(c));
                    }

                    else if (!strcmp(command, "throughput_analysis"))
                    {
                        if (c == NULL)
                        {
                            printf("No DFG has been mapped yet!\n");
                        }
                        else
                        {
                            printf("\033[1;36mThroughput Analyses:\033[0;0m\n");
                            printf("\tMaximum \033[1;36mInput\033[0;0m Throughput:\t");
                            printf("\033[1;35m%.2f\033[0;0m stream inputs / cycle\n", max_input_throughput(c));
                            printf("\tAverage \033[1;36mInput\033[0;0m Throughput:\t");
                            printf("\033[1;35m%.2f\033[0;0m stream inputs / cycle\n", avg_input_throughput(c));
                            printf("\tMaximum \033[1;32mOutput\033[0;0m Throughput:\t");
                            printf("\033[1;35m%.2f\033[0;0m stream outputs / cycle\n", max_output_throughput(c));
                            printf("\tAverage \033[1;32mOutput\033[0;0m Throughput:\t");
                            printf("\033[1;35m%.2f\033[0;0m stream outputs / cycle\n\n", avg_output_throughput(c));
                        }
                    }

                    // Displays the IPC (context)
                    else if (!strcmp(command, "ipc_analysis"))
                    {
                        if (c == NULL)
                            printf("No DFG has been mapped yet!\n");
                        else
                        {
                            printf("\033[1;33mInstructions per Cycle:\033[0;0m\n");
                            printf("\tMaximum IPC: \033[1;33m%.2f\033[0;0m\n", max_ipc(c));
                            printf("\tAverage IPC: \033[1;33m%.2f\033[0;0m\n\n", avg_ipc(c));
                        }
                    }

                    else if (!strcmp(command, "vector_analysis"))
                    {
                        if (c == NULL)
                            printf("No DFG has been mapped yet!\n");
                        else
                        {
                            int unrollingFactor;
                            if (atoi(arg) >= 1 && atoi(arg) < __INT_MAX__)
                                unrollingFactor = atoi(arg);
                            else
                                unrollingFactor = 1;
                            printf("\033[1;36mVectorization:\033[0;0m\n");
                            printf("\tMaximum Vector Width: \033[1;35m%d\033[0;0m\n", maxVectWidth(c));
                            printf("\tMaximum Vectorized Iterations Per Cycle: \033[1;35m%d\033[0;0m\n\n", maxVectIterPerCycle(c, unrollingFactor));
                        }
                    }

                    else if (!strcmp(command, "ii_analysis"))
                    {
                        if (c == NULL)
                            printf("No DFG has been mapped yet!\n");
                        else
                        {
                            printf("\033[1;35mInitiation Interval (II):\033[0;0m\n");
                            printf("\tMinimum II (MII): \033[1;36m%d\033[0;0m\tObtained II: \033[1;36m%d\033[0;0m\n", getDeviceMII(c), get_n_cgra_slices(c));
                            printf("\tII Ratio: \033[1;35m%.2f\033[0;0m\n\n", ratioII(c));
                            /* printf("\033[1;35mInitiation Interval (II):\033[0;0m\n");
                            printf("\tMinimum II (MII): \033[1;36m%d\033[0;0m\n", getDeviceMII(c));
                            printf("\tObtained II: \033[1;36m%d\033[0;0m\n", get_n_cgra_slices(c));
                            printf("\tII Ratio: \033[1;35m%.2f\033[0;0m\n\n", ratioII(c)); */
                        }
                    }

                    else if (!strcmp(command, "area_estimate"))
                    {
                        if (template == NULL)
                            printf("CGRA not yet imported.\n");
                        else
                        {
                            printf("\033[1;34mArea Estimate:\033[0;0m\t%.2f um^2\n", get_cgra_area_estimate(template));
                        }
                    }

                    else if (!strcmp(command, "power_estimate"))
                    {
                        if (template == NULL)
                            printf("CGRA not yet imported.\n");
                        else
                        {
                            printf("\033[1;34mPower Estimate:\033[0;0m\t%.2f uW\n", get_cgra_power_estimate(template));
                        }
                    }

                    // Displays the Resource Cost Function Value
                    else if (!strcmp(command, "resource_cost"))
                    {
                        if (c == NULL)
                        {
                            printf("CGRA not yet mapped.\n");
                            found = 1;
                            continue;
                        }

                        printf("\033[1;33mResource Cost: ");
                        if (get_resource_cost(c, d, *placed) < 100.0)
                            printf("\033[1;32m");
                        else if (get_resource_cost(c, d, *placed) < 140.0)
                            printf("\033[1;33m");
                        else
                            printf("\033[1;31m");

                        printf("%.2f\033[0;0m\n\n", get_resource_cost(c, d, *placed));
                    }

                    else if (!strcmp(command, "resource_analysis"))
                    {
                        printf("\033[1;33mResource Analysis:\033[0;0m\n");
                        printf("-----------------\n");

                        printf("Work in progress!\n");
                    }

                    else if (!strcmp(command, "set_arch_vector_width"))
                    {
                        int proposed_width = 0;
                        if (atoi(arg) >= 1 && atoi(arg) < __INT_MAX__)
                            proposed_width = atoi(arg);
                        if (proposed_width > 0)
                        {
                            vectorwidth = proposed_width;
                        }
                        else
                        {
                            printf("Vector width should be > 0. Setting vector width to default value of 1.\n");
                            vectorwidth = 1;
                        }
                    }

                    else if (!strcmp(command, "export_mapping"))
                    {
                        if (c == NULL)
                        {
                            printf("No valid CGRA imported!\n");
                        }
                        else if (d == NULL)
                        {
                            printf("No valid DFG imported!\n");
                        }
                        else
                        {
                            if (strlen(arg) > 1)
                                exportMapping(c, d, placed, arg, vectorwidth);
                            else
                                exportMapping(c, d, placed, "mapping_results", vectorwidth);
                        }
                    }

                    else if (!strcmp(command, "export_arch"))
                    {
                        int export_ii;
                        if (c == NULL)
                        {
                            printf("\033[1;33mWARNING: No valid CGRA imported!\033[0;0m\n");
                            export_ii = 1;
                        }
                        else
                        {
                            export_ii = get_n_cgra_slices(getFirstSlice(c));
                        }
                        if (strlen(arg) > 1)
                            exportArch(template, arg, export_ii, vectorwidth);
                        else
                            exportArch(template, "design", export_ii, vectorwidth);
                    }

                    else if (!strcmp(command, "export_all"))
                    {
                        if (c == NULL)
                        {
                            printf("No valid CGRA imported!\n");
                        }
                        else if (d == NULL)
                        {
                            printf("No valid DFG imported!\n");
                            int export_ii = get_n_cgra_slices(getFirstSlice(c));
                            exportArch(template, "design", export_ii, vectorwidth);
                        }
                        else
                        {
                            int export_ii = get_n_cgra_slices(getFirstSlice(c));
                            exportMapping(c, d, placed, "mapping_results", vectorwidth);
                            exportArch(template, "design", export_ii, vectorwidth);
                        }
                    }

                    found = 1;
                    break;
                }
            }

            if (!found)
            {
                printf("Unknown command.\n");
            }
        }
        else if (feof(script))
        {
            scriptProvided = 0;
        }
    }

    if (scriptProvided)
        fclose(script);

    if (result_fifo != NULL)
    {
        for (int i = 0; i < RESULT_FIFO_SIZE; i++)
        {
            if (result_fifo[i])
                delete_cgra(result_fifo[i]);

            if (mapped_dfgs)
            {
                if (mapped_dfgs[i])
                {
                    int j;
                    for (j = 0; j < dfg_targets_idx; j++)
                    {
                        if (dfg_targets[j] == mapped_dfgs[i])
                            break;
                    }
                    if (j >= dfg_targets_idx)
                        delete_dfg(mapped_dfgs[i], 1);
                }
            }
        }
        free(result_fifo);
        free(mapped_dfgs);
    }
    for (int i = 0; i < dfg_targets_idx; i++)
    {
        if (dfg_targets[i])
            delete_dfg(dfg_targets[i], 1);
    }
    free(dfg_targets);

    // MUST STILL DEBUG
    // display_neighbours(c, 1);

    // printf("//------- CRITICAL PATH -------///\n\n");
    // dfg* cpath = get_dfg_critical_path_after_mapping(c, d, placed);
    // display_dfg(cpath);

    return 0;
}
