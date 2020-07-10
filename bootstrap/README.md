The bootstrap scripts for the CPF infrastructure.

## Steps

1. `cp Makefile.example Makefile`
2. Edit Makefile and configure whether to recompile LLVM and NOELLE, where to install, verbosity, etc
3. make all

## Components

### LLVM
Compile from scratch and genearte both debug and release version.
The debug version supports `opt -debug` and `opt -debug-only` options but is way slower.

### NOELLE
Compile from scratch and genearte both debug and release version.
Note that both NOELLE versions are compiled with `debug` mode on, but with different LLVM builds.

### CPF
Compile from scratch and genearte both debug and release version.
