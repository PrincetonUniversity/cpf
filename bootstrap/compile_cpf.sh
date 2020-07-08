if [ -z $LLVM_INSTALL_DIR ]
then
    echo "LLVM_INSTALL_DIR is not set! Abort"
    exit -1
fi

if [ -z $NOELLE_INSTALL_DIR ]
then
    echo "NOELLE_INSTALL_DIR is not set! Abort"
    exit -1
fi

mkdir ../llvm-liberty-objects
cd ../llvm-liberty-objects
cmake -DCMAKE_INSTALL_PREFIX="./Release" -DCMAKE_BUILD_TYPE=Release ../liberty
make -j32
make install
#cmake -DCMAKE_INSTALL_PREFIX="./Debug" -DCMAKE_BUILD_TYPE=Debug ../liberty
