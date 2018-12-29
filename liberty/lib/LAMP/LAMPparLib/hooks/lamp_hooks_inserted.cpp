#if HAS_SMTX
#define __STDC_FORMAT_MACROS

#include "lamp_hooks.hxx"

#define LOAD LAMP_external_load
#define STORE LAMP_external_store
#define ALLOCATE LAMP_external_allocate
#define DEALLOCATE LAMP_external_deallocate

#undef ALLOCATE
#undef DEALLOCATE
#undef LOAD
#undef STORE

#include <iostream>
#include <iomanip>
#include <fstream>
#include <map>
#include <vector>

extern "C" {
#include <time.h>
#include <pthread.h>
}

#define MAX_NUM_LOOPS 1048

using namespace std;

/***** Proto Types  *****/
template <class T> void LAMP_load(const uint32_t instr, const uint64_t addr);
template<class T> void LAMP_store(uint32_t instrID, uint64_t addr, uint64_t value);
void LAMP_loop_invocation_par(const uint16_t loop);
void LAMP_loop_iteration_begin_par(void);


/* Globals */
volatile uint64_t *addrArray;
uint64_t pos;



Inline void lamp_streamWrite(volatile uint64_t *addr, uint64_t value)
{
  /*   __m64 mmxReg = _mm_set_pi64x((int64_t) value); */
  /*   _mm_stream_pi((__m64 *) addr, mmxReg); */

  __asm (
      "movntiq %1, (%0)\n"
      :
      : "r" (addr), "r" (value)
      );

}

/***********************************************************************
 ******************Load Functions***************************************
 ***********************************************************************/
void LAMP_load1(const uint32_t instr, const uint64_t addr) {
  //LAMP_load<uint8_t>(instr, addr);
  uint64_t tmp = L1;
  tmp = (tmp << 32) + instr;
  /*
     sq_produce(q,L1);
     sq_produce(q,instr);
     sq_produce(q,addr);
     sq_produce2(q, tmp, addr);
     */
  lamp_streamWrite(&addrArray[pos%MAX],tmp);
  pos++;
  lamp_streamWrite(&addrArray[pos%MAX],addr);
  pos++;
}

void LAMP_load2(const uint32_t instr, const uint64_t addr) {
  //LAMP_load<uint16_t>(instr, addr);
  uint64_t tmp = L2;
  tmp = (tmp << 32) + instr;
  /*
     sq_produce2(q, tmp, addr);

     sq_produce(q,L2);
     sq_produce(q,instr);
     sq_produce(q,addr);
     */
  lamp_streamWrite(&addrArray[pos%MAX],tmp);
  pos++;
  lamp_streamWrite(&addrArray[pos%MAX],addr);
  pos++;
}

void LAMP_load4(const uint32_t instr, const uint64_t addr)
{
  //  LAMP_load<uint32_t>(instr, addr);

  uint64_t tmp = L4;
  tmp = (tmp << 32) + instr;
  /*
     sq_produce2(q, tmp, addr);

     sq_produce(q,L4);
     sq_produce(q,instr);
     sq_produce(q,addr);
     */
  lamp_streamWrite(&addrArray[pos%MAX],tmp);
  pos++;
  lamp_streamWrite(&addrArray[pos%MAX],addr);
  pos++;
}

void LAMP_load8(const uint32_t instr, const uint64_t addr) {
  //LAMP_load<uint64_t>(instr, addr);
  uint64_t tmp = L8;
  tmp = (tmp << 32) + instr;
  /*
     sq_produce2(q, tmp, addr);


     sq_produce(q,L8);
     sq_produce(q,instr);
     sq_produce(q,addr);
     */
  lamp_streamWrite(&addrArray[pos%MAX],tmp);
  pos++;
  lamp_streamWrite(&addrArray[pos%MAX],addr);
  pos++;
}

void LAMP_llvm_memcpy_p0i8_p0i8_i32(const uint32_t instr,
    const uint8_t * dstAddr,
    const uint8_t * srcAddr,
    const uint32_t sizeBytes)
{
  LAMP_llvm_memmove_p0i8_p0i8_i64(instr, dstAddr, srcAddr, (uint64_t)sizeBytes);
}

void LAMP_llvm_memcpy_p0i8_p0i8_i64(const uint32_t instr,
    const uint8_t * dstAddr,
    const uint8_t * srcAddr,
    const uint64_t sizeBytes)
{
  LAMP_llvm_memmove_p0i8_p0i8_i64(instr, dstAddr, srcAddr, sizeBytes);
}

void LAMP_llvm_memmove_p0i8_p0i8_i32(const uint32_t instr,
    const uint8_t * dstAddr,
    const uint8_t * srcAddr,
    const uint32_t sizeBytes)
{
  LAMP_llvm_memmove_p0i8_p0i8_i64(instr, dstAddr, srcAddr, (uint64_t)sizeBytes);
}

void LAMP_llvm_memmove_p0i8_p0i8_i64(const uint32_t instr,
    const uint8_t * dstAddr,
    const uint8_t * srcAddr,
    const uint64_t sizeBytes)
{
  uint64_t i;

  if (srcAddr <= dstAddr && srcAddr + sizeBytes > dstAddr)
  {
    // overlap, copy backwards
    for(i=0; i<sizeBytes; ++i)
    {
      const uint64_t k = sizeBytes - 1 - i;
      LAMP_load1(instr, (uint64_t) &srcAddr[k]);
      LAMP_store1(instr, (uint64_t) &dstAddr[k], srcAddr[k]);
    }
  }
  else
  {
    // copy forward.
    for(i=0; i<sizeBytes; ++i)
    {
      LAMP_load1(instr, (uint64_t) &srcAddr[i]);
      LAMP_store1(instr, (uint64_t) &dstAddr[i], srcAddr[i] );
    }
  }
}

void LAMP_llvm_memset_p0i8_i32(const uint32_t instr,
    const uint8_t * dstAddr,
    const uint8_t value,
    const uint32_t sizeBytes)
{
  LAMP_llvm_memset_p0i8_i64(instr, dstAddr, value, (uint64_t)sizeBytes);
}

void LAMP_llvm_memset_p0i8_i64(const uint32_t instr,
    const uint8_t * dstAddr,
    const uint8_t value,
    const uint32_t sizeBytes)
{
  uint64_t i;

  for(i=0; i<sizeBytes; ++i)
    LAMP_store1(instr, (uint64_t) &dstAddr[i], value);
}

/***********************************************************************
 ***************Store Functions*****************************************
 ***********************************************************************/
void LAMP_store1(uint32_t instr, uint64_t addr, uint64_t value)
{
  //LAMP_store<uint8_t>(instr, addr, value);

  uint64_t tmp = S1;
  tmp = (tmp << 32) + instr;
  /*
     sq_produce2(q, tmp, addr);

     sq_produce(q,S1);
     sq_produce(q,instr);
     sq_produce(q,addr);
     sq_produce(q,value);
     */
  lamp_streamWrite(&addrArray[pos%MAX],tmp);
  pos++;
  lamp_streamWrite(&addrArray[pos%MAX],addr);
  pos++;
}

void LAMP_store2(uint32_t instr, uint64_t addr, uint64_t value)
{
  //LAMP_store<uint16_t>(instr, addr, value);

  uint64_t tmp = S2;
  tmp = (tmp << 32) + instr;
  /*
     sq_produce2(q, tmp, addr);

     sq_produce(q,S2);
     sq_produce(q,instr);
     sq_produce(q,addr);
     sq_produce(q,value);
     */
  lamp_streamWrite(&addrArray[pos%MAX],tmp);
  pos++;
  lamp_streamWrite(&addrArray[pos%MAX],addr);
  pos++;
}

void LAMP_store4(uint32_t instr, uint64_t addr, uint64_t value)
{
  //  LAMP_store<uint32_t>(instr, addr, value);
  uint64_t tmp = S4;
  tmp = (tmp << 32) + instr;
  /*
     sq_produce2(q, tmp, addr);

     sq_produce(q,S4);
     sq_produce(q,instr);
     sq_produce(q,addr);
     sq_produce(q,value);
     */
  lamp_streamWrite(&addrArray[pos%MAX],tmp);
  pos++;
  lamp_streamWrite(&addrArray[pos%MAX],addr);
  pos++;
}

void LAMP_store8(uint32_t instr, uint64_t addr, uint64_t value)
{
  //LAMP_store<uint64_t>(instr, addr, value);

  uint64_t tmp = S8;
  tmp = (tmp << 32) + instr;

  /*
     sq_produce2(q, tmp, addr);

     sq_produce(q,S8);
     sq_produce(q,instr);
     sq_produce(q,addr);
     sq_produce(q,value);
     */

  lamp_streamWrite(&addrArray[pos%MAX],tmp);
  pos++;
  lamp_streamWrite(&addrArray[pos%MAX],addr);
  pos++;
}

/***********************************************************************
 ***********************************************************************
 ***********************************************************************/

void LAMP_loop_iteration_begin(void) {
  //time_stamp++;
  //loop_hierarchy.loopIteration(time_stamp, iterationcount);
  //initializeSets();

  uint64_t tmp = LIB;
  tmp = tmp << 32;
  /*
     sq_produce2(q, tmp, 0);
     */

  lamp_streamWrite(&addrArray[pos%MAX],tmp);
  pos++;
}

void LAMP_loop_iteration_end(void) {
  return;
}

/* Unused ? */
void LAMP_loop_iteration_begin_st(void) {
  LAMP_loop_iteration_begin();
}

/* Unused ? */
void LAMP_loop_iteration_end_st(void) {
  LAMP_loop_iteration_end();
}

void LAMP_loop_exit(const uint16_t loop) {
  //loop_hierarchy.exitLoop();
     uint64_t tmp = LE;
     tmp = (tmp << 32) + loop;
  /*
     sq_produce2(q, tmp, 0);
     */

  lamp_streamWrite(&addrArray[pos%MAX],tmp);
  pos++;
}

/* Unused ? */
void LAMP_loop_exit_st(void) {
  LAMP_loop_exit(0);
}

void LAMP_loop_invocation(const uint16_t loop)
{
  //loop_hierarchy.enterLoop(loop, time_stamp);
  //initializeSets();

     uint64_t tmp = LI;
     tmp = (tmp << 32) + loop;
  /*
     sq_produce2(q,tmp,0);

     sq_produce(q,LI);
     sq_produce(q,loop);
     */
  pos = pos%MAX;
  lamp_streamWrite(&addrArray[pos%MAX],tmp);
  pos++;
}

/* Unused ? */
void LAMP_loop_invocation_st(void) {
  uint16_t loop_id = (uint16_t) LAMP_param1;
  LAMP_loop_invocation(loop_id);
}
#endif /* HAS_SMTX */
