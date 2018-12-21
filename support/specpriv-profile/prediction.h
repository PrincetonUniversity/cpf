#ifndef SPECPRIV_PREDICTION_H
#define SPECPRIV_PREDICTION_H

#include "config.h"
#include "context.h"
#include "live.h"

#include <ostream>

struct IntSample
{
  IntSample() : value(0), frequency(0) {}
  IntSample(uint64_t v) : value(v), frequency(1) {}

  uint64_t      value;
  uint64_t      frequency;

  bool empty() const { return frequency == 0; }
  bool operator==(const IntSample &other) const { return value == other.value; }

  void receive(const IntSample &other);

  void print(std::ostream &fout) const;

  bool is_worth_printing() const { return frequency>0; }
};

std::ostream &operator<<(std::ostream &fout, const IntSample &sample);

struct PtrSample
{
  PtrSample();
  PtrSample(const AUHolder &, uint64_t offs=0);

  AUHolder      au;
  uint64_t      offset;

  uint64_t      frequency;

  bool empty() const { return frequency == 0; }
  bool operator==(const PtrSample &other) const;

  void receive(const PtrSample &other);

  void print(std::ostream &fout) const;

  bool is_worth_printing() const { return frequency>0; }
};

std::ostream &operator<<(std::ostream &fout, const PtrSample &sample);

// SampleType must support .empty(), ==, +=, <<, and .frequency
template <class SampleType, const unsigned N>
struct SampleSet
{
  SampleSet() : bottom(false), numSamples(0) {}

  void print(std::ostream &fout) const
  {
    if( bottom )
      fout << "UNPREDICTABLE ";
    else
      fout << "PREDICTABLE ";

    unsigned num_values = 0;
    for(unsigned i=0; i<N; ++i)
      if( observations[i].is_worth_printing() )
        ++num_values;

    fout << numSamples << " SAMPLES OVER " << num_values << " VALUES { ";

    bool first = true;
    for(unsigned i=0; i<N; ++i)
    {
      const SampleType &sample = observations[i];

      if( sample.is_worth_printing() )
      {
        if( !first )
          fout << " , ";
        first = false;

        fout << " ( " << sample << " ) ";
      }

    }

    fout << " }";
  }

  void receive(const SampleType &sample)
  {
    ++numSamples;
    if( !bottom )
    {
      for(unsigned i=0; i<N; ++i)
      {
        if( observations[i].empty() )
        {
          observations[i] = sample;
          return;
        }

        else if( observations[i] == sample )
        {
          observations[i].receive(sample);
          return;
        }
      }

      bottom = true;
    }
  }

  bool is_bottom() const { return bottom; }

  bool is_worth_printing() const
  {
//    if( bottom )
//      return false;
    for(unsigned i=0; i<N; ++i)
      if( observations[i].is_worth_printing() )
        return true;
    return false;
  }

private:
  bool bottom;
  unsigned numSamples;
  SampleType observations[ N ];
};

template <class SampleType, const unsigned N>
std::ostream &operator<<(std::ostream &fout, const SampleSet<SampleType,N> &pt)
{
  pt.print(fout);
  return fout;
}

typedef SampleSet< IntSample, MAX_INT_PREDICTION_OBSERVATIONS > IntegerSamples;
typedef SampleSet< PtrSample, MAX_POINTER_PREDICTION_OBSERVATIONS > PointerSamples;
typedef SampleSet< PtrSample, MAX_UNDERLYING_OBJECT_OBSERVATIONS > UnderlyingObjectSamples;

struct PtrResidueSet
{
  PtrResidueSet() : num_samples(0), residue_set(0) {}

  void print(std::ostream &fout) const;

  void receive(void *sample);

  bool is_bottom() const { return (residue_set == 0x0ffffu); }
  bool is_worth_printing() const { return true; }

private:
  unsigned       num_samples;
  uint16_t       residue_set;
};

std::ostream &operator<<(std::ostream &fout, const PtrResidueSet &residues);

struct PredictionTable
{
  void print(std::ostream &fout) const;

  void predict_int(const CtxHolder &ctx, const char *name, const IntSample &sample);
  void predict_ptr(const CtxHolder &ctx, const char *name, const PtrSample &sample);

  void find_underlying_object(const CtxHolder &ctx, const char *name, const PtrSample &sample);

  void pointer_residue(const CtxHolder &ctx, const char *name, void *sample);

  void exit_ctx(const CtxHolder &ctx);

private:
  typedef std::pair<const char *, CtxHolder>          CtxValue;
  typedef std::map<CtxValue, IntegerSamples>          IntPredictMap;
  typedef std::map<CtxValue, PointerSamples>          PtrPredictMap;
  typedef std::map<CtxValue, UnderlyingObjectSamples> ObjPredictMap;
  typedef std::map<CtxValue, PtrResidueSet>           PtrResidueMap;

  IntPredictMap intPredictions;
  PtrPredictMap ptrPredictions;
  ObjPredictMap objPredictions;
  PtrResidueMap ptrResidues;

  template <class SetTy>
  void print_samples(
    std::ostream &fout,
    const char *key,
    const SetTy &set) const;

  void print_residues(std::ostream &fout) const;
};

std::ostream &operator<<(std::ostream &fout, const PredictionTable &pt);

#endif

