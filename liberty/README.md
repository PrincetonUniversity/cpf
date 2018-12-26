# Collaborative Parallelization Framework (CPF)

### Build

#### Setup Enviroment Variables

```
export LLVM_SRC_ROOT=/path/to/llvm/
export LLVM_OBJ_DIR=/path/to/llvm-objects/
export LLVM_INSTALL_DIR=/path/to/llvm-install/

export LIBERTY_SRC_DIR=/path/to/cpf
export LIBERTY_OBJ_DIR=$LIBERTY_SRC_DIR/../llvm-liberty-objects/
export LIBERTY_INCLUDE_DIR=$LIBERTY_SRC_DIR/include/
export LIBERTY_LIBS_DIR=$LIBERTY_OBJ_DIR/Debug+Asserts/lib/

export LD_LIBRARY_PATH=$LD_LIBRARY_PATH/:/$LLVM_INSTALL_DIR/lib:/$LIBERTY_LIBS_DIR/
```

#### Building the code

```
cd cpf
mkdir ../llvm-liberty-objects
cd ../llvm-liberty-objects
../cpf/configure --with-llvmsrc=$LLVM_SRC_ROOT --with-llvmobj=$LLVM_OBJ_DIR --prefix=$LLVM_INSTALL_DIR --exec-prefix=$LLVM_INSTALL_DIR --includedir=$LIBERTY_INCLUDE_DIR
make
```
