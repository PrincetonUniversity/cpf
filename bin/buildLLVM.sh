#!/bin/bash

# Fetch the inputs
if test $# -lt 1 ; then
  echo "USAGE: `basename $0` INSTALL_DIR" ;
  exit 1;
fi
instDir="`realpath $1`" ;

# Create the installation directory
origDir=`pwd` ;
mkdir -p "$instDir" ;
cd $instDir ;

mkdir llvm-workspace
cd llvm-workspace
cp "${origDir}"/llvm-install-tool/makefile .
cp "${origDir}"/llvm-install-tool/llvm5.patch .
make
