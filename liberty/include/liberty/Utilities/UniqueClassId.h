#ifndef LLVM_LIBERTY_UNIQUE_CLASS_ID_H
#define LLVM_LIBERTY_UNIQUE_CLASS_ID_H

namespace liberty
{
  /// Every time this class is instantiated, it
  /// grabs a unique id.  This is especially
  /// useful for static variables/fields of this type.
  class UniqueId
  {
    const static unsigned FirstId = 100;
    static unsigned NextId;
    const unsigned MyId;
  public:
    /// \todo Extend for multiple inheritance by returning the next prime.
    UniqueId() : MyId( NextId++ ) {}

    unsigned get() const { return MyId; }
  };


  /// States that a class has a getClassId() method.
  /// You should inherit from this class with
  /// 'public virtual' inheritance.
  /// This is here so that an abstract base class
  /// supports class identification, while concrete
  /// subclasses implement UniqueClassId<> to grab
  /// a unique id.
  class HasClassId
  {
  public:
    virtual ~HasClassId() {}

    virtual unsigned getClassId() const = 0;
  };

  /// llvm is compiled without RTTI.
  /// This is both good and bad.  When you need RTTI,
  /// you need to write it yourself. This class may help.
  ///
  /// To use it, write something like this:
  ///
  ///   class AbstractBaseClass : public virtual HasClassId {}
  ///
  ///   class ConcreteDerivedClass : public AbstractBaseClass,
  ///                                public UniqueClassId< ConcreteDerivedClass >
  ///   {
  ///     static inline bool classof(ConcreteDerivedClass*) { return true; }
  ///     static inline bool classof(AbstractBaseClass *e) { return (e->getClassId() % GetClassId()) == 0; }
  ///   }
  ///
  /// Yeah, it's sort of ugly, but that's a fundamental
  /// quality of re-implementing a language feature
  /// such as RTTI within the language itself.
  ///
  /// Subclasses of this each have a class identifier
  /// which is unique to this class.  It can be accessed
  /// via the getClassId() method.
  template <class Nonce>
  class UniqueClassId : public virtual HasClassId
  {
    static UniqueId MyClassId;

  public:
    ~UniqueClassId() {}

    /// Get the class id of an instance
    unsigned getClassId() const { return MyClassId.get(); }

    /// Get the class id of a class.
    static unsigned GetClassId() { return MyClassId.get(); }
  };

  template <class Nonce> UniqueId UniqueClassId<Nonce>::MyClassId;
}

#endif

