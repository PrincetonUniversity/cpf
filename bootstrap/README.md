The bootstrap scripts for the CPF infrastructure.

## Installation

1. `cp Makefile.example Makefile`
2. Edit Makefile and configure whether to recompile LLVM and NOELLE, where to
   install, verbosity, etc
3. `make all` (this will start to compile and install with 32 threads)
4. If a target failed and the verbose mode was not turned on, check out
   TARGET.log (e.g., llvm.log) under `install-prefix`.

## Develop CPF

### Recompile under Existing Build 
To develop CPF and overwrite the existing copy, first compile and install CPF
and all its dependences following the installation section.  After the initial
installation, after modifying CPF, cd to `install-prefix` you've set, and run
`make cpf` with the same Makefile will update CPF.

### Make a New Copy of CPF
To make a second copy of CPF libraries without recompiling LLVM and NOELLE,
modify Makefile to skip compiling LLVM and NOELLE by specifyin `compile-llvm=0`
and `compile-noelle=0` and also specify all installed directories. Change
`install-prefix` in the same Makefile to a new directory. Then run `make all`,
this will compile and install CPF under `install-prefix`, and most importantly,
also generate all the .rc files with the correct LLVM and NOELLE environment.
directory of LLVM and NOELLE. You can then cd to `install-prefix` and update
CPF by `make cpf` if you modify your code under `cpf-root-path`.

### Add New Modules
To add a new module in CPF, for example, a new FOO directory under
`liberty/lib`, first, modify `liberty/lib/CMakeLists.txt` to add a new
subdirectory, second, create a new CMakeLists.txt under `liberty/lib/FOO`
following an existing one under other modules. Finally, compile the code by
doing `make cpf` under `install-prefix` or make a new copy (follow the previous
steps)

## Use CPF

To use CPF, check the generated `.rc` files under `install-prefix` that you
specified in the Makefile, modify the `CPF_BENCHMARK_ROOT` to your intended
path to use the scripts. (#FIXME) Then, run `source cpf-env-release.rc` to use
the release version of the toolchain, run `source cpf-env-debug.rc` to use the
debug version of the toolchain.

## Components

### LLVM (Dependence)
The debug version supports `opt -debug` and `opt -debug-only` options but is way slower.

### NOELLE (Dependence)
Note that both NOELLE versions are compiled with `debug` mode on, but with different LLVM builds.

### CPF (This Repo)
Compile from scratch and genearte both debug and release version.
