BASE_DIR=$(shell pwd)
INSTALL_DIR=${BASE_DIR}/llvm-install
CONFIG_LLVM=-G "Unix Makefiles" -DCMAKE_BUILD_TYPE="Debug" -DCMAKE_INSTALL_PREFIX=${INSTALL_DIR} -DLLVM_TARGETS_TO_BUILD="X86" -DCMAKE_CXX_FLAGS="-std=c++14 -D_GLIBCXX_USE_CXX11_ABI=0"
#--program-prefix=llvm-  --enable-llvm=${BASE_DIR}/llvm --enable-shared --enable-pic --enable-jit
all: llvm-objects

llvm:
	svn co https://llvm.org/svn/llvm-project/llvm/branches/release_50 llvm	&& \
	cd llvm/ &&           \
	patch -p0 < ../llvm5.patch &&           \
	cd tools/ &&           \
	svn co https://llvm.org/svn/llvm-project/cfe/branches/release_50 clang  &&  \
	cd ../.. &&    \
	cd llvm/projects && \
	svn co https://llvm.org/svn/llvm-project/compiler-rt/branches/release_50 compiler-rt && \
	cd ../..

clean:
	rm -rf llvm-install
	rm -rf llvm-objects

llvm-objects: llvm
	mkdir llvm-install && 	\
	mkdir llvm-objects &&			\
	cd llvm-objects &&			\
	cmake ${CONFIG_LLVM} ../llvm/ &&	\
	make -j16 &&		\
	make install
