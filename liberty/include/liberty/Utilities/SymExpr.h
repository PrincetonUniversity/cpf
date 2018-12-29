#ifndef LLVM_LIBERTY_SYMBOLIC_EXPRESSION_H
#define LLVM_LIBERTY_SYMBOLIC_EXPRESSION_H

#include "llvm/ADT/DenseMapInfo.h"
#include "llvm/ADT/DenseMap.h"

#include <cstring>

namespace liberty
{
  /// A symbolic expression is represented
  /// as a binary expression tree.
  /// These are managed---both in terms of
  ///
  struct SymExpr
  {
    enum OpType {
      SE_Imm=0, SE_Var, SE_Add, SE_Sub,
      SE_Mul, SE_Div, SE_Min,  SE_Max };

    const OpType ty;

    double imm;
    const char *name;
    const SymExpr *left,
                  *right;


    SymExpr(double imm);
    SymExpr(const char *);
    SymExpr(OpType op, const SymExpr *left, const SymExpr *right);
    ~SymExpr();

    /// Write the symbolic expression
    /// to the given output stream.
    /// We template it so it will work
    /// both on std::ostream and
    /// llvm::raw_ostream.
    template <class OStream>
    void print(OStream &fout) const
    {
      static const char *Names[] = {
        "imm", "var", " + ",   " - ",
        " * ", " / ", " min ", " max " };

      if( ty == SE_Imm )
        fout << imm;
      else if( ty == SE_Var )
        fout << name;
      else
      {
        fout << " (";
        left->print(fout);
        fout << Names[ty];
        right->print(fout);
        fout << ") ";
      }
    }

    void dump() const;

    /// Evaluate the symbolic expression
    /// down to a constant value.
    /// This asserts if
    /// the expression contains a
    /// variable that has not been
    /// replaced.
    double eval() const;
  };
}

//#include "llvm/Support/raw_ostream.h"

namespace llvm
{
  using namespace liberty;

  /// Allow us to use SymExpr* as hash-table keys
  template <>
  struct DenseMapInfo< SymExpr * >
  {
    static inline SymExpr *getEmptyKey() { return (SymExpr*) ~0UL; }
    static inline SymExpr *getTombstoneKey() { return (SymExpr*) ~1UL; }
    static inline bool isPOD() { return true; }
    static bool isEqual(const SymExpr *a, const SymExpr *b)
    {
      if( a == b )
        return true;

      if( a == getEmptyKey() || b == getEmptyKey() )
        return false;

      if( a == getTombstoneKey() || b == getTombstoneKey() )
        return false;

      if( a->ty != b->ty )
        return false;

      if( a->ty == SymExpr::SE_Imm )
        return a->imm == b->imm;

      if( a->ty == SymExpr::SE_Var )
      {
        bool res = std::strcmp(a->name, b->name) == 0;
//        llvm::errs() << "Compare " << a->name << " to " << b->name << " is " << res << ".\n";
        return res;
      }

      return a->left == b->left
          && a->right == b->right;
    }

    static unsigned getHashValue(const SymExpr *a)
    {
      if( a->ty == SymExpr::SE_Imm )
      {
        // 1049 is an arbitrary prime number
        return (unsigned) 1049U * a->imm;
      }
      else if( a->ty == SymExpr::SE_Var )
      {
        // don't use this; it hashes pointer values.
        //return DenseMapInfo<const char*>::getHashValue( a->name );
        unsigned hash = 0xabcd1234;
        for(unsigned i=0; a->name[i]; ++i)
          hash = (47U * hash + 97U * a->name[i]) >> 1;
//        errs() << "Hash(" << a->name << ") = " << hash << ".\n";
        return hash;
      }
      else
      {
        // we use the (void*) cast to avoid recursion;
        // i.e. hash the pointer values instead of
        // hashing SymExpr objects.  17,29,41 are
        // arbitrary prime numbers
        return 17U * DenseMapInfo<void*>::getHashValue( (void*)a->left )
             + 29U * DenseMapInfo<void*>::getHashValue( (void*)a->right )
             + 41U * DenseMapInfo<unsigned>::getHashValue( (unsigned) a->ty );
      }
    }
  };
}

namespace liberty
{
  using namespace llvm;

  class SymExprContext
  {
    typedef DenseMap<SymExpr*,SymExpr*> Canonicalizer;
    Canonicalizer canon;

    /// this may be used as a singleton
    static SymExprContext *theSingletonContext;

  protected:
    /// Try to find this symexpr if it already
    /// exists; if so, delete it and return the
    /// old one; otherwise, insert the new one.
    const SymExpr *tryLookup(SymExpr *s);

    /// Construct and canonicalize a binary op, where op is not commutative
    const SymExpr *nonCommutative(
      SymExpr::OpType op, const SymExpr *l, const SymExpr *r);

    /// Construct and canonicalize a binary op, where op is commutative
    const SymExpr *commutative(
      SymExpr::OpType op, const SymExpr *l, const SymExpr *r);

    /// Construct and canonicalize a binary op; op may or may not be commutative
    const SymExpr *cons(SymExpr::OpType op, const SymExpr *l, const SymExpr *r);


  public:
    ~SymExprContext();

    /// You may use the singleton pattern, if you
    /// wish.
    static SymExprContext &TheContext();

    const SymExpr * immediate(double);
    const SymExpr * variable(const std::string &s);
    const SymExpr * variable(const char *);


    const SymExpr * add(const SymExpr *, const SymExpr *);
    const SymExpr * sub(const SymExpr *, const SymExpr *);
    const SymExpr * mul(const SymExpr *, const SymExpr *);
    const SymExpr * div(const SymExpr *, const SymExpr *);
    const SymExpr * min(const SymExpr *, const SymExpr *);
    const SymExpr * max(const SymExpr *, const SymExpr *);

    /// Compute a new symbolic expression: orig[replacement/Var(pattern)]
    const SymExpr * replace(
      const SymExpr *orig, const char *pattern, const SymExpr *replacement);

    /// Compute a new symbolic expression: orig[replacement/pattern]
    const SymExpr * replace(
      const SymExpr *orig, const SymExpr *pattern, const SymExpr *replacement);
  };
}

#endif // LLVM_LIBERTY_SYMBOLIC_EXPRESSION_H
