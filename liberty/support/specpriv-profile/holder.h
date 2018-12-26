#ifndef SPECPRIV_HOLDER_H
#define SPECPRIV_HOLDER_H

struct RefCount
{
  RefCount() : refcount(0) {}

  void incref();
  void decref();

private:
  unsigned refcount;
protected:
  virtual ~RefCount() {}
};

// A smart, reference counting pointer
// Payload must support incref, decref.
template <class Payload>
struct Holder
{
  Holder(Payload *c=0)
    : payload(c)
  {
    if( payload )
      payload->incref();
  }

  Holder(const Holder<Payload> &other)
    : payload( other.payload )
  {
    if( payload )
      payload->incref();
  }

  Holder<Payload> &operator=(Payload *c)
  {
    if( c )
      c->incref();
    if( payload )
      payload->decref();
    payload = c;

    return *this;
  }

  Holder<Payload> &operator=(const Holder<Payload> &other)
  {
    if( other.payload )
      other.payload->incref();
    if( payload )
      payload->decref();
    payload = other.payload;

    return *this;
  }

  ~Holder()
  {
    if( payload )
      payload->decref();
  }

  bool is_null() const { return payload == 0; }

  Payload *operator*() { return payload; }
  const Payload *operator*() const { return payload; }

  Payload *operator->() { return payload; }
  const Payload *operator->() const { return payload; }

  bool operator==(const Holder<Payload> &other) const
  {
    if( payload == other.payload )
      return true;
    else if( payload == 0 || other.payload == 0 )
      return false;
    else
      return (*payload) == (*other.payload);
  }

  bool operator!=(const Holder<Payload> &other) const
  {
    return ! ((*this) == other);
  }

  bool operator<(const Holder<Payload> &other) const
  {
    if( payload == other.payload )
      return false;
    else if( payload == 0 )
      return true;
    else if( other.payload == 0 )
      return false;
    else
      return (*payload) < (*other.payload);
  }

private:
  Payload *payload;
};


#endif

