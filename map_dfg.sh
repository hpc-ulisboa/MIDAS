if [ "$#" -ne 1 ]; then
    echo "Usage: $0 <DFG dot file>"
    exit 1
fi

python3 src/dfg_parser.py "$1"

./midas scripts/default.mcl