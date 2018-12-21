#include "holder.h"

void RefCount::incref() { ++refcount; }
void RefCount::decref() { if( 1 > --refcount ) delete this; }

