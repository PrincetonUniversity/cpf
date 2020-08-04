Updated on Mar 8, 2019
Ziyang Xu
Princeton University, Liberty Research Group

Scripts to automate all the profilings.

## File Description

Main Scripts:

- get\_results.py : Automatic do all profilings and experiment, dump a json, post results to Slack and  generate a detailed CSV report
- requirements.txt : Required libraries for Python 3
- pact19\_bmark\_list.json/bmark\_list.json : Benchmark list, required for automatic script

Helpers:

- test\_slamp.sh : Check if the SLAMP profile can be loaded
- regressions-watchdog : Limit time, memory usage of a profiling instance
- devirtualize : Do devirtualization (for indirect function call)
- aa : Alias analysis

Passes:

- lamp-profile : Do LAMP profiling
- loop-profile : Do Loop profiling
- slamp-driver : Do SLAMP profiling
- specpriv-profile : Do SpecPriv profiling
- collaborative-pipeline : Do the experiment

## Setup

1. Export necessary environment variables.
    
    `source scripts/cpf_environ.rc`

2. Make sure the benchmark list json file (`pact19_bmark_list.json` for now) is correct and up to date.

3. Set up a virtual environment for Python 3.

    ```
    cd ~
    mkdir ve_py3
    # Make sure virtualenv is installed.
    virtualenv -p /usr/bin/python3 ve_py3
    source ve_py3/bin/activate
    # Now you are in the virtual environment, remember the path, you need to source this every time
    cd ~/CPF_Benchmarks/scripts
    pip install -r requirements.txt
    ```

4. Run get\_results.py to generate report; get help from get\_results.py -h.

    For example, following command will run tests on PolyBench(-s PolyBench), using 8 processes(-n 8), and will remove old results(-r)

    `python get_results.py -p ${CPF_ROOT} -b pact19_bmark_list.json -n 8 -s PolyBench -r`
    
    While following command will run tests on 2mm and 052.alvinn, using 2 processes, and don't remove old results.

    `python get_results.py -p ${CPF_ROOT} -b pact19_bmark_list.json -n 2 -c -l 2mm 052.alvin`

