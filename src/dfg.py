import networkx as nx
import matplotlib.pyplot as plt
import matplotlib.patches as mpatches

class instr:
    def __init__(self, id, op, lat, val=0, name=""):
        self.id = id
        self.op = op
        self.lat = lat
        self.const_val = val
        self.name = name

    def get_id(self):
         return self.id

    def get_op(self):
         return self.op

    def get_lat(self):
         return self.lat
    
    def get_name(self):
        return self.name
    

class DataflowGraph:
    def __init__(self):
        self.instrs = {}
        self.recurrences = {} # for each recurrence, store the distance

    def add_instr(self, instr):
         self.instrs[instr] = {}
         self.recurrences[instr] = []

    def add_recurrence(self, instr, recurrence, distance):
        self.recurrences[instr].append((recurrence, distance))

    def add_dependency(self, i1, i2, weight=0, bidirectional=False):
        if i1 not in self.instrs:
            self.add_instr(i1)
        if i2 not in self.instrs:
            self.add_instr(i2)
        self.instrs[i1][i2] = weight
        if bidirectional:
            self.instrs[i2][i1] = weight

    def size(self):
        return len(self.instrs)
    
    def get_num_recurrences(self, instr):
        return len(self.recurrences[instr])
    
    def get_inputs(self, instr):
        # Find all predecessors of the given instruction
        inputs = []
        for src, targets in self.instrs.items():
            if instr in targets:
                inputs.append(src)
        return inputs
    
    def get_outputs(self, instr):
        # Find all successors (outputs) of the given instruction
        outputs = []
        if instr in self.instrs:
            outputs = list(self.instrs[instr].keys())  # All nodes to which the current node points
        return outputs
    
    def sort_and_renumber_instructions(self):
        """
        Sort the instructions so that all non-constants appear first and constants come last.
        Then, reassign the instruction IDs to reflect the new order:
        - The first instruction gets ID 1,
        - The second gets ID 2, and so on.
        
        Returns:
            A list of the sorted instructions.
        """

        # Sort by (is_CONST, original id) so that non-CONST (False) come before CONST (True).
        sorted_instrs = sorted(
            self.instrs.keys(),
            key=lambda instr: (instr.get_op() == "CONST", instr.get_id())
        )
        
        # Reassign IDs (and update the name if you wish to include the new ID)
        for new_id, instr_obj in enumerate(sorted_instrs, start=1):
            instr_obj.id = new_id
            # Optionally, update the name to reflect the new id:
            # If the original name is not set explicitly, use the op + new id.
            if not instr_obj.get_name() or instr_obj.get_name().startswith(instr_obj.get_op()):
                instr_obj.name = f"{instr_obj.get_op()}_{new_id}"
  
        return sorted_instrs


### For displaying the DFG #########################################################

    def plot(self):
        # Convert the adjacency list to a NetworkX graph
        G = nx.DiGraph()

        # Add nodes to the graph
        for instr in self.instrs:
            G.add_node(instr.get_id())

        # Add normal dependency edges to the graph
        for i1, dependencies in self.instrs.items():
            for i2, weight in dependencies.items():
                G.add_edge(i1.get_id(), i2.get_id(), weight=weight)

        # Set labels using node names
        labels = {node.get_id(): node.get_name() for node in self.instrs.keys()}

        # Map node colors based on their operation type
        colors = []
        for node in G.nodes:
            for instr_node in self.instrs.keys():
                if instr_node.get_id() == node:
                    if instr_node.get_op() in ("S_IN", "S_OUT"):
                        colors.append("#f0ff75")  # light yellow
                    elif instr_node.get_op() == "CONST":
                        colors.append('#9bff8b')
                    else:
                        colors.append("#94dbff")  # light blue

        # Assign layers to nodes based on topological sort (for better positioning)
        layers = {}
        for node in nx.topological_sort(G):
            predecessors = list(G.predecessors(node))
            if predecessors:
                layers[node] = max(layers[p] + 1 for p in predecessors)
            else:
                layers[node] = 0

        # Group nodes by layers and assign positions
        layers_grouped = {}
        for node, layer in layers.items():
            layers_grouped.setdefault(layer, []).append(node)

        pos = {}
        for layer, nodes in layers_grouped.items():
            y = -layer
            for i, node in enumerate(nodes):
                x = i - len(nodes) / 2 + 0.5
                pos[node] = (x, y)

        # Draw normal dependency edges and nodes using NetworkX's draw function
        nx.draw(G, pos=pos,
                with_labels=True,
                labels=labels,
                node_color=colors,
                node_size=1500,
                font_size=11,
                font_color='darkblue',
                arrows=True)

        # Draw recurrence edges with curved red arrows including arrow heads
        ax = plt.gca()
        for src_instr, rec_list in self.recurrences.items():
            src_id = src_instr.get_id()
            for rec_instr, distance in rec_list:
                target_id = rec_instr.get_id()
                start = pos[src_id]
                end = pos[target_id]
                arrow = mpatches.FancyArrowPatch(
                    posA=start,
                    posB=end,
                    label=distance,
                    connectionstyle="arc3,rad=0.2",
                    arrowstyle="Simple, tail_width=0.05, head_width=0.2, head_length=0.2",
                    color="red",
                    lw=2,
                    mutation_scale=20,
                    shrinkA=20,      # Move arrow tip away from the source node center
                    shrinkB=20,      # Move arrow tip away from the target node center
                    zorder=3
                )
                ax.add_patch(arrow)
                # Compute the midpoint for the label
                mid_x = (start[0] + end[0]) / 2
                mid_y = (start[1] + end[1]) / 2

                # Offset for better visibility
                label_offset = 0.05  # Adjust as needed
                text_x = mid_x + label_offset
                text_y = mid_y + label_offset

                # Add the label
                ax.text(text_x, text_y, str(distance), fontsize=12, color="black", 
                        ha='center', va='center', bbox=dict(facecolor='white', alpha=0.7, edgecolor='none'))

        plt.title("Graph Visualization")
        plt.show()

    # Export the file
    def export(self, filename="kernel"):
        
        filename += ".dfg"
        sorted_instrs = self.sort_and_renumber_instructions()
        try:
            with open(filename, 'w') as file:
                
                file.write(f"{len(self.instrs)}\n")

                # Write instructions
                for i in sorted_instrs:
                    ins = self.get_inputs(i)
                    outs = self.get_outputs(i)
                    recs = self.get_num_recurrences(i)
                    const_val = 0
                    consts = len([instr for instr in ins if instr.op == 'CONST'])
                    op = i.get_op()
                    name = i.get_name()
                    if op == 'S_IN':
                        op = 'STREAM_IN'
                    elif op == 'S_OUT':
                        op = 'STREAM_OUT'
                    elif op == 'CONST':
                        const_val = i.const_val
                    file.write(f"{name} {op} {i.get_lat()} {len(ins)} {len(outs)} {recs} {consts} {const_val}\n")
                    print(f"{name} {op} {i.get_lat()} {len(ins)} {len(outs)} {recs} {consts} {const_val}\n")


                for i in sorted_instrs:
                    ins = self.get_inputs(i)
                    ins += self.get_outputs(i)
                    for d in ins:
                        file.write(f"{d.get_id()} ")
                    file.write("\n")
                
                for i in sorted_instrs:
                    for r in self.recurrences[i]:
                        x, y = r
                        file.write(f"{i.get_id()} {x.get_id()} {y}\n")

                # Write the content you want to export
            print(f"Data successfully exported to {filename}")
        except IOError as e:
            print(f"Failed to write to file: {e}")


def create_dfg_from_file(filename):
    with open(filename, "r") as file:
        lines = file.readlines()

    # Parse instructions
    instructions = []
    for line in lines:
        line = line.strip()
        if line and ',' not in line:  # Detect instruction lines
            parts = line.split()
            if len(parts) == 3:  # Format: name op lat
                name, op, lat = parts
            elif len(parts) == 2:  # Format: op lat (no name)
                op, lat = parts
                name = ""
            else:
                print(f"Skipping invalid line: {line}")
                continue
            print(op)
            print(lat)
            print(name)
            lat = int(lat)
            instructions.append((name, op, lat))

    # Create `instr` objects
    instr_objects = [
    instr(i + 1, op, lat, name = op + str(i + 1) if name == "" else name) 
    for i, (name, op, lat) in enumerate(instructions)
]
    
    # Parse dependencies
    dependencies = []
    for line in lines:
        line = line.strip()
        if line and ',' in line and 'rec' not in line:  # Detect dependency lines
            name1, name2 = line.split(',')  # Read names
            name1, name2 = name1.strip(), name2.strip()
            dependencies.append((name1, name2))  # Store names instead of IDs

    # Build the DFG
    d = DataflowGraph()
    instr_dict = {i.get_name(): i for i in instr_objects}  # Map IDs to `instr` objects

    for i in instr_objects:
        d.add_instr(i)

    for i1, i2 in dependencies:
        d.add_dependency(instr_dict[i1], instr_dict[i2])


    for line in lines:
        line = line.strip()
        # Only process lines that start with "rec"
        if line.startswith("rec "):
            parts = line.replace('rec ','').split(',')
            if len(parts) == 3:
                # parts[0] is "rec", parts[1] is source instruction name,
                # parts[2] is target instruction name, parts[3] is the distance.
                source_name = parts[0].strip()
                target_name = parts[1].strip()
                try:
                    distance = int(parts[2].strip())
                except ValueError:
                    # Default to a distance of 1 if conversion fails.
                    distance = 1
                if source_name in instr_dict and target_name in instr_dict:
                    # Add the recurrence edge to the DFG.
                    d.add_recurrence(instr_dict[source_name],
                                       instr_dict[target_name],
                                       distance)
                else:
                    print(f"Warning: Unknown nodes in recurrence: {source_name}, {target_name}")
            else:
                print(f"Warning: Invalid recurrence format: {line}")

    return d

def create_dfg_from_cgra_me(filename):
    """
    Create a DataflowGraph from a DOT-like file representation,
    handling recurrence edges by detecting cycles.

    This function reads a Graphviz-format file where each node is defined as:
        nodename [opcode=op, constVal="..."];
    and each edge is defined as:
        source -> target [operand=...];

    If adding an edge from source to target would create a cycle in the dependency graph,
    the edge is stored as a recurrence edge (with a default distance of 1).
    Extra properties are ignored.
    """
    import networkx as nx

    with open(filename, 'r') as file:
        lines = file.readlines()
    
    dfg = DataflowGraph()
    instr_dict = {}
    
    # First pass: process node definitions.
    for line in lines:
        line = line.strip()
        if not line or line.startswith("digraph") or line in {"{", "}"}:
            continue
        if "->" not in line:
            # Process node line (e.g., "const0 [opcode=const, constVal="0"];")
            if line.endswith(";"):
                line = line[:-1]
            parts = line.split("[", 1)
            node_name = parts[0].strip()
            opcode = ""
            const_val = 0
            if len(parts) > 1:
                properties = parts[1].strip("]")  # Remove trailing bracket.
                props = {}
                for prop in properties.split(","):
                    if "=" in prop:
                        key, value = prop.split("=", 1)
                        props[key.strip()] = value.strip().strip('"')
                opcode = props.get("opcode", "")
                if opcode == 'input':
                    opcode = 'S_IN'
                elif opcode == 'output':
                    opcode = 'S_OUT'
                elif opcode == 'const':
                    # if it's a const node, try to get constVal
                    const_val_str = props.get("constVal", "0")
                    try:
                        const_val = int(const_val_str)
                    except ValueError:
                        const_val = 0  # fallback if parsing fails
            # Create an instruction with default latency of 1.
            new_instr = instr(node_name, opcode.upper(), 1, const_val, node_name)
            dfg.add_instr(new_instr)
            instr_dict[node_name] = new_instr
    
    # Create a temporary graph for cycle detection (using node names).
    G_temp = nx.DiGraph()
    for node_name in instr_dict.keys():
        G_temp.add_node(node_name)
    
    # Second pass: process dependency definitions.
    for line in lines:
        line = line.strip()
        if "->" in line:
            if line.endswith(";"):
                line = line[:-1]
            # Split the edge definition.
            parts = line.split("->")
            if len(parts) >= 2:
                src = parts[0].strip()
                # Extract target name (ignoring any properties).
                target_part = parts[1].strip()
                target_name = target_part.split()[0].split("[")[0].strip()
                
                if src not in instr_dict or target_name not in instr_dict:
                    print(f"Warning: Unknown node in dependency: {src} -> {target_name}")
                    continue
                
                # Check if adding an edge from src to target would create a cycle.
                if nx.has_path(G_temp, target_name, src):
                    # Adding this edge would close a loop: record as recurrence edge.
                    dfg.add_recurrence(instr_dict[src], instr_dict[target_name], 1)
                else:
                    # No cycle detected: add as a normal dependency.
                    dfg.add_dependency(instr_dict[src], instr_dict[target_name])
                    G_temp.add_edge(src, target_name)

    print(dfg.recurrences)
    
    return dfg
