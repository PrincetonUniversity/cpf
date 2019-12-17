#ifndef MEMORY_MAP_H
#define MEMORY_MAP_H

#ifndef __STDC_FORMAT_MACROS
#define __STDC_FORMAT_MACROS
#endif

#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <string.h>

#include <iostream>
using namespace std;

#include <tr1/unordered_map>

using namespace std::tr1;

#ifdef MEMMAP_DEBUG
#include "debug_new.hxx"
#endif

#define ROUND_UP_DIVISION(x,y) ((x+y-1)/y)
#define BITS_PER_BYTE 8ULL

namespace Memory {

  extern FILE *debug_log;

  void memory_map_init(FILE *debug);

  template<class T>
    bool is_aligned(uint64_t addr) {
      return (addr % sizeof(T) == 0);
    }

  template<class T>
    bool is_aligned(const void *addr) {
      return is_aligned<T>((uint64_t) addr);
    }

  enum endian {LITTLE = 0, BIG = 1};

  template<class T, endian ENDIAN>
    T construct_value(uint8_t *bytes) {
      T value = 0;

      if (ENDIAN == LITTLE) {
        for (uint8_t i = 0; i < sizeof(T); i++) {
          const uint64_t mask = (((uint64_t) bytes[i]) << (i * BITS_PER_BYTE));
          value = value | mask;
        }
      } else { // ENDIAN == BIG
        for (uint8_t i = 0; i < sizeof(T); i++) {
          const uint64_t mask = (((uint64_t) bytes[sizeof(T) - i - 1]) << (i * BITS_PER_BYTE));
          value = value | mask;
        }
      }

      return value;
    }

  template<class T, endian ENDIAN>
    void deconstruct_value(T value, uint8_t *bytes) {
      if (ENDIAN == LITTLE) {
        uint8_t * value_ptr = (uint8_t *) &value;
        for (uint8_t i = 0; i < sizeof(T); i++) {
          bytes[i] = value_ptr[i];
        }
      } else { // ENDIAN == BIG
        uint8_t * value_ptr = (uint8_t *) &value;
        for (uint8_t i = 0; i < sizeof(T); i++) {
          bytes[i] = value_ptr[sizeof(T) - i - 1];
        }
      }
    }


  typedef uint64_t addr_t;
  typedef uint64_t pageaddr_t;

  struct PageAddrEquals {
    bool operator()(const pageaddr_t &page1, const pageaddr_t page2) const {
      return page1 == page2;
    }
  };

  struct PageAddrHash {
    size_t operator()(const pageaddr_t &page) const {
      return page;
    }
  };

  enum valid_t {NONE = 0, SOME = 1, ALL = 2};

  static const uint64_t DEFAULT_PAGE_BITS = 12;

  // Must be at least 6 to ensure that every bit of a read_track_t is used
  template<class T, unsigned int PAGE_BITS = DEFAULT_PAGE_BITS>
    class MemoryPage {
      protected:
        MemoryPage(const MemoryPage<T, PAGE_BITS> &node) {}

        MemoryPage<T> & operator=(const MemoryPage<T, PAGE_BITS> &node) {return *this;}

      protected:
        static const uint64_t PAGE_SIZE = (1ULL << PAGE_BITS);

        static const uint64_t OFFSET_MASK = ((1ULL << PAGE_BITS) - 1ULL);

        static const uint64_t ADDR_MASK = ~(OFFSET_MASK);

        // MJB: The code below assumes that no read can be more bytes than bits in a read_track_t
        typedef uint8_t read_track_t;

        static const uint64_t TRACK_BITS_PER_INDEX = (sizeof(read_track_t) * 8);

        // This needs to be consistent with TRACK_BITS_PER_INDEX
        static const uint64_t TRACK_SHIFT_OFFSET = 3;

        static const uint64_t TRACK_SIZE = PAGE_SIZE / TRACK_BITS_PER_INDEX;

        pageaddr_t page_addr;

        T values[PAGE_SIZE];

        read_track_t valid[TRACK_SIZE];

        void check_range(const void * addr, const uint32_t size) const {
          if (am_page_addr(addr) != this->page_addr) {
            cerr<<"Write to wrong page"<<endl;
            abort();
          }

          if (am_page_addr((char *) addr + size - 1) != this->page_addr) {
            cerr<<"Write of size "<<size<<" @ "<<addr<<" crosses page boundary"<<endl;
            abort();
          }
        }

        void check_range(const void * addr) const {
          check_range(addr, 1);
        }

        static uint32_t am_offset(const void * addr) {
          return ((intptr_t) addr) & OFFSET_MASK;
        }

        const T *getOffset(const uint32_t offset) const {
          if (!this->is_offset_valid(offset, 1)) {
            return NULL;
          }
          return &(this->values[offset]);
        }

        T *getOffset(const uint32_t offset) {
          if (!this->is_offset_valid(offset, 1)) {
            return NULL;
          }
          return &(this->values[offset]);
        }

        read_track_t mask(const uint8_t length) const {
          return ((1ULL << length) - 1);
        }

        read_track_t offsetMask(const uint8_t length, const uint32_t offset) const {
          if ((length + offset) > TRACK_BITS_PER_INDEX) {
            fprintf(stderr, "Can not create offset mask for %u + %u > %" PRIu64 "\n",
                length, offset, TRACK_BITS_PER_INDEX);
            abort();
          }
          return (mask(length) << offset);
        }

        valid_t num_offset_valid(const uint32_t offset, uint8_t length) const {
          const uint32_t byte_offset = offset >> TRACK_SHIFT_OFFSET;
          const uint32_t bit_offset = offset % TRACK_BITS_PER_INDEX;
          const read_track_t omask = offsetMask(length, bit_offset);
          const read_track_t bits = (this->valid[byte_offset] & omask);

          const read_track_t isolated = (bits >> bit_offset);
          if (isolated == 0) {
            return NONE;
          } else if (isolated == mask(length)) {
            return ALL;
          } else {
            return SOME;
          }
        }

        uint8_t is_offset_valid(const uint32_t offset, uint8_t length) const {
          const uint32_t byte_offset = offset >> TRACK_SHIFT_OFFSET;
          const uint32_t bit_offset = offset % TRACK_BITS_PER_INDEX;
          const read_track_t omask = offsetMask(length, bit_offset);
          const read_track_t bits = (this->valid[byte_offset] & omask);
          return (bits == omask);
        }

        void set_offset_valid(const uint32_t offset, uint8_t length) {
          const uint32_t byte_offset = offset >> TRACK_SHIFT_OFFSET;
          const uint32_t bit_offset = offset % TRACK_BITS_PER_INDEX;
          const read_track_t mask = offsetMask(length, bit_offset);
          this->valid[byte_offset] |= mask;
        }

        void set_offset_invalid(const uint32_t offset, uint8_t length) {
          const uint32_t byte_offset = offset >> TRACK_SHIFT_OFFSET;
          const uint32_t bit_offset = offset % TRACK_BITS_PER_INDEX;
          const read_track_t mask = ~offsetMask(length, bit_offset);
          this->valid[byte_offset] &= mask;
        }

      public:
        MemoryPage(pageaddr_t addr) : page_addr(addr) {
          if (am_page_addr((void *) addr) != (uint64_t) addr) {
            cerr<<"Invalid key address"<<endl;
            abort();
          }

          memset(this->valid, 0, sizeof(this->valid));
        }

        pageaddr_t getAddress() const {
          return this->page_addr;
        }

        bool inPage(const void *ad) const {
          return (am_page_addr(ad) == this->page_addr);
        }

        static void check_addr_range(const void * addr, const uint32_t size) {
#ifdef CHECK_ADDR_RANGE
          if (am_page_addr(((char *) addr) + size - 1) != am_page_addr(addr)) {
            cerr<<"Write of size "<<size<<" @ "<<addr<<" crosses page boundary"<<endl;
            abort();
          }
#endif
        }

        static uint64_t am_page_addr(const void *addr) {
          return ((intptr_t) addr) & ADDR_MASK;
        }

        void clear() {
          memset(this->valid, 0, sizeof(this->valid));
        }

        const T *getItem(const void * addr) const {
          check_range(addr);
          const uint32_t offset = am_offset(addr);
          return getOffset(offset);
        }

        T *getItem(const void * addr) {
          check_range(addr);
          const uint32_t offset = am_offset(addr);
          return getOffset(offset);
        }

        void setItem(const void * addr, const T &item) {
          check_range(addr);
          const uint32_t offset = am_offset(addr);
          this->values[offset] = item;
          this->set_offset_valid(offset, 1);
        }

        valid_t num_valid(const void *addr, uint8_t length) const {
          check_range(addr, length);
          const uint32_t offset = am_offset(addr);
          return this->num_offset_valid(offset, length);
        }

        uint8_t is_valid(const void *addr, uint8_t length) const {
          check_range(addr, length);
          const uint32_t offset = am_offset(addr);
          return this->is_offset_valid(offset, length);
        }

        void set_valid(const void *addr, uint8_t length) {
          check_range(addr, length);
          const uint32_t offset = am_offset(addr);
          this->set_offset_valid(offset, length);
        }

        void set_invalid(const void *addr, uint8_t length) {
          check_range(addr, length);
          const uint32_t offset = am_offset(addr);
          this->set_offset_invalid(offset, length);
        }

        valid_t get_aligned_validity(const void * addr, uint8_t length) const {
          this->check_range(addr, length);
          return this->num_valid(addr, length);
        }
    };

  template<class T>
    class MemoryMap  {
      private:

        MemoryMap(const MemoryMap<T> &map) {}

        MemoryMap &operator=(const MemoryMap<T> &map) {return *this;}

      protected:
        typedef unordered_map<pageaddr_t, T *, PageAddrHash, PageAddrEquals> PageMap;

        PageMap pageMap;

      public:
        MemoryMap() : pageMap() {}

        virtual ~MemoryMap() {
          for (typename PageMap::iterator iter = this->pageMap.begin(); iter != this->pageMap.end(); iter++) {
            T *node = iter->second;
            delete node;
          }
        }

        void clear() {
          for (typename PageMap::iterator iter = this->pageMap.begin(); iter != this->pageMap.end(); iter++) {
            T *node = iter->second;
            delete node;
          }
          pageMap.clear();
        }

        void clearPages() {
          for (typename PageMap::iterator iter = this->pageMap.begin(); iter != this->pageMap.end(); iter++) {
            iter->second->clear();
          }
        }

        bool containsPage(const void * addr) {
          return (this->find(T::am_page_addr(addr)) != this->pageMap.end());
        }

        const T *getNode(const void *addr) const {
          return getNode(T::am_page_addr(addr));
        }

        const T *getNode(const pageaddr_t &addr) const {
          typename PageMap::const_iterator iter = this->pageMap.find(addr);
          if (iter == this->pageMap.end()) return NULL;
          return iter->second;
        }

        T *getNode(const void *addr) {
          return getNode(T::am_page_addr(addr));
        }

        T *getNode(const pageaddr_t &addr) {
          typename PageMap::iterator iter = this->pageMap.find(addr);
          if (iter == this->pageMap.end()) return NULL;
          return iter->second;
        }

        T *get_or_create_node(const void *addr) {
          pageaddr_t paddr = T::am_page_addr(addr);
          T *item = getNode(paddr);
          if (item == NULL) {
            item = new T(paddr);
            this->pageMap[paddr] = item;
          }
          return item;
        }

        template <class S>
          bool is_valid(const void *addr) const {
            const T *node = this->getNode(addr);
            if (node == NULL)
              return false;

            return (node->is_valid(addr, sizeof(S)));
          }

        template <class S>
          void set_invalid(const void *addr) {
            T *node = this->getNode(addr);
            if (node == NULL)
              return;
            node->set_invalid(addr, sizeof(S));
          }

        template <class S>
          void set_valid(const void *addr) {
            T *node = this->get_or_create_node(addr);
            node->set_valid(addr, sizeof(S));
          }
    };


  template <class T, unsigned int PAGE_BITS = DEFAULT_PAGE_BITS>
    class MemoryNodeMap : public MemoryMap<MemoryPage<T, PAGE_BITS> > {
      public:
        typedef MemoryNodeMap<T, PAGE_BITS> MapType;

        typedef MemoryPage<T, PAGE_BITS> PageType;
      private:
        MemoryNodeMap(const MapType &map) {}

        MemoryNodeMap &operator=(const MapType &map) {return *this;}

      public:
        MemoryNodeMap() : MemoryMap<PageType>() {}

        virtual ~MemoryNodeMap() {}

        template <class S>
          valid_t get_aligned_validity(const void * addr) const {
            PageType::check_addr_range(addr, sizeof(S));
            if (this->pageMap.empty())
              return NONE;

            const PageType *node = this->getNode(addr);
            if (node == NULL) {
              return NONE;
            }
            return node->get_aligned_validity(addr, sizeof(S));
          }

        const T * getItem(const void *addr) const {
          PageType *node = this->getNode(addr);
          if (node == NULL) {
            return NULL;
          }

          return node->getItem(addr);
        }

        T * getItem(const void *addr) {
          PageType *node = this->getNode(addr);
          if (node == NULL) {
            return NULL;
          }

          return node->getItem(addr);
        }

        void setItem(const void *addr, const T &item) {
          PageType *node = this->get_or_create_node(addr);
          node->setItem(addr, item);
        }
    };

  extern int max_seqno;

  /**
   * uint8_t specifics
   */

  template <unsigned int PAGE_BITS = DEFAULT_PAGE_BITS>
    class BytePage : public MemoryPage<uint8_t, PAGE_BITS> {
      private:
        BytePage(const BytePage &page) : MemoryPage<uint8_t, PAGE_BITS>(page), seqno(++max_seqno) {}

        BytePage &operator=(const BytePage &page) {return *this;}

      protected:
        uint64_t seqno;

        static void * construct_address(pageaddr_t page_address, uint64_t granule) {
          return (void *) (intptr_t) (page_address | granule);
        }

      public:
        BytePage(pageaddr_t addr) : MemoryPage<uint8_t, PAGE_BITS>(addr), seqno(++max_seqno) {}

        template <class T>
          T read_value(const void * addr) const {
            this->check_range(addr, sizeof(T));

            if (!this->is_valid(addr, sizeof(T))) {
              fprintf(stderr, "Attempt to read invalid value\n");
              abort();
            }

            const uint32_t offset = this->am_offset(addr);
            //cerr<<"Reading value at offset "<<offset<<" in page "<<(void *) this->page_addr<<endl;
            const T *ptr = (T *) (&(this->values[offset]));
            return *ptr;
          }

        template <class T>
          void write_value(const void *addr, const T & value) {
            this->check_range(addr, sizeof(T));
            const uint32_t offset = this->am_offset(addr);
            //cerr<<"Writing value at offset "<<offset<<" in page "<<(void *) this->page_addr<<endl;
            T *ptr = (T *) (&(this->values[offset]));
            *ptr = value;
            this->set_offset_valid(offset, sizeof(T));
          }

        void merge(const BytePage *node) {
          uint64_t live_bytes = 0;

          for (uint64_t i = 0; i < sizeof(this->valid); i++) {
            const uint8_t validity = node->valid[i];
            this->valid[i] |= validity;
            const uint64_t offset = i * 8;

            switch(validity) {
              case 0:   // 0x00
                continue;
                break;
              case 15:  // 0x0f
                *((uint32_t *) &this->values[offset]) = *((uint32_t *) &node->values[offset]);
                break;
              case 240: // 0xf0
                *((uint32_t *) &this->values[offset + 4]) = *((uint32_t *) &node->values[offset + 4]);
                break;
              case 255: // 0xff
                *((uint64_t *) &this->values[offset]) = *((uint64_t *) &node->values[offset]);
                break;
              default:
                {
                  for (uint64_t granule = offset; granule < offset + MemoryPage<uint8_t, PAGE_BITS>::TRACK_BITS_PER_INDEX ; granule++) {
                    if (node->is_offset_valid(granule, 1)) {
#ifdef MEMMAP_DEBUG
                      fprintf(debug_log, "Writing %lx %lx %p\n", page_addr, granule, (void *) (intptr_t) (page_addr | granule) );
#endif
                      this->values[granule] = node->values[granule];
                      live_bytes++;
                    }
                  }
                }
                break;
            }
          }
        }


        /* Untested */
        void merge2(const BytePage *node) {

          for (uint64_t i = 0; i < sizeof(this->valid); i++) {
            uint8_t validity = node->valid[i];
            this->valid[i] |= validity;
            const uint64_t offset = i * 8;

            if (validity == 0) {
              continue;
            } else if (validity == 0xff) {
              this->values[offset] = *((uint64_t *) &node->values[offset]);
            } else {
              for (unsigned j = 0; j < ((sizeof(validity) * 8) / 4); j++) {
                switch(validity & 0xf) {
                  case 0:
                    break;
                  case 1:
                    this->values[offset] = *((uint8_t *) &node->values[offset]);
                    break;
                  case 2:
                    this->values[offset + 1] = *((uint8_t *) &node->values[offset + 1]);
                    break;
                  case 3:
                    *((uint16_t *) this->values[offset]) = *((uint16_t *) &node->values[offset]);
                    break;
                  case 4:
                    this->values[offset + 2] = *((uint8_t *) &node->values[offset + 2]);
                    break;
                  case 5:
                    this->values[offset + 2] = *((uint8_t *) &node->values[offset + 2]);
                    this->values[offset] = *((uint8_t *) &node->values[offset]);
                    break;
                  case 6:
                    this->values[offset + 1] = *((uint8_t *) &node->values[offset + 1]);
                    this->values[offset + 2] = *((uint8_t *) &node->values[offset + 2]);
                    break;
                  case 7:
                    *((uint16_t *) this->values[offset]) = *((uint16_t *) &node->values[offset]);
                    this->values[offset + 2] = *((uint8_t *) &node->values[offset + 2]);
                    break;
                  case 8:
                    this->values[offset + 3] = *((uint8_t *) &node->values[offset + 3]);
                    break;
                  case 9:
                    this->values[offset] = *((uint8_t *) &node->values[offset]);
                    this->values[offset + 3] = *((uint8_t *) &node->values[offset + 3]);
                    break;
                  case 10:
                    this->values[offset + 1] = *((uint8_t *) &node->values[offset + 1]);
                    this->values[offset + 3] = *((uint8_t *) &node->values[offset + 3]);
                    break;
                  case 11:
                    *((uint16_t *) this->values[offset]) = *((uint16_t *) &node->values[offset]);
                    this->values[offset + 3] = *((uint8_t *) &node->values[offset + 3]);
                    break;
                  case 12:
                    *((uint16_t *) this->values[offset + 2]) = *((uint16_t *) &node->values[offset + 2]);
                    break;
                  case 13:
                    this->values[offset] = *((uint8_t *) &node->values[offset]);
                    *((uint16_t *) this->values[offset + 2]) = *((uint16_t *) &node->values[offset + 2]);
                    break;
                  case 14:
                    this->values[offset + 1] = *((uint8_t *) &node->values[offset + 1]);
                    *((uint16_t *) this->values[offset + 2]) = *((uint16_t *) &node->values[offset + 2]);
                    break;
                  case 15:
                    *((uint32_t *) this->values[offset]) = *((uint32_t *) &node->values[offset]);
                    break;
                  default:
                    abort();
                    break;
                }
                validity = validity>>4;
              }
            }
          }
        }

        bool commit_to_main_memory(FILE *fp) const {
          const uint64_t addr = this->page_addr;
          bool wrote_something = false;
          for (uint64_t granule = 0; granule < this->PAGE_SIZE; granule++) {
            if (this->is_offset_valid(granule, 1)) {
              uint8_t *maddr = (uint8_t *) (intptr_t) (addr | granule);
#ifdef MEMMAP_DEBUG_BYTE
              fprintf(fp, "Commiting (%lx | %lx) = %p\n", page_addr, granule, maddr);
#endif

#ifdef MEMMAP_DEBUG
              check_address(maddr, fp);
#endif

              *maddr = this->values[granule];
              wrote_something = true;
            }
          }

#ifdef MEMMAP_DEBUG
          fflush(fp);
#endif
          return wrote_something;
        }

        bool are_values_correct() const {
          const uint64_t addr = this->page_addr;
          for (uint64_t granule = 0; granule < this->PAGE_SIZE; granule++) {
            if (this->is_offset_valid(granule, 1)) {
              const uint8_t *maddr = (uint8_t *) (intptr_t) (addr | granule);
              if (*maddr != this->values[granule])
                return false;
            }
          }
          return true;
        }

        uint32_t print_valid_ranges(FILE *fp) const {
          const uint64_t addr = this->page_addr;
          uint32_t num_valid = 0;
          for (uint64_t granule = 0; granule < this->PAGE_SIZE; granule++) {
            while ((granule < this->PAGE_SIZE) && !this->is_offset_valid(granule, 1)) {
              granule++;
            }

            if (granule >= this->PAGE_SIZE) {
              break;
            }
            const uint64_t first = granule;

            while ((granule < this->PAGE_SIZE) && this->is_offset_valid(granule, 1)) {
#ifdef MEMMAP_DEBUG_BYTE
              uint8_t *maddr = (uint8_t *) (intptr_t) (addr | granule);
              fprintf(fp, "Committing %lx %lx %p\n", page_addr, granule, maddr );
#endif
              num_valid++;
              granule++;
            }
            {
              const void *begin = construct_address(addr, first);
              const void *end = construct_address(addr, granule - 1);
              fprintf(fp, "Valid %p-%p [%" PRIu64 "]\n", begin, end, (granule - first));
            }
          }

          fflush(fp);
          return num_valid;
        }

        template <class T>
          T read_value_committed(const void * addr) const {
            this->check_range(addr, sizeof(T));

            uint8_t bytes[sizeof(T)];
            for (uint8_t i = 0; i < sizeof(T); i++) {
              const void * caddr = ((char *) addr) + i;
              uint8_t byte_value;
              if (this->is_valid(caddr, 1)) {
                byte_value = this->read_value(caddr, 1);
              } else {
                byte_value = *((const uint8_t *) caddr);
              }
              bytes[i] = byte_value;
            }

            return construct_value<T, LITTLE>(bytes);
          }
    };

  template<unsigned int PAGE_BITS2 = DEFAULT_PAGE_BITS>
    class MemoryValueMap : public MemoryMap<BytePage<PAGE_BITS2> > {
      public:
        typedef BytePage<PAGE_BITS2> PageType;

        typedef MemoryValueMap<PAGE_BITS2> MapType;

      private:
        MemoryValueMap(const MapType &map) {}

        MemoryValueMap &operator=(const MapType &map) {return *this;}

      public:
        MemoryValueMap() : MemoryMap<PageType>() {}

        virtual ~MemoryValueMap() {}

        template <class S>
          valid_t get_aligned_validity(const void * addr) const {
            PageType::check_addr_range(addr, sizeof(S));
            const PageType *node = this->getNode(addr);
            if (node == NULL) {
              return NONE;
            }
            return node->get_aligned_validity(addr, sizeof(S));
          }

        template <class T>
          T read_aligned_value(const void * addr) const {
            PageType::check_addr_range(addr, sizeof(T));
            const PageType *node = this->getNode(addr);
            if (node == NULL) {
              fprintf(stderr, "Illegal read\n");
            }
            return node->read_value(addr);
          }

        template <class T>
          T read_unaligned_value(const void * addr) const {
            uint8_t bytes[sizeof(T)];
            for (uint8_t i = 0; i < sizeof(T); i++) {
              uint8_t byte = read_aligned_value<uint8_t>(((char *) addr) + i);
              bytes[i] = byte;
            }
            return *((T *) bytes);
          }

        template <class T>
          T read_value(const void * addr) const {
            if (!is_aligned<T>(addr)) {
              return read_unaligned_value<T>(addr);
            } else {
              return read_aligned_value<T>(addr);
            }
          }

        template <class T>
          void write_aligned_value(void * addr, const T &value) {
            PageType::check_addr_range(addr, sizeof(T));
            PageType *node = this->get_or_create_node(addr);
            node->write_value(addr, value);
          }

        template <class T>
          void write_unaligned_value(const void *addr, const T &value) {
            uint8_t bytes[sizeof(T)];
            deconstruct_value<T, LITTLE>(value, bytes);
            for (uint8_t i = 0; i < sizeof(T); i++) {
              write_aligned_value<uint8_t>(((char *) addr) + i, bytes[i]);
            }
          }

        template <class T>
          void write_value(void * addr, const T & value) {
            if (!is_aligned<T>(addr)) {
              write_unaligned_value<T>(addr, value);
            } else {
              write_aligned_value<T>(addr, value);
            }
          }

        template <class T>
          T read_value_committed(const void * addr) const {
            if (!is_aligned<T>(addr)) {
              abort();
            }

            const PageType *node = this->getNode(addr);
            if (node == NULL) {
              return *((const T *) addr);
            }

            return node->read_value_committed(addr);
          }

        void merge(const MapType *mm_version) {
          for (typename MemoryMap<BytePage<PAGE_BITS2> >::PageMap::const_iterator iter = mm_version->pageMap.begin(); iter != mm_version->pageMap.end(); iter++) {
            const PageType *node = iter->second;
            const pageaddr_t addr = node->getAddress();
            PageType *this_node = this->get_or_create_node((void *) addr);
            this_node->merge(node);
          }
        }

        void commit_to_main_memory() const {
          FILE *fp = debug_log;

          for (typename MemoryMap<BytePage<PAGE_BITS2> >::PageMap::const_iterator iter = this->pageMap.begin(); iter != this->pageMap.end(); iter++) {
            const PageType *node = iter->second;

#ifdef MEMMAP_DEBUG
            const uint32_t num_valid = node->print_valid_ranges(fp);
            if ((fp != NULL) && (num_valid > 0))
              fprintf(fp, "\t for page %lx\n", node->getAddress());
#endif

            const bool wrote_something = node->commit_to_main_memory(fp);
            if (fp != NULL && wrote_something)
              fprintf(fp, "Committing page %" PRIx64 "\n", node->getAddress());
          }
        }

        bool are_values_correct() const {
          FILE *fp = debug_log;
          for (typename MemoryMap<BytePage<PAGE_BITS2> >::MapType::PageMap::const_iterator iter = this->pageMap.begin(); iter != this->pageMap.end(); iter++) {
            const PageType *node = iter->second;

#ifdef MEMMAP_DEBUG
            const uint32_t num_valid = node->print_valid_ranges(fp);
            if ((fp != NULL) && (num_valid > 0))
              fprintf(fp, "\t for page %lx\n", node->getAddress());
#endif

            const bool are_correct = node->are_values_correct();
            if (!are_correct) {
              if (fp != NULL) {
                fprintf(fp, "Invalid values on page %" PRIx64 "\n", node->getAddress());
              }
              return false;
            }
          }
          return true;
        }
    };
}

#endif
