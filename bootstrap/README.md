The bootstrap scripts for the CPF infrastructure.

## Installation

1. `cp Makefile.example Makefile`
2. Edit Makefile and configure whether to recompile LLVM and NOELLE, where to install, verbosity, etc
3. `make all` (this will start to compile and install with 32 threads)

## Use CPF

To use CPF, check the generated `.rc` files, modify the `CPF_BENCHMARK_ROOT` to your intended path to use the scripts.
Then, run `source cpf-env-release.rc` to use the release version of the toolchain, run `source cpf-env-debug.rc` to use the debug version of the toolchain.

## Components

### LLVM (Dependence)
The debug version supports `opt -debug` and `opt -debug-only` options but is way slower.

### NOELLE (Dependence)
Note that both NOELLE versions are compiled with `debug` mode on, but with different LLVM builds.

### CPF (This Repo)
Compile from scratch and genearte both debug and release version.
