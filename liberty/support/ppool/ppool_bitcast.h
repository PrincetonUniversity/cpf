#ifndef BITCAST_H
#define BITCAST_H

typedef union { double d; int64_t i; } SQ_DI;
typedef union { float f; int32_t i; } SQ_FI;

static inline int64_t doubleToInt(double d) {
	SQ_DI conv;  
	conv.d = d;   
	return conv.i; 
}                 
static inline int64_t floatToInt(float f) { 
	SQ_FI conv;
	conv.f = f;       
	return conv.i;     
}
static inline double intToDouble(int64_t i) {
	SQ_DI conv;    
	conv.i = i;    
	return conv.d; 
}                

static inline float intToFloat(int64_t i) {
	SQ_FI conv;    
	conv.i = i;    
	return conv.f; 
}                

#endif /* BITCAST_H */
