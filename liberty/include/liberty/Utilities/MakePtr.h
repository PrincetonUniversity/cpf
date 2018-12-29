#ifndef LLVM_LIBERTY_UTILS_MAKE_POINTER_H
#define LLVM_LIBERTY_UTILS_MAKE_POINTER_H

namespace liberty
{

// Some iterators give references, some give pointers.
// This always gives a pointer.
template <class Type> static Type *MakePointer( Type *t ) { return t; }
template <class Type> static Type *MakePointer( Type &t ) { return &t; }

// This always gives a reference.
template <class Type> static Type &MakeReference( Type *t) { return *t; }
template <class Type> static Type &MakeReference( Type &t) { return t; }

}

#endif
