/*
Copyright (c) 2015, Intel Corporation
All rights reserved.

Redistribution and use in source and binary forms, with or without 
modification, are permitted provided that the following conditions are met:
- Redistributions of source code must retain the above copyright notice, 
  this list of conditions and the following disclaimer.
- Redistributions in binary form must reproduce the above copyright notice, 
  this list of conditions and the following disclaimer in the documentation 
  and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE 
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR 
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF 
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS 
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) 
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF 
THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef PSE_ARRAY_H_
#define PSE_ARRAY_H_

//#define ALIAS_ANA

#include <stdlib.h>
#include <stdint.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <iostream>
#include <sstream>
#include <algorithm>
#include <array>
#include <set>
#ifdef ALIAS_ANA
#include "../intel-runtime/include/pse-runtime.h"
#endif

#ifdef DEBUG_J2C_ARRAY
#define PRINTF printf
#define FLUSH(...) fflush(stdout)
#else
#define PRINTF(...)
#define FLUSH(...)
#endif

#define MAX_DIM 5

/*
 * Holds the start and end of a memory region.  operator< is overloaded so that any overlapping
 * ranges will show as equal and then we can use a set to detect overlaps.
 */
struct MemRange {
    MemRange(void *s, void *e) : start(s), end(e) { assert(s <= e); }

    // The "<" operator returns true if the left range is entirely below the right range,
    // with no overlap.  If ranges A and B overlap, then (A<B)==false && (B<A)==false,
    // therefore the STL set judges A and B to be equal.  This is how we detect partially
    // aliased variables.
    bool operator < (const MemRange &b) const {
        return (end < b.start);
    }
    const void *start, *end;
};

// In the C file generated by J2C, some types are hand-written like array here.
// The others are coverted from Julia types by compiler.

// We have to define Array as a C struct instead of a C++ class, because
// C++ class cannot be offloaded to MIC

#ifdef DEBUGJ2C
class RefcountStats {
private:
    unsigned num_decrements, num_increments;
public:
    RefcountStats(void) : num_decrements(0), num_increments(0) {}

    ~RefcountStats() {
        PRINTF("Refcount stats: decrements = %d, increments = %d\n", num_decrements, num_increments);
        FLUSH();
    }

    void dec() {
        __sync_fetch_and_add(&num_decrements, 1);
    }

    void inc() {
        __sync_fetch_and_add(&num_increments, 1);
    }
};
RefcountStats g_refcount_stats;
#endif

int64_t global_zero[MAX_DIM] = {0,0,0,0,0};

template <typename ELEMENT_TYPE>
class j2c_array;

// An interface for serializing/deserializing J2C array or other values
class j2c_array_io {
public:
    virtual void write_in(void *arr, uint64_t arr_length, unsigned int elem_size, bool immutable) = 0;
    virtual void write(void *arr, uint64_t arr_length, unsigned int elem_size, bool immutable) = 0;
    virtual void read(void **arr, uint64_t *length) = 0;
};

template <typename ELEMENT_TYPE>
class j2c_array_copy {
   public:
   /* Both copy_to_mic and copy_from_mic are meant to be called from host */
   static inline void copy_to_mic(int run_where, uintptr_t dst, ELEMENT_TYPE *data, uint64_t start, uint64_t len) {
   // FIXME: use into syntax if someone knows how to make it work
PRINTF("scalar copy to mic data = %x start = %d len = %d\n", data, start, len);
FLUSH();
        data += start;
        dst  += sizeof(ELEMENT_TYPE) * start;
// #pragma offload target(mic:run_where) in(data:length(len))
        {
            memcpy((void*)dst, (void*)data, sizeof(ELEMENT_TYPE) * len);
        }
PRINTF("scalar copy to mic data => %x\n", data);
FLUSH();
    }

    static inline void copy_from_mic(int run_where, ELEMENT_TYPE *dst, uintptr_t data, uint64_t start, uint64_t len) {
PRINTF("scalar copy from mic data = %x start = %d len = %d\n", data, start, len);
FLUSH();
        ELEMENT_TYPE *tmp;
        data += sizeof(ELEMENT_TYPE) * start;
        dst  += start;
PRINTF("scalar copy from mic data = %x dst = %x\n", data, dst);
FLUSH();
// #pragma offload target(mic:run_where) out(tmp[0:len]:into(dst[0:len]) preallocated alloc_if(1))
        {
            tmp = (ELEMENT_TYPE*)data;
        }
    }

    static void dump_element(std::stringstream *s, ELEMENT_TYPE d) {
        *s << d;
    }

    static void serialize(unsigned num_dim, int64_t *dims, ELEMENT_TYPE *data, j2c_array_io *s, bool immutable) {
        assert(num_dim > 0);
        s->write_in((void*)dims, num_dim, sizeof(int64_t), immutable);
        uint64_t len = 1;
        for (int i = 0; i < num_dim; i++) len *= dims[i];
        s->write((void*)data, len, sizeof(ELEMENT_TYPE), immutable);
    }

    static j2c_array<ELEMENT_TYPE> deserialize(j2c_array_io *s) {
        int64_t *dims;
        uint64_t num_dim, length;
        void *data;
        s->read((void**)&dims, &num_dim);
        if (num_dim == 0) {
          j2c_array<ELEMENT_TYPE> tmp;
          tmp.num_dim = 0;
          return tmp;
        }     
        s->read((void**)&data, &length);
        return j2c_array<ELEMENT_TYPE>((ELEMENT_TYPE*)data, (unsigned)num_dim, dims);
    }
                           
    static ELEMENT_TYPE *alloc_elements(uint64_t len) {
        //ELEMENT_TYPE *x = (ELEMENT_TYPE*)malloc(sizeof(ELEMENT_TYPE) * len);
        ELEMENT_TYPE *x = new ELEMENT_TYPE[len];
PRINTF("alloc scalar elements %x\n", x);
FLUSH();
        return x;
    }

    static void free_elements(ELEMENT_TYPE* a, uint64_t len) {
PRINTF("free scalar elements %x\n", a);
FLUSH();
#ifdef ALIAS_ANA
        pert_unregister_data((void*)a);
#endif
        //free(a);
        delete [] a;
    }
};

template <typename ELEMENT_TYPE>
class j2c_array_copy<j2c_array<ELEMENT_TYPE> > {
   public:
   /* Both functions are meant to be called from host */
   static inline void copy_to_mic(int run_where, uintptr_t dst, j2c_array<ELEMENT_TYPE> *data, int64_t start, int64_t len) {
        uintptr_t tmp[len];
PRINTF("nested copy to mic data = %x start = %d len = %d\n", data, start, len);
FLUSH();
        for (int64_t i = 0; i < len; i++)
        {
            if (data[start + i].data != NULL)
                tmp[i] = data[start + i].to_mic(run_where);
            else
                tmp[i] = 0;
        }
// #pragma offload target(mic:run_where) in(tmp:length(len))
        {
            j2c_array<ELEMENT_TYPE> *arr = (j2c_array<ELEMENT_TYPE>*)dst;
            for (int64_t i = 0; i < len; i++)
            {
                if (tmp[i] != 0) {
                    arr[start + i] = *(j2c_array<ELEMENT_TYPE>*)tmp[i];
                    delete ((j2c_array<ELEMENT_TYPE>*)tmp[i]);
                }
            }
        }
   }

   // Expect MIC pointer of type j2c_array<ELEMENT_TYPE>*
   // Return host pointer of type j2c_array<ELEMENT_TYPE>*
   static inline void copy_from_mic(int run_where, j2c_array<ELEMENT_TYPE> *dst, uintptr_t data, int64_t start, int64_t len) {
        uintptr_t _data[len];
//#pragma offload target(mic:run_where) out(tmpdata[0:len]:into(_data[0:len]) preallocated targetptr alloc_if(1))
//        #pragma offload target(mic:run_where) inout(_data:length(len))
        {
            for (int i = 0; i < len; i++)
            {
                j2c_array<ELEMENT_TYPE> &arr = ((j2c_array<ELEMENT_TYPE>*)data)[start + i];
                if (arr.data != NULL)
                    _data[i] = (uintptr_t)(&arr);
                else
                    _data[i] = 0;
            }
        }
PRINTF("copy_from_mic called on array of array\n");
FLUSH();
        for (int i = 0; i < len; i++)
        {
PRINTF("copy_from_mic copy %x\n", _data[i]);
FLUSH();
            if (_data[i] != 0)
                dst[i] = j2c_array<ELEMENT_TYPE>::from_mic(run_where, _data[i]);
        }
    }

    static void dump_element(std::stringstream *s, j2c_array<ELEMENT_TYPE> d) {
        *s << d.dump();
    }
    
    static void serialize(unsigned num_dim, int64_t *dims, j2c_array<ELEMENT_TYPE> *data, j2c_array_io *s, bool immutable) {
        assert(num_dim > 0);
        s->write_in((void*)dims, num_dim, sizeof(int64_t), immutable);
        uint64_t len = 1;
        for (int i = 0; i < num_dim; i++) len *= dims[i];
        for (int i = 0; i < len; i++) 
            if (data[i].num_dim == 0) s->write_in(NULL, 0, sizeof(int64_t), false);
            else j2c_array_copy<ELEMENT_TYPE>::serialize(data[i].num_dim, data[i].dims, data[i].data, s, false);
    }

    static j2c_array<j2c_array<ELEMENT_TYPE> > deserialize(j2c_array_io *s) {
        int64_t *dims;
        uint64_t num_dim, len = 1;
        s->read((void**)&dims, &num_dim);
        for (int i = 0; i < num_dim; i++) len *= dims[i];
        j2c_array<j2c_array<ELEMENT_TYPE> > arr = j2c_array<j2c_array<ELEMENT_TYPE> >(NULL, num_dim, dims);
        for (int i = 0; i < len; i++) arr.ARRAYELEM(i+1) = j2c_array_copy<ELEMENT_TYPE>::deserialize(s);
        return arr;
    }

     static j2c_array<ELEMENT_TYPE>* alloc_elements(uint64_t len) {
        //size_t s = sizeof(j2c_array<ELEMENT_TYPE>) * len;
        //j2c_array<ELEMENT_TYPE> *a = (j2c_array<ELEMENT_TYPE>*)malloc(s);
        //memset(a, 0, s);
        j2c_array<ELEMENT_TYPE> *a = new j2c_array<ELEMENT_TYPE>[len];
        return a;
    }

    static void free_elements(j2c_array<ELEMENT_TYPE>* a, uint64_t len) {
PRINTF("delete array elements %x\n", a);
FLUSH();
#ifdef ALIAS_ANA
        pert_unregister_data((void*)a);
#endif
        //free(a);
        delete [] a;
    }
};

/*
 * A base class for all other templatized j2c_array's.  We use this where we know that a variable is
 * some kind of j2c_array but we don't know the exact type.  This is useful when a void * is coming
 * in from Julia and we cast it as a j2c_array_interface and then invoke the necessary method.
 */
class j2c_array_interface {
public:
    virtual ~j2c_array_interface() {}
    virtual bool addRange(std::set<MemRange> &ranges) = 0;
    virtual uint64_t ARRAYLEN(void) const = 0;
    virtual uint64_t ARRAYSIZE(unsigned i) = 0;
    virtual void decrement(void) = 0;
    virtual void * getData(void) = 0;
    virtual void * being_returned(void) = 0;
    virtual void ARRAYGET(uint64_t i, void *v) = 0;
    virtual void ARRAYSET(uint64_t i, void *v) = 0;
};

// The catchall case for adding a 
template <typename ELEMENT_TYPE>
bool getNested(ELEMENT_TYPE &jai, std::set<MemRange> &ranges) {
    return false;
}

template <typename ELEMENT_TYPE>
bool getNested(j2c_array<ELEMENT_TYPE> &jai, std::set<MemRange> &ranges) {
    return jai.addRange(ranges);
}

template <typename ELEMENT_TYPE>
bool isNested(ELEMENT_TYPE &jai) {
    return false;
}

template <typename ELEMENT_TYPE>
bool isNested(j2c_array<ELEMENT_TYPE> &jai) {
    return true;
}

template <typename ELEMENT_TYPE>
void ARRAYGET(j2c_array<ELEMENT_TYPE> *arr, uint64_t i, void *v) {
    arr->getnonnested(i,v);
}

template <typename ELEMENT_TYPE>
void ARRAYGET(j2c_array<j2c_array<ELEMENT_TYPE> > *arr, uint64_t i, void *v) {
    arr->getnested(i,v);
}

template <typename ELEMENT_TYPE>
class j2c_array : public j2c_array_interface {
protected:
    int64_t* offsets;
    int64_t* max_size;
public:
    ELEMENT_TYPE* data;
    unsigned num_dim;
    int64_t dims[MAX_DIM];
    unsigned *refcount;  // is always NULL if data is not owned.

    virtual void * getData(void) {
        return data;
    }

    void getnonnested(uint64_t i, void *v) {
//        std::cout << "non-nested ARRAYGET v = " << v << std::endl;
        *((ELEMENT_TYPE*)v) = data[i - 1 - offsets[0]];
    }

    void getnested(uint64_t i, void *v) {
//        std::cout << "nested ARRAYGET v = " << v << std::endl;
        *((ELEMENT_TYPE**)v) = &data[i - 1 - offsets[0]];
    }

    /*
     * Add the current j2c_array to the set of other input arrays "ranges".
     * If this array is nested then recursively do the same thing for each
     * nested array.
     * Return value is true if there is an overlap from adding this array, false otherwise.
     */
    bool addRange(std::set<MemRange> &ranges) {
        // Get the beginning and ending address of the data array.
        void *s=NULL, *e=NULL;
        getStartEnd(s,e);
        MemRange jai_mr(s, e);

        // If the new range jai_mr is found in the ranges set then there is an overlap so return true.
        if (ranges.find(jai_mr) != ranges.end()) {
            return true;
        }

        // There is no overlap so add it to the ranges to test if later arrays overlap.
        ranges.insert(jai_mr);

        unsigned this_len = ARRAYLEN();
        // No need to recurse if the array is 0 length.
        if (this_len == 0) {
            return false;
        }
        // No need to recurse if the array is not nested.
        if (!isNested(data[0])) {
            return false;
        }

        // For each nested array, call getNested to add that array's range.
        for (int i = 0; i < this_len; i++) {
            if (getNested(data[i], ranges)) {
                return true;
            } 
        }

        // If we get here there are no overlaps so return false.
        return false;
    }

    void getStartEnd(void * &start, void * &end) const {
        start = data;
        end   = ((char*)(data + ARRAYLEN())) - 1;
        if (end < start) {
            end = start;
        }
    }

    void decrement(void) {
        if (refcount) {
PRINTF("decrement %x => %d - 1\n", data, *refcount);
FLUSH();
#ifdef DEBUGJ2C
            g_refcount_stats.dec();
#endif
            unsigned orig_value = __sync_fetch_and_sub(refcount, 1);
            if(orig_value == 1) {
              if (data != NULL) {
                 j2c_array_copy<ELEMENT_TYPE>::free_elements(data, ARRAYLEN());
//                  pert_unregister_data((void*)data);
              }
              delete refcount;
              data = NULL;
              refcount = NULL;
            }
PRINTF("decrement done\n");
FLUSH();
        }
    }

    void increment(void) {
      if (refcount) {
PRINTF("increment %x => %d + 1\n", data, *refcount);
FLUSH();
#ifdef DEBUGJ2C
        g_refcount_stats.inc();
#endif
        __sync_fetch_and_add(refcount, 1);
      }
    }

    j2c_array() : data(NULL), refcount(NULL), offsets(global_zero), max_size(dims) {
PRINTF("default constructor called on %x\n", this);
FLUSH();
}

    j2c_array(const j2c_array<ELEMENT_TYPE> &rhs) :
        data(rhs.data),
        num_dim(rhs.num_dim),
        refcount(rhs.refcount)
    {
        if (rhs.offsets != NULL && rhs.offsets != global_zero) offsets = rhs.offsets;
        else offsets = global_zero;
        if (rhs.max_size != NULL && rhs.max_size != rhs.dims) max_size = rhs.max_size;
        else max_size = dims;
        increment();
        memcpy(dims, rhs.dims, sizeof(dims));
    }

    j2c_array<ELEMENT_TYPE> & operator=(const j2c_array<ELEMENT_TYPE> &rhs) {
PRINTF("j2c_array assignment to %x refcount = %x\n", this, refcount);
FLUSH();
        decrement();
        data = rhs.data;
        num_dim = rhs.num_dim;
        refcount = rhs.refcount;
        memcpy(dims, rhs.dims, sizeof(dims));
        if (rhs.offsets != NULL && rhs.offsets != global_zero) offsets = rhs.offsets;
        else offsets = global_zero;
        if (rhs.max_size != NULL && rhs.max_size != rhs.dims) max_size = rhs.max_size;
        else max_size = dims;
        increment();
        return *this;
    }

    // Used in the source if we have something of the form "a = b;" where we know that "b" is not used afterwards.
    j2c_array<ELEMENT_TYPE> & assign_from_dead_rhs(j2c_array<ELEMENT_TYPE> &rhs) {
PRINTF("j2c_array assignment to %x refcount = %x\n", this, refcount);
FLUSH();
        decrement();
        data = rhs.data;
        num_dim = rhs.num_dim;
        refcount = rhs.refcount;
        memcpy(dims, rhs.dims, sizeof(dims));
        if (rhs.offsets != NULL && rhs.offsets != global_zero) offsets = rhs.offsets;
        else offsets = global_zero;
        if (rhs.max_size != NULL && rhs.max_size != rhs.dims) max_size = rhs.max_size;
        else max_size = dims;
        // No need to increment the refcount since we are swapping ownership of the reference from rhs to lhs.
        // RHS is dead now so NULL out data and refcount so that when RHS goes out of scope that it won't try to
        // decrement the refcount again.
        rhs.data = NULL;
        rhs.refcount = NULL;
        return *this;
    }

// C++11
#if __cplusplus >= 201103L
    // Used in the source if we have something of the form "a = b;" where we know that "b" is not used afterwards.
    j2c_array(j2c_array<ELEMENT_TYPE> &&rhs) :
        data(rhs.data),
        num_dim(rhs.num_dim),
        refcount(rhs.refcount)
    {
//printf("Move constructor\n");
        if (rhs.offsets != NULL && rhs.offsets != global_zero) offsets = rhs.offsets;
        else offsets = global_zero;
        if (rhs.max_size != NULL && rhs.max_size != rhs.dims) max_size = rhs.max_size;
        else max_size = dims;
        memcpy(dims, rhs.dims, sizeof(dims));
        // No need to increment the refcount since we are swapping ownership of the reference from rhs to lhs.
        // RHS is dead now so NULL out data and refcount so that when RHS goes out of scope that it won't try to
        // decrement the refcount again.
        rhs.data = NULL;
        rhs.refcount = NULL;
    }

    j2c_array<ELEMENT_TYPE> & operator=(j2c_array<ELEMENT_TYPE> &&rhs) {
//printf("Move assignment\n");
        decrement();
        data = rhs.data;
        num_dim = rhs.num_dim;
        refcount = rhs.refcount;
        memcpy(dims, rhs.dims, sizeof(dims));
        if (rhs.offsets != NULL && rhs.offsets != global_zero) offsets = rhs.offsets;
        else offsets = global_zero;
        if (rhs.max_size != NULL && rhs.max_size != rhs.dims) max_size = rhs.max_size;
        else max_size = dims;
        // No need to increment the refcount since we are swapping ownership of the reference from rhs to lhs.
        // RHS is dead now so NULL out data and refcount so that when RHS goes out of scope that it won't try to
        // decrement the refcount again.
        rhs.data = NULL;
        rhs.refcount = NULL;
        return *this;
    }
#endif // C++11

    j2c_array(ELEMENT_TYPE* _data, unsigned _num_dim, int64_t *_dims) {
        assert(_num_dim <= MAX_DIM);
        num_dim = _num_dim;
        uint64_t len = 1;
        for (unsigned i = 0; i < _num_dim; i++) { dims[i] = _dims[i]; len *= _dims[i]; }
        if (_data == NULL) {
            data = j2c_array_copy<ELEMENT_TYPE>::alloc_elements(len);
#ifdef ALIAS_ANA
      int64_t *max_size = (int64_t*)malloc(sizeof(int64_t)*_num_dim);
      for (unsigned i = 0; i < _num_dim; i++) {
        max_size[i] = dims[i];
      }
      pert_register_data( (void*)(data), false, _num_dim, max_size, sizeof(ELEMENT_TYPE) );
#endif
        }
        else data = _data;
        if (_data == NULL) { 
            refcount = new unsigned;
            *(refcount) = 1;
        }
        else {
            refcount = NULL;
        }
        offsets = global_zero;
        max_size = dims;
    }

    uintptr_t inline to_mic(const int run_where, int64_t _num_dim, int64_t lower[], int64_t upper[]) {
        uintptr_t _arr, _data;
        assert(num_dim == _num_dim);
// #pragma offload target(mic:run_where) in(dims) out(_arr) out(_data)
        {
            j2c_array<ELEMENT_TYPE> *tmp = new j2c_array<ELEMENT_TYPE>(NULL, num_dim, dims);
            _arr = (uintptr_t)tmp;
            _data = (uintptr_t)tmp->data;
        }
        // we always copy contiguous region
        int64_t start = 0;
        int64_t end = 0;
        for (int i = _num_dim - 1; i >= 0; i--)
        {
            if (i < _num_dim - 1)
            {
                start *= max_size[i];
                end *= max_size[i];
            }
            start += lower[i];
            end += upper[i];
PRINTF("to_mic lower[%d]=%d upper[%d]=%d\n", i, lower[i], i, upper[i]);
FLUSH();
        }
PRINTF("to_mic start = %d end = %d len=%d\n", start, end, ARRAYLEN());
FLUSH();
        j2c_array_copy<ELEMENT_TYPE>::copy_to_mic(run_where, _data, data, 0, ARRAYLEN()); //start, 1 + end - start);
        return _arr;
    }

    uintptr_t inline to_mic(const int run_where) {
        uintptr_t _arr, _data;
// #pragma offload target(mic:run_where) in(dims) out(_arr) out(_data)
        {
            j2c_array<ELEMENT_TYPE> *tmp = new j2c_array<ELEMENT_TYPE>(NULL, num_dim, dims);
            _arr = (uintptr_t)tmp;
            _data = (uintptr_t)tmp->data;
        }
        j2c_array_copy<ELEMENT_TYPE>::copy_to_mic(run_where, _data, data, 0, ARRAYLEN());
        return _arr;
    }

    // NOTE: lower and upper are 0 based indices
    void copy_block0(j2c_array<ELEMENT_TYPE> &_from, int64_t *lower, int64_t *upper) {
        assert(num_dim == _from.num_dim);
        // we always copy contiguous region
        int64_t idx[num_dim];
		bool in_range = false;
        for (int i = 0; i < num_dim; i++) {
            idx[i] = lower[i];
            in_range |= upper[i] >= lower[i];
        }
        if (!in_range) return;
        bool done = false;
        while (true) {
            ARRAYELEM0(idx) = _from.ARRAYELEM0(idx);
            int i = 0; 
            while (i < num_dim) {
                if (idx[i] < upper[i]) { idx[i]++; break; }
                idx[i] = lower[i];
                if (i + 1 < num_dim) { i++; }
                else { done = true; break; }
            }
            if (done) break;
        }
    }

    void copy_block0(j2c_array<ELEMENT_TYPE> &_from, int64_t _num_dim, int64_t *lower, int64_t *upper) {
        assert(num_dim == _num_dim);
        copy_block0(_from, lower, upper);
    }

    void from_mic_into(const int run_where, uintptr_t obj, int64_t _num_dim, int64_t *lower, int64_t *upper) {
        assert(num_dim == _num_dim);
        int64_t obj_num_dim;
        uintptr_t _data;
// #pragma offload target(mic:run_where) out(_data) out(obj_num_dim)
        {
            j2c_array<ELEMENT_TYPE> *tmp = (j2c_array<ELEMENT_TYPE>*)obj;
            _data = (uintptr_t)tmp->data;
            obj_num_dim = tmp->num_dim;
        }
        assert(num_dim == obj_num_dim);
        // we always copy contiguous region
        int64_t start = 0;
        int64_t end = 0;
        for (int i = _num_dim - 1; i >= 0; i--)
        {
            if (i < _num_dim - 1)
            {
                start *= max_size[i];
                end *= max_size[i];
            }
            start += lower[i];
            end += upper[i];
PRINTF("from_mic lower[%d]=%d upper[%d]=%d\n", i, lower[i], i, upper[i]);
FLUSH();
        }
PRINTF("from_mic start = %d end = %d len=%d\n", start, end, ARRAYLEN());
FLUSH();
        j2c_array_copy<ELEMENT_TYPE>::copy_from_mic(run_where, data, _data, start, 1 + end - start);
        //j2c_array_copy<ELEMENT_TYPE>::copy_from_mic(run_where, data, _data, 0, ARRAYLEN());
    }

    static inline j2c_array<ELEMENT_TYPE> from_mic(const int run_where, uintptr_t obj) {
        unsigned num_dim, len;
        int64_t dims[MAX_DIM], *tmpdims;
        uintptr_t data;
// #pragma offload target(mic:run_where) out(data) out(num_dim) out(tmpdims[0:MAX_DIM]:into(dims[0:MAX_DIM]) preallocated alloc_if(1))
        {
            j2c_array<ELEMENT_TYPE> *tmp = (j2c_array<ELEMENT_TYPE>*)obj;
            data = (uintptr_t)tmp->data;
            num_dim = tmp->num_dim;
            tmpdims = tmp->dims;
            len = tmp->ARRAYLEN();
        }
PRINTF("dims[0] = %d\n", dims[0]);
FLUSH();
        j2c_array<ELEMENT_TYPE> arr = j2c_array<ELEMENT_TYPE>(NULL, num_dim, dims);
        j2c_array_copy<ELEMENT_TYPE>::copy_from_mic(run_where, arr.data, data, 0, len);
        return arr;
    }

   ~j2c_array(void) {
PRINTF("j2c_array destructor %x data = %x refcount = %p\n", this, data, refcount);
FLUSH();
        decrement();
    }

    void * being_returned(void) {
        increment();
        return data;
    }

    void apply_arg_offset(int64_t *_offsets, int64_t *_max_size) {
        offsets  = _offsets;
        max_size = _max_size;
    }

    static j2c_array<ELEMENT_TYPE> new_j2c_array_1d(ELEMENT_TYPE* _data, int64_t _N1) {
        int64_t dims[1] = { _N1 };
        return j2c_array<ELEMENT_TYPE>(_data, 1, dims);
    }

    static j2c_array<ELEMENT_TYPE> new_j2c_array_2d(ELEMENT_TYPE* _data, int64_t _N1, int64_t _N2) {
        int64_t dims[2] = { _N1, _N2 };
        return j2c_array<ELEMENT_TYPE>(_data, 2, dims);
    }

    static j2c_array<ELEMENT_TYPE> new_j2c_array_3d(ELEMENT_TYPE* _data, int64_t _N1, int64_t _N2, int64_t _N3) {
        int64_t dims[3] = { _N1, _N2, _N3 };
        return j2c_array<ELEMENT_TYPE>(_data, 3, dims);
    } 

    static j2c_array<ELEMENT_TYPE> new_j2c_array_4d(ELEMENT_TYPE* _data, int64_t _N1, int64_t _N2, int64_t _N3, int64_t _N4) {
        int64_t dims[4] = { _N1, _N2, _N3, _N4 };
        return j2c_array<ELEMENT_TYPE>(_data, 4, dims);
    }

    /* raw reference into array data without any index offset */
    ELEMENT_TYPE* ARRAYELEMREF(uint64_t i) {
        return &(data[i - 1]);
    }

    ELEMENT_TYPE& ARRAYELEM(uint64_t i) {
        return data[i - 1 - offsets[0]];
    }

    void ARRAYGET(uint64_t i, void *v) {
        ::ARRAYGET(this, i, v);
    }

    void ARRAYSET(uint64_t i, void *v) {
        data[i - 1 - offsets[0]] = *(ELEMENT_TYPE*)v;
    }

    ELEMENT_TYPE& ARRAYELEM(uint64_t i, uint64_t j) {
        return data[((j - 1 - offsets[1]) * max_size[0]) + i - 1 - offsets[0]];
    }

    ELEMENT_TYPE& ARRAYELEM(uint64_t i, uint64_t j, uint64_t k) {
        return data[(((k - 1 - offsets[2]) * max_size[1] + j - 1 - offsets[1]) * max_size[0]) + i - 1 - offsets[0]];
    }

    ELEMENT_TYPE& ARRAYELEM(uint64_t i, uint64_t j, uint64_t k, uint64_t l) {
        return data[((((l - 1 - offsets[3]) * max_size[2] + k - 1 - offsets[2]) * max_size[1] + j - 1 - offsets[1]) * max_size[0]) + i - 1 - offsets[0]];
    }

    ELEMENT_TYPE& ARRAYELEM(uint64_t i, uint64_t j, uint64_t k, uint64_t l, uint64_t m) {
        return data[(((((m - 1 - offsets[4]) * max_size [3] + l - 1 - offsets[3]) * max_size[2] + k - 1 - offsets[2]) * max_size[1] + j - 1 - offsets[1]) * max_size[0]) + i - 1 - offsets[0]];
    }

    // 0 based indice
    ELEMENT_TYPE& ARRAYELEM0(int64_t *idx) {
        uint64_t i = 0;
        for (int k = num_dim - 1; k >= 0; k--) {
           if (k < num_dim - 1) i *= max_size[k];
           i += idx[k] - offsets[k];
        }
        return data[i];
    }

    ELEMENT_TYPE& ARRAYELEM(uint64_t *idx) {
        uint64_t i = 0;
        for (int k = num_dim - 1; k >= 0; k--) {
           if (k < num_dim - 1) i *= max_size[k];
           i += idx[k] - 1 - offsets[k];
        }
        return data[i];
    }

    ELEMENT_TYPE& SAFEARRAYELEM(ELEMENT_TYPE d, uint64_t i) {
        return ((i >= 1 && i <= ARRAYLEN()) ? data[i - 1 - offsets[0]] : d);
    }

    ELEMENT_TYPE SAFEARRAYELEM(ELEMENT_TYPE d, uint64_t i, uint64_t j) {
        return ((i >= 1 && i <= dims[0] && j >= 1 && j <= dims[1]) ? data[((j - 1 - offsets[1]) * max_size[0]) + i - 1 - offsets[0]] : d);
    }

    ELEMENT_TYPE SAFEARRAYELEM(ELEMENT_TYPE d, uint64_t i, uint64_t j, uint64_t k) {
        return ((i >= 1 && i <= dims[0] && j >= 1 && j <= dims[1] && k >= 1 && k <= dims[2]) ? 
                data[(((k - 1 - offsets[2]) * max_size[1] + j - 1 - offsets[1]) * max_size[0]) + i - 1 - offsets[0]] : d);
    }

    uint64_t ARRAYLEN(void) const {
        uint64_t ret = dims[0];
        int i;
        for(i = 1; i < num_dim; ++i) {
            ret *= dims[i];
        }
        return ret;
    }

    uint64_t ARRAYSIZE(unsigned i) {
        return dims[i-1];
    }

    void ARRAYBOUNDSCHECK(uint64_t i) {
        assert(i >= 1 && i <= ARRAYLEN()); // Use ARRAYLEN here since we could be indexing N-D arrays using a linear index.
    }

    void ARRAYBOUNDSCHECK(uint64_t i, uint64_t j) {
        assert(i >= 1 && i <= dims[0] && j >= 1 && j <= dims[1]);
        //assert((j - 1)*dims[0] + i >= 1 && (j - 1)*dims[0] + i <= ARRAYLEN());
    }

    void ARRAYBOUNDSCHECK(uint64_t i, uint64_t j, uint64_t k) {
        assert(i >= 1 && i <= dims[0] && j >= 1 && j <= dims[1] && k >= 1 && k <= dims[2]);
    }

    std::string dump() {
        std::stringstream s;
        if (num_dim == 0) { s << "[]"; return s.str(); }
        s << "[";
        for (int i = 0; i < num_dim; i++) {
            s << dims[i] << (i == num_dim - 1 ? "" : ":");
        }
        s << "](";
        if (refcount != NULL) s << *refcount;
        s << ") = [";
        uint64_t len = ARRAYLEN();
        for (int i = 0; i < len; i++) {
            j2c_array_copy<ELEMENT_TYPE>::dump_element(&s, data[i]);
            s << ((i == len - 1) ? "" : ",");
        }
        s << "]";
        return s.str();
    }
};

template <typename ELEMENT_TYPE>
uint64_t TOTALSIZE(j2c_array<ELEMENT_TYPE> &array) {
    return array.ARRAYLEN() * sizeof(ELEMENT_TYPE);
}

template <typename ELEMENT_TYPE>
uint64_t TOTALSIZE(j2c_array< j2c_array<ELEMENT_TYPE> > &array) {
    uint64_t len = array.ARRAYLEN();
    uint64_t ret = len * sizeof(j2c_array<ELEMENT_TYPE>);
    unsigned i;
    for (i = 0; i < len; ++i) {
        ret += TOTALSIZE(array.data[i]);
    }
    return ret;
}

#if 0
extern "C" // DLLEXPORT
int j2c_array_bytesize()
{
    return sizeof(j2c_array<uintptr_t>);
}
#endif

#if 0
extern "C" // DLLEXPORT
void *j2c_array_new(int elem_bytes, void *data, unsigned ndim, int64_t *dims)
{
    void *a = NULL;
    switch (elem_bytes)
    {
    case 0: // special case for array of array
        a = new j2c_array<j2c_array_interface *>((j2c_array_interface **)data, ndim, dims);
        break;
    case 1:
        a = new j2c_array<int8_t>((int8_t*)data, ndim, dims);
        break;
    case 2:
        a = new j2c_array<int16_t>((int16_t*)data, ndim, dims);
        break;
    case 4:
        a = new j2c_array<int32_t>((int32_t*)data, ndim, dims);
        break;
    case 8:
        a = new j2c_array<int64_t>((int64_t*)data, ndim, dims);
        break;
    case 16:
        a = new j2c_array<double _Complex>((double _Complex *)data, ndim, dims);
        break;
    default:
        fprintf(stderr, "J2C Array does not support %d-byte  element size.", elem_bytes);
        assert(false);
        break;
    }
    return a;
}
#endif

/* own means the caller wants to own the pointer */
extern "C" // DLLEXPORT
void* j2c_array_to_pointer(void *arr, bool own)
{
   j2c_array_interface *jai = (j2c_array_interface*)arr;
   if (own) {
     return jai->being_returned();
   }
   else {
     return jai->getData();
   }
}

extern "C" // DLLEXPORT
unsigned j2c_array_length(void *arr)
{
    j2c_array_interface *jai = (j2c_array_interface*)arr;
    return jai->ARRAYLEN();
}

extern "C" // DLLEXPORT
unsigned j2c_array_size(void *arr, unsigned dim)
{
    j2c_array_interface *jai = (j2c_array_interface*)arr;
    return jai->ARRAYSIZE(dim);
}

/* In case that elem_bytes is 0, value is set to a pointer into the data of
 * the input array without any copying. */
extern "C" // DLLEXPORT
void j2c_array_get(int elem_bytes, void *arr, unsigned idx, void *value)
{
    j2c_array_interface *jai = (j2c_array_interface*)arr;
    jai->ARRAYGET(idx, value);
}

/* in the case elem_bytes = 0, value is a pointer to a j2c_array object */
extern "C" // DLLEXPORT
void j2c_array_set(int elem_bytes, void *arr, unsigned idx, void *value)
{
    j2c_array_interface *jai = (j2c_array_interface*)arr;
    jai->ARRAYSET(idx, value);
}


/* Only delete this array object, no nested deletion */
extern "C" // DLLEXPORT
void j2c_array_delete(void* a)
{
    j2c_array_interface *jai = (j2c_array_interface*)a;
//    std::cout << "j2c_array_delete this = " << jai << std::endl;
    delete jai;
}

/* Deref without deletion */
extern "C" // DLLEXPORT
void j2c_array_deref(void* a)
{
    j2c_array_interface *jai = (j2c_array_interface*)a;
    jai->decrement();
}

/*
 * Calls to this function are inserted by j2c to test for aliasing of the input array parameters.
 * Returns true if there is aliasing, false otherwise.
 * The std::array param lets us pass the input arrays in the form "{ &array1, &array2, ..., &arrayN }".
 */
template <std::size_t N>
bool j2c_alias_test(const std::array<j2c_array_interface *, N> &jai) {
    std::set<MemRange> ranges;
    unsigned i;

    // For each input array, try to add its range and if that calls reports an overlap then report an overlap here as well.
    for (i = 0; i < jai.size(); ++i) {
        if (jai[i]->addRange(ranges)) return true;
    }

    // If we get here then all arrays are added with no overlap.
    return false;
}

#endif /* PSE_ARRAY_H_ */
