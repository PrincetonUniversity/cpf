This directory contains a limited set of benchmarks and scripts for regression testing.

## Benchmark Directory Structure

- `./scripts` directory contains scripts to automate profiling and experiments for individual benchmark
- `./Makefile.generic` file specifies general targets for all benchmarks
- `./regression/{benchmark_name}` directory contains source code, inputs, outputs, and config files for a regression benchmark

## Benchmark Structure

- `./execInfo` specifies input arguments for profiling and different experiments
    * PROFILEARGS is used for profiling (short inputs)
    * TESTARGS is used for artifact experiments (long inputs)
    * LARGETESTARGS is used for the experiments in the paper (longest inputs)
    * SETUP, PROFILESETUP, TESTSETUP are used to set up inputs for corresponding experiments
    * CLEANUP, PROFILECLEANUP, TESTCLEANUP are used to clean up inputs for corresponding experiments
- `./input*` directories contain different inputs for a benchmark
- `./src` contains source files and Makefile for a benchmark
