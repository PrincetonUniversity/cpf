#ifndef LLVM_LIBERTY_PREDICATE_H
#define LLVM_LIBERTY_PREDICATE_H

namespace liberty
{

  template <class Domain>
  struct Predicate
  {
    virtual ~Predicate() {}
    virtual bool operator()(const Domain &) = 0;
  };


  template <class Domain>
  struct Relation
  {
    virtual ~Relation() {}
    virtual bool operator()(const Domain &, const Domain &) = 0;
  };

  template <class Domain>
  struct FcnPtrPredicate : public Predicate<Domain>
  {
    typedef bool (*FcnPtr)(const Domain &);

    FcnPtrPredicate(FcnPtr ptr) : fcn(ptr) {}

    bool operator()(const Domain &d) { return fcn(d); }

  private:
    FcnPtr    fcn;
  };

  template <class Domain>
  struct TruePredicate : public Predicate<Domain>
  {
    bool operator()(const Domain &d) { return true; }
  };

  template <class Domain>
  struct FalsePredicate : public Predicate<Domain>
  {
    bool operator()(const Domain &d) { return false; }
  };

  template <class Domain, class AssociativeCollection>
  struct MembershipPredicate : public Predicate<Domain>
  {
    MembershipPredicate(const AssociativeCollection &co) : collection(co) {}
    bool operator()(const Domain &d) { return collection.find(collection) != collection.end(); }

  private:
    const AssociativeCollection &collection;
  };

  template <class Domain>
  struct FcnPtrRelation : public Relation<Domain>
  {
    typedef bool (*FcnPtr)(const Domain &, const Domain &);

    FcnPtrRelation(FcnPtr ptr) : fcn(ptr) {}

    bool operator()(const Domain &a, const Domain &b) { return fcn(a,b); }

  private:
    FcnPtr    fcn;
  };

}

#endif

