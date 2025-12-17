import argparse
from pycparser import parse_file
from pycparser import c_ast

def find_loop_with_pragma(filename):
    ast = parse_file(filename, use_cpp=True, cpp_args=r'-I./fake_libc_include')
    
    class LoopFinder(c_ast.NodeVisitor):
        def __init__(self):
            self.target_loop = None
            self.pragma_found = False

        def visit_Pragma(self, node):
            if "DFGLoop loop" in node.string:
                self.pragma_found = True

        def visit_For(self, node):
            if self.pragma_found:
                self.target_loop = node
                self.pragma_found = False  # Reset for subsequent loops

    finder = LoopFinder()
    finder.visit(ast)
    return finder.target_loop

class SimplifiedDFGGenerator:
    def __init__(self):
        self.nodes = []
        self.edges = []
        self.var_counter = 0
        self.operations = {'+': 'add', '-': 'sub', '*': 'mul', '/': 'div', '>>': 'ashr', '<<': 'ashl'}
        self.node_map = {}  # Tracks computed values like "c_i"
        self.accumulators = {}  # Tracks all accumulators: {var_name: add_node}

    def new_name(self, prefix):
        self.var_counter += 1
        return f"{prefix}{self.var_counter}"

    def generate_simplified_dfg(self, loop):
        for stmt in loop.stmt.block_items:
            if isinstance(stmt, c_ast.Assignment):
                self._process_assignment(stmt)
        return self._generate_dot()

    def _process_assignment(self, stmt):
        if isinstance(stmt.lvalue, c_ast.ArrayRef):
            key = self._get_array_key(stmt.lvalue)
            rhs_node = self._handle_expression(stmt.rvalue)
            self.node_map[key] = rhs_node
            output_node = self._create_output_node(stmt.lvalue)
            self.edges.append(f"{rhs_node} -> {output_node};")
        elif isinstance(stmt.lvalue, c_ast.ID):
            var_name = stmt.lvalue.name
            rhs_node = self._handle_expression(stmt.rvalue)
            self._connect_accumulator(var_name, rhs_node, stmt.op)

    def _connect_accumulator(self, var_name, rhs_node, op):
        if var_name not in self.accumulators:
            # Initialize new accumulator
            add_node = self.new_name(self.operations[op[:-1]])
            self.accumulators[var_name] = add_node
            self.nodes.append(f"{add_node} [opcode=add];")
            
            # Initial value and self-loop
            self.edges.append(f"{add_node} -> {add_node} [operand=0];")  # Self-loop
            
            # Output node
            self.nodes.append(f"{var_name}_output [opcode=output];")
            self.edges.append(f"{add_node} -> {var_name}_output;")
        
        # Connect the new value to operand 1 of the add node
        add_node = self.accumulators[var_name]
        self.edges.append(f"{rhs_node} -> {add_node} [operand=1];")


    def _handle_expression(self, expr):
        if isinstance(expr, c_ast.BinaryOp):
            return self._handle_binop(expr)
        elif isinstance(expr, c_ast.ArrayRef):
            key = self._get_array_key(expr)
            # Use existing value from node_map if available
            return self.node_map.get(key, self._create_input_node(key))
        elif isinstance(expr, c_ast.Constant):
            return self._handle_constant(expr)
        return None

    def _get_array_key(self, array_ref):
        """Generate keys like 'A1_ii_i' for A1[ii][i]"""
        # Traverse nested ArrayRefs to get all indices
        indices = []
        current = array_ref
        while isinstance(current, c_ast.ArrayRef):
            indices.append(self._get_index_name(current.subscript))
            current = current.name
        array_name = current.name
        indices.reverse()
        return f"{array_name}_{'_'.join(indices)}"

    def _get_index_name(self, subscript):
        """Parse subscript expressions (e.g., i, i+1) into strings"""
        if isinstance(subscript, c_ast.ID):
            return subscript.name
        elif isinstance(subscript, c_ast.BinaryOp):
            left = self._get_index_name(subscript.left)
            right = self._get_index_name(subscript.right)
            if not (subscript.op == '+'):
                return f"{left}_{subscript.op}{right}"
            return f"{left}_{right}"
        elif isinstance(subscript, c_ast.Constant):
            return subscript.value
        return "unknown"

    def _create_input_node(self, array_ref):
        """Create an input node only if the array element isn't computed."""
        #array_name = array_ref.name.name
        #index = self._get_index_name(array_ref.subscript)
        #node_name = f"{array_name}_{index}"
        node_name = array_ref
        self.nodes.append(f"{node_name} [opcode=input];")
        return node_name

    def _create_output_node(self, array_ref):
        """Create an output node for array stores with proper subscript formatting."""
        key = self._get_array_key(array_ref)
        node_name = f"{key}_out"
        self.nodes.append(f"{node_name} [opcode=output];")
        return node_name

    def _handle_binop(self, expr):
        op = self.operations.get(expr.op, 'unknown')
        left = self._handle_expression(expr.left)
        right = self._handle_expression(expr.right)
        node_name = self.new_name(op)
        self.nodes.append(f"{node_name} [opcode={op}];")
        self.edges.append(f"{left} -> {node_name};")
        self.edges.append(f"{right} -> {node_name};")
        return node_name

    def _handle_constant(self, expr):
        const_node = self.new_name("const")
        self.nodes.append(f"{const_node} [opcode=const, constVal={expr.value}];")
        return const_node

    def _generate_dot(self):
        # Collect all used nodes from edges
        used_nodes = set()
        for edge in self.edges:
            if ' -> ' not in edge:
                continue
            src, rest = edge.split(' -> ', 1)
            src = src.strip()
            if ' [' in rest:
                tgt = rest.split(' [', 1)[0].strip()
            else:
                tgt = rest.split(';', 1)[0].strip()
            used_nodes.add(src)
            used_nodes.add(tgt)

        # Filter nodes to keep only those with connections
        filtered_nodes = []
        for node in set(self.nodes):  # Remove duplicates
            node_name = node.split(' [', 1)[0].strip()
            if node_name in used_nodes:
                filtered_nodes.append(node)

        filtered_nodes = sorted(filtered_nodes)
        filtered_nodes = sorted(filtered_nodes, key=lambda s: (0 if s.startswith('const') else 1))

        # Generate DOT content
        dot = "digraph G {\n"
        dot += "\n".join(filtered_nodes) + "\n"
        dot += "\n".join(sorted(set(self.edges))) + "\n}"
        return dot

# Set up argument parsing
parser = argparse.ArgumentParser(description="Process a DFG file and create a DFG object.")
parser.add_argument("filepath", type=str, help="Path to the DFG file")
args = parser.parse_args()

loop = find_loop_with_pragma(args.filepath)  # Reuse previous AST parsing

generator = SimplifiedDFGGenerator()
dot_content = generator.generate_simplified_dfg(loop)
with open("simplified_loop.dot", "w") as f:
    f.write(dot_content)