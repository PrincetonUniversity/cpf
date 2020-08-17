#!/bin/sh
clear
echo "Running exe"
echo "Add your cpf-benchmark path to BENCHMARK_DIR"
python get_results.py -p $BENCHMARK_DIR -b regression_list.json -s All <<EOF
0
y
test check
EOF
echo "run done"
exit 0
