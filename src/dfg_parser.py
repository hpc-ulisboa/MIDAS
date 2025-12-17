import argparse
import networkx as nx
import matplotlib.pyplot as plt

from dfg import *

# Set up argument parsing
parser = argparse.ArgumentParser(description="Process a DFG file and create a DFG object.")
parser.add_argument("filepath", type=str, help="Path to the DFG file")

args = parser.parse_args()

if args.filepath.endswith('.txt'):
    dfg = create_dfg_from_file(args.filepath)
elif args.filepath.endswith('.dot'):
    dfg = create_dfg_from_cgra_me(args.filepath)

dfg.export()
