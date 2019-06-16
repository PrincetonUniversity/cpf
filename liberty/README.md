# Collaborative Analysis Framework (CPF) Code

## Build

#### Setup Environment Variables

```
export LLVM_SRC_ROOT=/path/to/llvm/
export LLVM_OBJ_DIR=/path/to/llvm-objects/
export LLVM_INSTALL_DIR=/path/to/llvm-install/

export LIBERTY_SRC_DIR=/path/to/cpf/liberty
export LIBERTY_OBJ_DIR=$LIBERTY_SRC_DIR/../llvm-liberty-objects/
export LIBERTY_INCLUDE_DIR=$LIBERTY_SRC_DIR/include/
export LIBERTY_LIBS_DIR=$LIBERTY_OBJ_DIR/Debug+Asserts/lib/
export LIBERTY_SMTX_DIR=$LIBERTY_SRC_DIR/support/smtx/

export LD_LIBRARY_PATH=$LD_LIBRARY_PATH/:/$LLVM_INSTALL_DIR/lib:/$LIBERTY_LIBS_DIR/
```

#### Building LLVM
```
mkdir ~/llvm-workspace
cd ~/llvm-workspace
cp /path/to/cpf/llvm-install-tool/makefile .
cp /path/to/cpf/llvm-install-tool/llvm5.patch .
make
```
Following this llvm build the environment variables setup for llvm becomes:
```
export LLVM_SRC_ROOT=$HOME/llvm-workspace/llvm/
export LLVM_OBJ_DIR=$HOME/llvm-workspace/llvm-objects/
export LLVM_INSTALL_DIR=$HOME/llvm-workspace/llvm-install/
```

#### Building CPF
Adjust first NOELLEHEADERS in cpf/liberty/Makefile.common.in to the correct location
```
cd $LIBERTY_SRC_DIR/support/smtx
make
cd $LIBERTY_SRC_DIR
mkdir ../llvm-liberty-objects
cd ../llvm-liberty-objects
../liberty/configure --with-llvmsrc=$LLVM_SRC_ROOT --with-llvmobj=$LLVM_OBJ_DIR --prefix=$LLVM_INSTALL_DIR --exec-prefix=$LLVM_INSTALL_DIR --includedir=$LIBERTY_INCLUDE_DIR --disable-optimized
make -jX
```
  
  
## Remediators in CPF

### Non-Speculative Remediators

1. Conservative Reduction (ReduxRemed)
    *   Both scalar and memory reduction

2. Conservative Privatization (PrivRemed)
    *   Removes loop-carried false memory dependences on conservatively provable privitizable objects

3. Memory Versioning (MemVerRemed)
    *   Assumes privatized memory for each thread
    *   Removes loop-carried false memory dependences
    *   Stronger but more expensive than PrivRemed
    *   Process-based parallelization enforces this remediator

4. Counted Loop Detection (CountedIVRemed)
    *   Removes loop-carried register and control dependences related to induction variables on counted loops

5. TXIO (TXIORemed)
    *   I/O deferral
    *   Delays execution of instructions with side-effects, such as I/O operations

6. Conservative Loop Fission (LoopFissionRemed)
    *   Separate non-DOALL SCCs to an initial sequential stage (loop in this case)
    *   Only small sequential stages considered
    *   No usage of speculative remediators to achieve separation


### Speculative Remediators


7. Control Speculation (ControlSpecRemed)
    *   Based on edge profile info, removes control flow edges and considers all basic blocks dominated by these edges as speculatively dead
    *   Inexpensive runtime validation

8. Value Prediction (LoadedValuePred)
    *   Some load instructions always read a single, predictable value from memory
    *   Loop-Invariant Loaded-Value
    *   Inexpensive runtime validation

9. Header-phi prediction (HeaderPhiPredRemed)
    *   Some phi instructions have a predictable value. 
    *   Allows removal of loop-carried register dependeces
    *   Inexpensive runtime validation

10. Separation Speculation (LocalityRemed)
    *   Johnson et al. PLDI '12
    *   Separation speculation partitions a program's allocations into a few disjoint "families" of objects and speculatively assumes that every pointer in the program refers exclusively to objects in one family. Under this assumption, if two pointers reference distinct families, they cannot alias. 
    *   Relatively inexpensive runtime validation due to small number of families, allocation of objects in family-specific memory regions, runtime optimizations and thread-local checks (no communication among concurrent threads needed).
    *   Secondary speculation built on top of separation speculation:

        - Read-only speculation
            *   Read-only family
            *   Some memory objects are never modified but static dependence analysis is sometimes unable to prove this property
            *   Objects in the read-only family are only accessed by read-only memory operations. Thus, speculatively read-only memory operations never experience flow, anti or output dependences
            *   Apart from separation speculation validation, no other validation is required

        - Speculative accumulator expansion
            *   Compiler identifies accumulators as values which are repeatedly updated with an associative and commutative operator (a reduction) but whose intermediate values are otherwise unused within the loop. Static dependence sometimes fails to prove that every access to a given storage location is a reduction or that intermediate values are never used.
            *   The pointer-family assumption establishes that objects in the reduction family are only accessed by load-reduce-store sequences and consequently that intermediate values cannot be otherwise observed or modified. 
            *   Apart from separation speculation validation, no other validation is required

        - Speculative privatization
            *   Reuse of data structures with no flow dependences from one iteration to the other prevents parallelization due to anti or output dependences.
            *   Addressed by having a private copy of the data structure for each iteration
            *   Assumption: loads from certain private objects never read values stored during earlier iterations of the loop.
            *   Privatization criteria validated in two phases, one thread-local and one more expensive that requires communication among threads.

        - Object-lifetime speculation
            *   Short-lived objects
            *   Some objects allocated within a loop iteration are always deallocated before the end of that same iteration. Static dependence analysis often fails to identify this case.
            *   Loads from or stores to such objects cannot depend on memory accesses in other iterations. 
            *   Extra validation on top of separation speculation validation: object lifetime speculation must validate that short-lived objects never outlive their iteration. Thread-local checks.

11. Memory Flow Speculation (SmtxSlampRemed & SmtxLampRemed)
    *   Assumes the absence of flow dependences between memory operations when not manifested during profiling.
    *   Provides as much or more an enabling effect than many other types of speculation.
    *   Expensive validation. Requires communication among concurrent threads.

12. Speculative AA stack (MemSpecAARemed)
    *   Allows collaboration among memory flow speculation, control speculation, value prediction, speculative points-to analysis and static analysis.
    *   Demonstrates the power of collaboration among remediators, and between speculation techniques and static analysis.
    *   Provides at least as much coverage as all the remediators addressing mem deps combined (only excludes SLAMP mem spec and localityaa).

13. Speculative Loop Fission (LoopFissionRemed)
    *   Same as Conservative Loop Fission apart from the fact that it requires usage of speculative remediators to achieve separation

14. Pointer-Residue Speculation (Not added)
    *   Never part of a paper. Published in Nick Johnson's thesis
    *   Separation speculation disambiguates references to different objects, but does not disambiguate references within the same object. Pointer-residue speculation works at the sub-object level.
    *   It disambiguates different fields within an object and in some cases recognizes different regular strides across an array.
    *   It characterizes each pointer expression in the program according to the possible values of its four least-significant bits (residue).
    *   Examines whether residue sets are disjoint with respect to the size of the memory accesses of two given operations.
