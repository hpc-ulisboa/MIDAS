import os
import glob
import warnings
import random
import json

class RegisterFile:
    def __init__(self, size=0):
        self.size = size
        self.ports = {}

    def set_size(self, size):
        self.size = size

    def addReadPort(self, destination='None', num=1):
        self.ports[destination]  = self.ports.get(destination, 0) + num

class ProcessingElement:
    def __init__(self):
        self.name = ""
        self.tile = 0
        self.operations = []
        self.n_output_registers = 1
        self.registerFile = None
        self.constant_units = 0
        self.pipeline_stages = 1
        self.muxes = 0

        # Visual Representation
        self.color = '#befe6c'

    def add_funct(self, op):
        self.operations.append(op)

    def rmv_funct(self, op):
        self.operations.remove(op)

    def add_registerFile(self, size):
        self.registerFile = RegisterFile(size)

    def set_registerFile_size(self, size):
        if not isinstance(self.registerFile, RegisterFile):
            self.registerFile = RegisterFile(size)
        else:
            self.registerFile.set_size(size)

    def addRFReadPorts(self, destination='None', num=1):
        self.registerFile.addReadPort(destination, num)

    def set_n_output_registers(self, num_outputs):
        if num_outputs < 1:
            warnings.warn("Number of output registers must be at least 1. Setting to 1.")
            self.n_output_registers = 1
        else:
            self.n_output_registers = num_outputs

    def set_n_constant_units(self, num_units):
            self.constant_units = num_units

    def set_pipeline_stages(self, num_stages):
        self.pipeline_stages = num_stages

    def __repr__(self):
        return "PE"
    
    def set_color(self, color):
        self.color = color

    def get_color(self):
        return self.color

def import_pe_from_json(json_file_path):
    with open(json_file_path, 'r') as f:
        data = json.load(f)
    
    pe_data = data["PE"]
    
    pe = ProcessingElement()
    
    pe.name = pe_data.get("name")

    # Process registers
    registers = pe_data.get("registers", [])
    pe.registerFile = RegisterFile(len(registers))
    
    # Process muxes
    pe.muxes = len(pe_data.get("muxes", []))
    
    # Process FUs and collect operations
    fus = pe_data.get("fus", [])
    operations = []
    for fu in fus:
        ops = fu.get("ops", [])
        operations.extend(ops)
    pe.operations = operations
    
    # Process connections to adjust the registerFile's size
    outputs = pe_data.get("outputs", [])
    connections = pe_data.get("connections", [])
    for conn in connections:
        from_node = conn.get("from", "")
        to_node = conn.get("to", "")
        if from_node in registers and to_node in outputs:
            pe.registerFile.set_size(pe.registerFile.size - 1)
    
    return pe

def create_pe_dictionary(folder_path):

    pe_dict = {}
    
    # Get all JSON files in directory
    json_files = glob.glob(os.path.join(folder_path, "*.json"))
    
    for json_file in json_files:
        try:
            pe = import_pe_from_json(json_file)
            pe.set_color("#{:06x}".format(random.randint(0, 0xFFFFFF)))

            # Skip PEs without a name
            if not hasattr(pe, 'name') or not pe.name:
                warnings.warn(f"Skipping file {json_file} - No PE name found")
                continue
                
            # Handle duplicate names
            if pe.name in pe_dict:
                warnings.warn(f"Duplicate PE name '{pe.name}' found in {json_file} - Overwriting")
                
            pe_dict[pe.name] = pe
            
        except Exception as e:
            warnings.warn(f"Error processing {json_file}: {str(e)}")
            continue
            
    return pe_dict

class StreamPort:
    def __init__(self, isInput=True, isOutput=False): #isInput = True (input) or False (output)
        self.tile = 0
        self.isInput = isInput
        self.isOutput = isOutput
        self.rep = "I" if isInput and not isOutput else "O" if isOutput and not isInput else "IO" if isInput and isOutput else "X"
        # Visual Representation
        self.color = '#f0ff75'

    def __repr__(self):
        return self.rep    

    def get_color(self):
        return self.color

    def set_color(self, color):
        self.color = color