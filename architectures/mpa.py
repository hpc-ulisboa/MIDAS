import networkx as nx
import matplotlib.pyplot as plt
import copy

from proc_elmts import *

class MultiProcessorAggregate:
    def __init__(self):
        self.elements = {} # Dictionary to store the elements (PEs and SPs)
        self.pos = {}
        self.se_ld_bw = 128 # in bytes/cycle
        self.se_st_bw = 128 # in bytes/cycle
        self.data_width = 4 # in bytes

        self.ic_export_flags = []

    def add_element(self, elmt, x=0, y=0, rfSize=1):
        position = (x, -y)
        
        # Check if the position is already occupied
        if position in self.pos.values():
            raise ValueError(f"Position {position} is already occupied by another element.")
        
        if isinstance(elmt, ProcessingElement):
            elmt.set_registerFile_size(rfSize)
        self.elements[elmt] = {}
        self.pos[elmt] = (x,-y)

    def get_element(self, x, y):
        # Find the element that is at the specified position
        element = None
        for elmt, pos in self.pos.items():
            if pos == (x, -y):
                element = elmt
                break

        # If no element is found at that position, raise an error
        if element is None:
            #raise ValueError(f"No element found at position {(x, -y)}.")
            return None
        
        return element
    
    def get_coord(self, elmt):
        x, y = self.pos[elmt]
        return (x,-y)
              
    def remove_element(self, x, y):
        position = (x, -y)
        
        # Find the element that is at the specified position
        element_to_remove = None
        for elmt, pos in self.pos.items():
            if pos == position:
                element_to_remove = elmt
                break

        # If no element is found at that position, raise an error
        if element_to_remove is None:
            #raise ValueError(f"No element found at position {position}.")
            return 
        
        # Remove the element and its position
        del self.elements[element_to_remove]
        del self.pos[element_to_remove]
        
        # Remove any interconnects associated with the element
        for other_element in self.elements:
            self.elements[other_element].pop(element_to_remove, None)

    def add_interconnect(self, e1_or_pos, e2_or_pos, latency=0, bidirectional=False, export=False):
        # Resolve the first element (e1)
        if isinstance(e1_or_pos, tuple):  # (x, y) position given
            x, y = e1_or_pos
            # Find the element at the position (x, y)
            e1 = None
            for elmt, pos in self.pos.items():
                if pos == (x, -y):
                    e1 = elmt
                    break
            if e1 is None:
                raise ValueError(f"No element found at position ({x}, {y}).")
        else:  # Element name (e1) given
            e1 = e1_or_pos
        
        if e1 not in self.elements:
            raise ValueError(f"Element {e1} must be added before adding an interconnect.")

        # Resolve the second element (e2)
        if isinstance(e2_or_pos, tuple):  # (x, y) position given
            x, y = e2_or_pos
            # Find the element at the position (x, y)
            e2 = None
            for elmt, pos in self.pos.items():
                if pos == (x, -y):
                    e2 = elmt
                    break
            if e2 is None:
                raise ValueError(f"No element found at position ({x}, {y}).")
        else:  # Element name (e2) given
            e2 = e2_or_pos
        
        if e2 not in self.elements:
            raise ValueError(f"Element {e2} must be added before adding an interconnect.")
        
        # Add the interconnection between e1 and e2
        self.elements[e1][e2] = latency
        if bidirectional:
            self.elements[e2][e1] = latency

        x1, y1 = self.pos[e1]
        x2, y2 = self.pos[e2]

        if export:
            self.ic_export_flags.append(f"CONN {abs(y1)} {x1} {abs(y2)} {x2} {latency}")
            if bidirectional:
                self.ic_export_flags.append(f"CONN {abs(y2)} {x2} {abs(y1)} {x1} {latency}")    
        
    def remove_interconnect(self, e1_or_pos, e2_or_pos, bidirectional=False, export=False):
        # Resolve the first element (e1)
        if isinstance(e1_or_pos, tuple):  # (x, y) position given
            x, y = e1_or_pos
            # Find the element at the position (x, y)
            e1 = None
            for elmt, pos in self.pos.items():
                if pos == (x, -y):
                    e1 = elmt
                    break
            if e1 is None:
                raise ValueError(f"No element found at position ({x}, {y}).")
        else:  # Element name (e1) given
            e1 = e1_or_pos
        
        if e1 not in self.elements:
            raise ValueError(f"Element {e1} must be added before adding an interconnect.")

        # Resolve the second element (e2)
        if isinstance(e2_or_pos, tuple):  # (x, y) position given
            x, y = e2_or_pos
            # Find the element at the position (x, y)
            e2 = None
            for elmt, pos in self.pos.items():
                if pos == (x, -y):
                    e2 = elmt
                    break
            if e2 is None:
                raise ValueError(f"No element found at position ({x}, {y}).")
        else:  # Element name (e2) given
            e2 = e2_or_pos
        
        if e2 not in self.elements:
            raise ValueError(f"Element {e2} must be added before adding an interconnect.")
        
        # Remove the interconnection between e1 and e2
        self.elements[e1].pop(e2, None)
        if bidirectional:
            self.elements[e2].pop(e1, None)

        x1, y1 = self.pos[e1]
        x2, y2 = self.pos[e2]
        
        if export:
            self.ic_export_flags.append(f"CONN {abs(y1)} {x1} {abs(y2)} {x2} -1")
            if bidirectional:
                self.ic_export_flags.append(f"CONN {abs(y2)} {x2} {abs(y1)} {x1} -1") 

    def set_se_bw(self, ld=None, st=None):
        if ld != None:
            self.se_ld_bw = ld
        if st != None:
            self.se_st_bw = st
    
    def set_data_width(self, data_width):
        self.data_width = data_width

    # PEs

    def set_pe_by_coord(self, pos, pe):
        if not isinstance(pe, ProcessingElement):
            return
        x, y = pos
        self.remove_element(x, y)
        self.add_element(copy.deepcopy(pe), x, y, pe.registerFile.size)

    def set_sp_by_coord(self, pos, sp):
        if not isinstance(sp, StreamPort):
            return
        x, y = pos
        self.remove_element(x, y)
        self.add_element(copy.deepcopy(sp), x, y)

    def add_funct_to_PEs(self, funct):
        for pe in self.elements:
            if (isinstance(pe, ProcessingElement)):
                pe.operations.append(funct)

    def set_all_pe_num_output_registers(self, numOutputRegisters):
        for pe in self.elements:
            if (isinstance(pe, ProcessingElement)):
                pe.set_n_output_registers(numOutputRegisters)

    def set_pe_registerFile_sizes(self, rfsize):
        for pe in self.elements:
            if (isinstance(pe, ProcessingElement)):
                pe.set_registerFile_size(rfsize)

    def set_pe_num_constant_units(self, ncu):
        for pe in self.elements:
            if (isinstance(pe, ProcessingElement)):
                pe.set_n_constant_units(ncu)

    def addRfReadPortsToAllPEs(self, destination='None', num=1):
        for pe in self.elements:
            if (isinstance(pe, ProcessingElement)):
                pe.addRFReadPorts(destination, num)

    def add_pe_rfReadPorts(self, pe_or_pos, destination='None', num=1):
        if isinstance(pe_or_pos, tuple):  # (x, y) position given
            x, y = pe_or_pos
            # Find the element at the position (x, y)
            e1 = None
            for elmt, pos in self.pos.items():
                if pos == (x, -y):
                    e1 = elmt
                    break
            if e1 is None:
                raise ValueError(f"No element found at position ({x}, {y}).")
        else:  # Element name (e1) given
            e1 = pe_or_pos
        
        if e1 not in self.elements:
            raise ValueError(f"Element {e1} must be added before adding an interconnect.")
        e1.addRFreadPorts(destination, num)

    def add_funct_to_PE(self, pe_or_pos, funct):
        if isinstance(pe_or_pos, tuple):  # (x, y) position given
            x, y = pe_or_pos
            # Find the element at the position (x, y)
            e1 = None
            for elmt, pos in self.pos.items():
                if pos == (x, -y):
                    e1 = elmt
                    break
            if e1 is None:
                raise ValueError(f"No element found at position ({x}, {y}).")
        else:  # Element name (e1) given
            e1 = pe_or_pos
        
        if e1 not in self.elements:
            raise ValueError(f"Element {e1} must be added before adding an interconnect.")

        e1.operations.append(funct)
        return e1
    
    def set_PE_pipeline_stages(self, pe_or_pos, num_stages):
        if isinstance(pe_or_pos, tuple):  # (x, y) position given
            x, y = pe_or_pos
            # Find the element at the position (x, y)
            e1 = None
            for elmt, pos in self.pos.items():
                if pos == (x, -y):
                    e1 = elmt
                    break
            if e1 is None:
                raise ValueError(f"No element found at position ({x}, {y}).")
        else:  # Element name (e1) given
            e1 = pe_or_pos
        
        if e1 not in self.elements:
            raise ValueError(f"Element {e1} must be added before adding an interconnect.")

        e1.set_pipeline_stages(num_stages)
        return e1

    def set_all_PE_pipeline_stages(self, num_stages):
        for pe in self.elements:
            if (isinstance(pe, ProcessingElement)):
                pe.set_pipeline_stages(num_stages)

    def set_pe_registerFile_size(self, pe_or_pos, rfsize):
        if isinstance(pe_or_pos, tuple):  # (x, y) position given
            x, y = pe_or_pos
            # Find the element at the position (x, y)
            e1 = None
            for elmt, pos in self.pos.items():
                if pos == (x, -y):
                    e1 = elmt
                    break
            if e1 is None:
                raise ValueError(f"No element found at position ({x}, {y}).")
        else:  # Element name (e1) given
            e1 = pe_or_pos
        
        if e1 not in self.elements:
            raise ValueError(f"Element {e1} must be added before adding an interconnect.")
        
        e1.set_registerFile_size(rfsize)
        return e1

    def prune_stream_ports(self, bidirectional=False):
        st = 0
        ld = 0
        rows = min(pos[1] for pos in self.pos.values())
        cols = max(pos[0] for pos in self.pos.values())
        if isinstance(self.get_element(0, 0), StreamPort) and isinstance(self.get_element(0, 1), StreamPort):
            if isinstance(self.get_element(1, 0), StreamPort):
                if self.get_element(0, 0).isInput:
                    ld -= 1
                    if (bidirectional == True):
                        st -= 1
                else:
                    st -= 1
                self.remove_element(0, 0)

        if isinstance(self.get_element(cols + 1, 0), StreamPort) and isinstance(self.get_element(cols + 1, 1), StreamPort):
            if isinstance(self.get_element(cols, 0), StreamPort):
                if self.get_element(cols + 1, 0).isInput:
                    ld -= 1
                    if (bidirectional == True):
                        st -= 1
                else:
                    st -= 1
                self.remove_element(cols + 1, 0)

        if isinstance(self.get_element(cols, 0), StreamPort) and isinstance(self.get_element(cols, 1), StreamPort):
            if isinstance(self.get_element(cols - 1, 0), StreamPort):
                if self.get_element(cols, 0).isInput:
                    ld -= 1
                    if (bidirectional == True):
                        st -= 1
                else:
                    st -= 1
                self.remove_element(cols, 0)

        if isinstance(self.get_element(0, -rows), StreamPort) and isinstance(self.get_element(1, -rows), StreamPort):
            if isinstance(self.get_element(0, -rows - 1), StreamPort):
                if self.get_element(0, -rows).isInput:
                    ld -= 1
                    if (bidirectional == True):
                        st -= 1
                else:
                    st -= 1
                self.remove_element(0, -rows)
    
        if isinstance(self.get_element(0, -rows + 1), StreamPort) and isinstance(self.get_element(1, -rows + 1), StreamPort):
            if isinstance(self.get_element(0, -rows), StreamPort):
                if self.get_element(0, -rows + 1).isInput:
                    ld -= 1
                    if (bidirectional == True):
                        st -= 1
                else:
                    st -= 1
                self.remove_element(0, -rows + 1)

        if isinstance(self.get_element(cols, -rows), StreamPort) and isinstance(self.get_element(cols - 1, -rows), StreamPort):
            if isinstance(self.get_element(cols, -rows - 1), StreamPort):
                if self.get_element(cols, -rows).isInput:
                    ld -= 1
                    if (bidirectional == True):
                        st -= 1
                else:
                    st -= 1
                self.remove_element(cols, -rows)
    
        if isinstance(self.get_element(cols, -rows + 1), StreamPort) and isinstance(self.get_element(cols - 1, -rows + 1), StreamPort):
            if isinstance(self.get_element(cols, -rows), StreamPort):
                if self.get_element(cols, -rows + 1).isInput:
                    ld -= 1
                    if (bidirectional == True):
                        st -= 1
                else:
                    st -= 1
                self.remove_element(cols, -rows + 1)

        if isinstance(self.get_element(cols + 1, -rows), StreamPort) and isinstance(self.get_element(cols, -rows), StreamPort):
            if isinstance(self.get_element(cols + 1, -rows - 1), StreamPort):
                if self.get_element(cols + 1, -rows).isInput:
                    ld -= 1
                    if (bidirectional == True):
                        st -= 1
                else:
                    st -= 1
                self.remove_element(cols + 1, -rows)
    
        if isinstance(self.get_element(cols + 1, -rows + 1), StreamPort) and isinstance(self.get_element(cols, -rows + 1), StreamPort):
            if isinstance(self.get_element(cols + 1, -rows), StreamPort):
                if self.get_element(cols + 1, -rows + 1).isInput:
                    ld -= 1
                    if (bidirectional == True):
                        st -= 1
                else:
                    st -= 1
                self.remove_element(cols + 1, -rows + 1)
    
        return st, ld
    
    def add_stream_ports(self, port_locations='left', latency=1, isInput=True, bidirectional=False, mergeIOs=False):

        rows = min(pos[1] for pos in self.pos.values())
        cols = max(pos[0] for pos in self.pos.values())
        ld = 0
        st = 0
        out = not isInput if mergeIOs == False else True
        isInput = isInput if mergeIOs == False else True

        if port_locations == 'all' or port_locations == 'horizontal' or port_locations == 'left':
            # Check if any element is at column 0
            if any(pos[0] == 0 for pos in self.pos.values()):
                # Shift all elements to the right by 1
                for elmt in list(self.pos.keys()):
                    x, y = self.pos[elmt]
                    self.pos[elmt] = (x + 1, y)
                cols += 1
            # Add stream ports to the left 
            for i in range(rows,1):
                sp = StreamPort(isInput, isOutput=out)
                self.add_element(sp, 0, -i)
                if (isInput):
                    self.add_interconnect(sp, (1, -i), latency=latency, bidirectional=bidirectional and mergeIOs)
                    ld += 1
                    if (mergeIOs == True):
                        st += 1
                else:
                    self.add_interconnect((1, -i), sp, latency=latency, bidirectional=bidirectional and mergeIOs)
                    st += 1

        if port_locations == 'all' or port_locations == 'horizontal' or port_locations == 'right':
            inpt = isInput if mergeIOs == False else True
            if port_locations == 'all' or port_locations == 'horizontal':
                inpt = not isInput if mergeIOs == False else True
            for i in range(rows,1):
                sp = StreamPort(inpt, isOutput=not inpt if mergeIOs == False else True)
                self.add_element(sp, cols + 1, -i)
                if (inpt):
                    self.add_interconnect(sp, (cols, -i), latency=latency, bidirectional=bidirectional and mergeIOs)
                    ld += 1
                    if (mergeIOs == True):
                        st += 1
                else:
                    self.add_interconnect((cols, -i), sp, latency=latency, bidirectional=bidirectional and mergeIOs)
                    st += 1

        if port_locations == 'all' or port_locations == 'vertical' or port_locations == 'top':
            # Check if any element is at row 0
            if any(pos[1] == 0 for pos in self.pos.values()):
                # Shift all elements down by 1
                for elmt in list(self.pos.keys()):
                    x, y = self.pos[elmt]
                    self.pos[elmt] = (x, y - 1)
                rows -= 1
            # Add stream ports to the top
            for i in range(cols+1):
                sp = StreamPort(isInput, isOutput=out)
                self.add_element(sp, i, 0)
                if (isInput):
                    self.add_interconnect(sp, (i, 1), latency=latency, bidirectional=bidirectional and mergeIOs)
                    ld += 1
                    if (mergeIOs == True):
                        st += 1
                else:
                    self.add_interconnect((i, 1), sp, latency=latency, bidirectional=bidirectional and mergeIOs)
                    st += 1

        if port_locations == 'all' or port_locations == 'vertical' or port_locations == 'bottom':
            inpt = isInput if mergeIOs == False else True
            if port_locations == 'all' or port_locations == 'vertical':
                inpt = not isInput if mergeIOs == False else True
            for i in range(cols+1):
                sp = StreamPort(inpt, isOutput=not inpt if mergeIOs == False else True)
                self.add_element(sp, i, -rows + 1)
                if (inpt):
                    self.add_interconnect(sp, (i, -rows), latency=latency, bidirectional=bidirectional and mergeIOs)
                    ld += 1
                    if (bidirectional == True):
                        st += 1
                else:
                    self.add_interconnect((i, -rows), sp, latency=latency, bidirectional=bidirectional and mergeIOs)
                    st += 1

        if port_locations == 'all':
            if isinstance(self.get_element(0, 0), StreamPort) and self.get_element(0, 0).isInput:
                ld -= 1
                if (bidirectional == True):
                        st -= 1
            elif isinstance(self.get_element(0, 0), StreamPort) and not self.get_element(0, 0).isInput:
                st -= 1
            self.remove_element(0, 0)
        #if port_locations == 'all' or (isinstance(self.get_element(cols + 1, 1), StreamPort) and port_locations == 'top') or (isinstance(self.get_element(cols, 0), StreamPort) and port_locations == 'right'):
            if isinstance(self.get_element(cols + 1, 0), StreamPort) and self.get_element(cols + 1, 0).isInput:
                ld -= 1
                if (bidirectional == True):
                        st -= 1
            elif isinstance(self.get_element(cols + 1, 0), StreamPort) and not self.get_element(cols + 1, 0).isInput:
                st -= 1
            self.remove_element(cols + 1, 0)
        #if port_locations == 'all' or (isinstance(self.get_element(0, -rows - 1), StreamPort) and port_locations == 'bottom') or (isinstance(self.get_element(1, -rows), StreamPort) and port_locations == 'left'):
            if isinstance(self.get_element(0, -rows + 1), StreamPort) and self.get_element(0, -rows + 1).isInput:
                ld -= 1
                if (bidirectional == True):
                        st -= 1
            elif isinstance(self.get_element(0, -rows + 1), StreamPort) and not self.get_element(0, -rows + 1).isInput:
                st -= 1
            self.remove_element(0, -rows + 1)
        #if port_locations == 'all' or (isinstance(self.get_element(cols + 1, -rows), StreamPort) and port_locations == 'bottom') or (isinstance(self.get_element(cols, -rows), StreamPort) and port_locations == 'right'):
            if isinstance(self.get_element(cols + 1, -rows + 1), StreamPort) and self.get_element(cols + 1, -rows + 1).isInput:
                ld -= 1
                if (bidirectional == True):
                        st -= 1
            elif isinstance(self.get_element(cols + 1, -rows + 1), StreamPort) and not self.get_element(cols + 1, -rows + 1).isInput:
                st -= 1
            self.remove_element(cols + 1, -rows + 1)
        else:
            s, l = self.prune_stream_ports(bidirectional)         
            st += s
            ld += l

        if "STREAM_CONN 0" not in self.ic_export_flags:
            self.ic_export_flags.append("STREAM_CONN 0")
        
    def customize_PE(self, pe_or_pos, funct, color='#befe6c'):
        e = self.add_funct_to_PE(pe_or_pos, funct)
        e.set_color(color)
        return e

    def get_inputs(self, r):

        if r not in self.elements:
            raise ValueError(f"Resource {r} is not part of the mpa.")

        inputs = []
        for node in self.elements:
            if r in self.elements[node]:
                inputs.append(node)

        return inputs

    def get_num_tiles(self, tileType=ProcessingElement, spIsInput=True):
        num = 0
        for el in self.elements:
            if isinstance(el, tileType):
                if tileType != StreamPort:
                    num += 1
                if tileType == StreamPort and el.isInput == spIsInput:
                    num += 1
        return num

    # Interconnects

    def ic_grid_standard(self, latency=0, bidirectional=False):
        # Iterate through all pairs of elements and determine adjacency
        for elmt1 in self.elements:
            x1, y1 = self.pos[elmt1]
            for elmt2 in self.elements:
                if elmt1 == elmt2:
                    continue  # Skip self-loops
                
                x2, y2 = self.pos[elmt2]

                valid_interconnect = not(isinstance(elmt1, StreamPort) and isinstance(elmt2, StreamPort))
                valid_pos = (x2 == x1 + 1 and y2 == y1) or (x2 == x1 and y2 == y1 - 1)
                valid_interconnect = valid_interconnect and valid_pos      

                if valid_interconnect:
                    self.add_interconnect(elmt1, elmt2, latency, bidirectional)
                    if bidirectional:
                        self.add_interconnect(elmt2, elmt1, latency, bidirectional)

        flag = f"LEFT_TO_RIGHT {latency}"
        if flag not in self.ic_export_flags:
            self.ic_export_flags.append(flag)
            if bidirectional:
                self.ic_export_flags.append(f"RIGHT_TO_LEFT {latency}")

        flag = f"UP_TO_DOWN {latency}"
        if flag not in self.ic_export_flags:
            self.ic_export_flags.append(flag)
            if bidirectional:
                self.ic_export_flags.append(f"DOWN_TO_UP {latency}")

    def ic_grid_diagonals(self, latency=0, bidirectional=False):
        # Iterate through all pairs of elements and determine adjacency
        for elmt1 in self.elements:
            x1, y1 = self.pos[elmt1]
            for elmt2 in self.elements:
                if elmt1 == elmt2:
                    continue  # Skip self-loops
                
                x2, y2 = self.pos[elmt2]
                
                valid_interconnect = not(isinstance(elmt1, StreamPort) and isinstance(elmt2, StreamPort))
                valid_pos = (x2==x1+1 and y2==y1) or (x2==x1 and y2==y1-1) or (x2==x1+1 and y2==y1-1)
                valid_interconnect = valid_interconnect and valid_pos    

                if valid_interconnect:
                    self.add_interconnect(elmt1, elmt2, latency, bidirectional)
                    if bidirectional:
                        self.add_interconnect(elmt2, elmt1, latency, bidirectional)

        flag = f"LEFT_TO_RIGHT {latency}"
        if flag not in self.ic_export_flags:
            self.ic_export_flags.append(flag)
            if bidirectional:
                self.ic_export_flags.append(f"RIGHT_TO_LEFT {latency}")

        flag = f"UP_TO_DOWN {latency}"
        if flag not in self.ic_export_flags:
            self.ic_export_flags.append(flag)
            if bidirectional:
                self.ic_export_flags.append(f"DOWN_TO_UP {latency}")
        
        flag = f"DIAGONAL_NW {latency}"
        if flag not in self.ic_export_flags:
            self.ic_export_flags.append(flag)
            if bidirectional:
                self.ic_export_flags.append(f"DIAGONAL_SW {latency}")
                self.ic_export_flags.append(f"DIAGONAL_NE {latency}")
                self.ic_export_flags.append(f"DIAGONAL_SE {latency}")

    def ic_grid_full(self, latency=0):
        # Iterate through all pairs of elements and determine adjacency
        for elmt1 in self.elements:
            x1, y1 = self.pos[elmt1]
            for elmt2 in self.elements:
                if elmt1 == elmt2:
                    continue  # Skip self-loops
                
                x2, y2 = self.pos[elmt2]
                
                valid_interconnect = not(isinstance(elmt1, StreamPort) and isinstance(elmt2, StreamPort))
                valid_pos = (x2==x1+1 and y2==y1) or (x2==x1 and y2==y1-1) or (x2==x1+1 and y2==y1-1) or (x2==x1-1 and y2==y1-1)
                valid_interconnect = valid_interconnect and valid_pos                  
                
                if valid_interconnect:
                    self.add_interconnect(elmt1, elmt2, latency, True)
                    self.add_interconnect(elmt2, elmt1, latency, True)

        flag = f"LEFT_TO_RIGHT {latency}"
        if flag not in self.ic_export_flags:
            self.ic_export_flags.append(flag)
            self.ic_export_flags.append(f"RIGHT_TO_LEFT {latency}")

        flag = f"UP_TO_DOWN {latency}"
        if flag not in self.ic_export_flags:
            self.ic_export_flags.append(flag)
            self.ic_export_flags.append(f"DOWN_TO_UP {latency}")
        
        flag = f"DIAGONAL_NW {latency}"
        if flag not in self.ic_export_flags:
            self.ic_export_flags.append(flag)
            self.ic_export_flags.append(f"DIAGONAL_SW {latency}")
            self.ic_export_flags.append(f"DIAGONAL_NE {latency}")
            self.ic_export_flags.append(f"DIAGONAL_SE {latency}")

    def ic_horizontal(self, latency=0, bidirectional=False):
        # Iterate through all pairs of elements and determine adjacency
        for elmt1 in self.elements:
            x1, y1 = self.pos[elmt1]
            for elmt2 in self.elements:
                if elmt1 == elmt2:
                    continue  # Skip self-loops
                
                x2, y2 = self.pos[elmt2]

                valid_interconnect = not(isinstance(elmt1, StreamPort) and isinstance(elmt2, StreamPort))
                valid_pos = (x2 == x1 + 1 and y2 == y1)
                valid_interconnect = valid_interconnect and valid_pos      

                if valid_interconnect:
                    self.add_interconnect(elmt1, elmt2, latency, bidirectional)
                    if bidirectional:
                        self.add_interconnect(elmt2, elmt1, latency, bidirectional)

        flag = f"LEFT_TO_RIGHT {latency}"
        if flag not in self.ic_export_flags:
            self.ic_export_flags.append(flag)
            self.ic_export_flags.append(f"RIGHT_TO_LEFT {latency}")

    def ic_vertical(self, latency=0, bidirectional=False):
            # Iterate through all pairs of elements and determine adjacency
            for elmt1 in self.elements:
                x1, y1 = self.pos[elmt1]
                for elmt2 in self.elements:
                    if elmt1 == elmt2:
                        continue  # Skip self-loops
                    
                    x2, y2 = self.pos[elmt2]

                    valid_interconnect = not(isinstance(elmt1, StreamPort) and isinstance(elmt2, StreamPort))
                    valid_pos = (x2 == x1 and y2 == y1 - 1)
                    valid_interconnect = valid_interconnect and valid_pos      

                    if valid_interconnect:
                        self.add_interconnect(elmt1, elmt2, latency, bidirectional)
                        if bidirectional:
                            self.add_interconnect(elmt2, elmt1, latency, bidirectional)

            flag = f"UP_TO_DOWN {latency}"
            if flag not in self.ic_export_flags:
                self.ic_export_flags.append(flag)
                self.ic_export_flags.append(f"DOWN_TO_UP {latency}")

    def add_wrap_around_interconnects(self, latency=0, side='all'):
        if side == 'all' or side == 'LR' or side == 'lr' or side == 'left-right':
            self.ic_export_flags.append(f"WRAP_AROUND_LR {latency}")
        if side == 'all' or side == 'RL' or side == 'rl' or side == 'right-left':
            self.ic_export_flags.append(f"WRAP_AROUND_RL {latency}")
        if side == 'all' or side == 'UD' or side == 'ud' or side == 'up-down':
            self.ic_export_flags.append(f"WRAP_AROUND_UD {latency}")
        if side == 'all' or side == 'DU' or side == 'du' or side == 'down-up':
            self.ic_export_flags.append(f"WRAP_AROUND_DU {latency}")

    def size(self):
        return len(self.elements)  

    # Display the MPA
    def plot(self):
        # Create a NetworkX DiGraph to store the MPA as a directed graph
        G = nx.DiGraph()

        for element in self.elements:
            G.add_node(element)

        for e1, interconnects in self.elements.items():
            for e2, latency in interconnects.items():
                G.add_edge(e1, e2, weight=latency)

        # Assign grid-like positions to the nodes manually (assuming a simple 2x2 grid here)
        positions = self.pos
        
        # Set node colors and sizes
        node_colors = []
        for node in G.nodes:
            if (isinstance(node, ProcessingElement) or isinstance(node, StreamPort)):
                node_colors.append(node.get_color())
            else:
                node_colors.append('#d3d3d3')

        # Set a fixed size for the plot (adjust the width and height as needed)
        plt.figure(figsize=(12, 12))  # Set the figure size to 8x6 inches

        # Draw the graph
        nx.draw(G, pos=positions,
                with_labels=True,
                node_color=node_colors,
                node_shape='s',
                node_size=1000,
                font_size=10,
                font_color='black',
                arrows=True)

        # Add edge labels (latency values)
        edge_labels = nx.get_edge_attributes(G, 'weight')
        nx.draw_networkx_edge_labels(G, pos=positions, edge_labels=edge_labels)

        # Display the plot
        #plt.title("MultiProcessor Aggregate (MPA) Graph")
        plt.show()

    # Export the file
    def export(self, filename="design"):
        
        filename += ".cmpa"

        try:
            with open(filename, 'w') as file:

                L = 0
                C = 0

                for pe in self.elements:
                    x, y = self.get_coord(pe)
                    L = max(L, abs(y))
                    C = max(C, x)
                L += 1
                C += 1

                matrix = [[0 for _ in range(C)] for _ in range(L)]
                for pe in self.elements:
                    x, y = self.get_coord(pe)
                    matrix[abs(y)][x] = pe

                # Header and Matrix
                file.write(f"{L} {C} {self.se_ld_bw} {self.se_st_bw} {self.data_width}\n")
                for i in range(0, L):
                    for j in range(0, C):
                        if isinstance(matrix[i][j], StreamPort):
                            if matrix[i][j].isInput and not matrix[i][j].isOutput:
                                file.write("-2 ")
                            elif matrix[i][j].isOutput and not matrix[i][j].isInput:
                                file.write("-3 ")
                            elif matrix[i][j].isInput and matrix[i][j].isOutput:
                                file.write("-4 ")
                            else:
                                file.write("-1 ")
                        elif isinstance(matrix[i][j], ProcessingElement): # element is a PE
                            file.write(f"{len(matrix[i][j].operations)} ")
                        else: # No element at this position
                            file.write("-1 ")    
                    file.write("\n")
                
                # PE Register Files and Pipeline Stages
                for i in range(0, L):
                    for j in range(0, C):
                        if isinstance(matrix[i][j], ProcessingElement):
                            rfreadportsToFUs = 0
                            rfreadportsToORs = 0
                            if isinstance(matrix[i][j].registerFile, RegisterFile):
                                rfreadportsToFUs = matrix[i][j].registerFile.ports.get('FU', 0)
                            if isinstance(matrix[i][j].registerFile, RegisterFile):
                                rfreadportsToORs = matrix[i][j].registerFile.ports.get('OutputRegisters', 0)
                            file.write(f"{i} {j} {matrix[i][j].n_output_registers} {matrix[i][j].registerFile.size} {rfreadportsToFUs} {rfreadportsToORs} {matrix[i][j].constant_units} {matrix[i][j].pipeline_stages}\n")

                # Operations supported by each PE
                for i in range(0, L):
                    for j in range(0, C):
                        if isinstance(matrix[i][j], ProcessingElement):
                            #file.write(f"{i} {j} {matrix[i][j].registerFile_size}")
                            for k in range(0, len(matrix[i][j].operations)):
                                file.write(f"{i} {j} {matrix[i][j].operations[k]}\n")
                            #    file.write(f" {matrix[i][j].operations[k]}")
                            #file.write("\n")

                # Interconnect Configurations
                for config in self.ic_export_flags:
                    file.write(f"{config}\n")

                # Write the content you want to export
            print(f"Data successfully exported to {filename}")
        except IOError as e:
            print(f"Failed to write to file: {e}")

def init_empty_MPA(rows, cols, useStreams=False):
    mpa = MultiProcessorAggregate()

    for i in range(rows):
        if useStreams:
            mpa.add_element(StreamPort(True), 0, i) # Stream In
        for j in range(cols):
            mpa.add_element(ProcessingElement(), j + useStreams, i) # PEs
        if useStreams:
            mpa.add_element(StreamPort(False), cols + 1, i) # Stream Out

    if useStreams:
        mpa.ic_export_flags.append("STREAM_CONN 0")

    return mpa

def init_standard_MPA(rows, cols, latency=1, diagonals=False, StreamPorts='None', set_default_mem_thrghpts = True, ld_thrghpt=1, st_thrghpt=1):
    mpa = MultiProcessorAggregate()

    for i in range(rows):
        for j in range(cols):
            mpa.add_element(ProcessingElement(), j, i) # PEs

    if diagonals:
        mpa.ic_grid_diagonals(latency)
    else:
        mpa.ic_grid_standard(latency)

    if StreamPorts != 'None':
        mpa.add_stream_ports(StreamPorts, latency)

    return mpa

def init_standard_cgra(rows, cols, latency=1, diagonals=False, StreamPorts='None', mergeIOs=False):
    mpa = MultiProcessorAggregate()

    for i in range(rows):
        for j in range(cols):
            mpa.add_element(ProcessingElement(), j, i) # PEs

    if diagonals:
        mpa.ic_grid_diagonals(latency, bidirectional=True)
    else:
        mpa.ic_grid_standard(latency, bidirectional=True)

    if StreamPorts != 'None':
        mpa.add_stream_ports(StreamPorts, latency, bidirectional=True, mergeIOs=mergeIOs)

    return mpa

def init_standard_full(rows, cols, latency=1, useStreams=False):
    mpa = MultiProcessorAggregate()

    for i in range(rows):
        if useStreams:
            mpa.add_element(StreamPort(True), 0, -i) # Stream In
        for j in range(cols):
            mpa.add_element(ProcessingElement(), j + useStreams, -i) # PEs
        if useStreams:
            mpa.add_element(StreamPort(False), cols + 1, -i) # Stream Out

    mpa.ic_grid_full(latency)

    if useStreams:
        mpa.ic_export_flags.append("STREAM_CONN 0")

    return mpa

def init_homogeneous_MPA(rows, cols, procElem, latency=1, diagonals=False, StreamPorts='None'):
    
    if not isinstance(procElem, ProcessingElement):
        return None
    
    mpa = MultiProcessorAggregate()

    for i in range(rows):
        for j in range(cols):
            mpa.add_element(copy.deepcopy(procElem), j, i) # PEs

    if diagonals:
        mpa.ic_grid_diagonals(latency)
    else:
        mpa.ic_grid_standard(latency)

    if StreamPorts != 'None':
        mpa.add_stream_ports(StreamPorts, latency)

    return mpa


def init_homogeneous_cgra(rows, cols, procElem, latency=1, diagonals=False, StreamPorts='None'):
    
    if not isinstance(procElem, ProcessingElement):
        return None
    
    mpa = MultiProcessorAggregate()

    for i in range(rows):
        for j in range(cols):
            mpa.add_element(copy.deepcopy(procElem), j, i) # PEs

    if diagonals:
        mpa.ic_grid_diagonals(latency, bidirectional=True)
    else:
        mpa.ic_grid_standard(latency, bidirectional=True)

    if StreamPorts != 'None':
        mpa.add_stream_ports(StreamPorts, latency, bidirectional=True)

    return mpa
