#pragma once

#include "llvm/Analysis/LoopInfo.h"
#include "llvm/IR/Instructions.h"
#include <string>
#include <set>
#include <unordered_map>

using namespace llvm;

namespace AutoMP
{

  class Annotation
  {
  public:
    Annotation();
    Annotation(Loop *l, std::string k, std::string v) : loop(l), key(k), value(v) {}

    // probably unecessary
    std::string get_key() const { return key; }
    std::string get_value() const { return value; }
    void setLoop(Loop *l) { loop = l; }
    Loop *getLoop() { return loop; }

    // for having annotations in a set
    bool operator<(const Annotation &a) const { return true; } // for std::set::insert
    bool operator=(const Annotation &a) const { return !key.compare(a.key) && !value.compare(a.value); }

  private:
    Loop *loop;
    std::string key;
    std::string value;
  };

  typedef std::unordered_map<Loop *, Annotation> LoopToAnnotationMap;

  /*
   * Syntax for reductions:
   *    #pragma note noelle reduction = <type>:<variable1>,<variable2>,...
   *
   * Limitations:
   * Doesn't support user-defined reductions yet (probably never will)
   */
  class ReduxAnnotation : public Annotation
  {
    enum class Type
    {
      Sum,
      Product
    };

  public:
    ReduxAnnotation();

  private:
    Type type;
    Value *redux_var; // is Value specific enough?
    std::set<Value *> associated_vars; // better naming later
  };

  class PrivateAnnotation : public Annotation
  {

  };

} // namespace AutoMP
