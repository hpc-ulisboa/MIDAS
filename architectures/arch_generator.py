import argparse
import networkx as nx
import matplotlib.pyplot as plt

from mpa import *

mpa = init_standard_cgra(2, 3, StreamPorts='all', mergeIOs=False)

#mpa.add_interconnect((1, 1), (2, 2), latency=1, export=True)

mpa.add_funct_to_PEs("ADD")
mpa.add_funct_to_PEs("SUB")
mpa.add_funct_to_PEs("MUL")
mpa.add_funct_to_PEs("ASHR")

mpa.set_pe_registerFile_sizes(4)

n_output_registers = 1
mpa.set_all_pe_num_output_registers(n_output_registers)

mpa.addRfReadPortsToAllPEs("FU", 1)
mpa.addRfReadPortsToAllPEs("OutputRegisters", n_output_registers)

mpa.plot()

mpa.export()
