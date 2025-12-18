#!/bin/bash

# Generate the DFG
python3 benchmarks/dfg_generator.py benchmarks/benchmark.c

# Check the argument
if [ "$#" -eq 0 ]; then
    # No arguments: do nothing extra
    python3 src/dfg_visualizer.py simplified_loop.dot -s
elif [ "$1" == "-v" ]; then
    # Run visualizer if -v is passed
    python3 src/dfg_visualizer.py simplified_loop.dot
else
    # Invalid argument
    echo "Usage: $0 [-v]"
    echo "  -v    visualize the generated DFG"
    exit 1
fi
