#ifndef BUCKET_H_
#define BUCKET_H_

/*
 * Bucket sieving: radix-sort sieve updates as they are created.
 */

#include <stdint.h>
#include "cado-endian.h"
// #define SAFE_BUCKETS
#ifdef SAFE_BUCKETS
#include <stdio.h>
#include "portability.h"
#endif
#include "misc.h"
#include "fb-types.h"
#include "fb.h"

/*
 * This bucket module provides a way to store elements (that are called
 * updates), while partially sorting them, according to some criterion (to
 * be defined externally): the incoming data is stored into several
 * buckets. The user says for each data to which bucket it belongs. This
 * module is supposed to perform this storage in a cache-friendly way and
 * so on...
 */

/*
 * Main available commands are 
 *   push_bucket_update(i, x)  :   add x to bucket number i
 *   get_next_bucket_update(i) :   iterator to read contents of bucket number i
 *
 * See the MAIN FUNCTIONS section below for the complete interface, with
 * prototypes of exported functions.
 */

/********  Data structure for the contents of buckets **************/

/* In principle, the typedef for the bucket_update can be changed without
 * affecting the rest of the code. 
 */

/*
 * For the moment, we store the bucket updates and a 16-bit field
 * that can contain, for instance, the low bits of p.
 */

/* THE ORDER IS MANDATORY! */
typedef struct {
#ifdef CADO_LITTLE_ENDIAN
    slice_offset_t hint;
    uint16_t x;
#else
    uint16_t x;
    slice_offset_t hint;
#endif
} bucket_update_t;

/* Same than previous, but 1 byte more for indirection.
   Careful: this byte is the first in little endian, and
   the last byte in big endian.
   k = kilo bucket */
typedef uint8_t k_bucket_update_t[1 + sizeof(bucket_update_t)];

/* Same than previous, but with 2 bytes (double 1 byte indirections).
   m = mega bucket */
typedef uint8_t m_bucket_update_t[2 + sizeof(bucket_update_t)];


/* An update with the complete prime, generated by line re-sieving */

typedef struct {
    uint16_t x;
    uint32_t p;
} bucket_prime_t;

/* When purging a bucket, we don't store pointer arrays to indicate where in
   the puged data a new slice begins, as each slice will have only very few
   updates surviving. Instead, we re-write each update to store both slice
   index and offset. */

typedef struct {
    uint16_t x;
    slice_index_t index;
    slice_offset_t hint;
} bucket_complete_update_t;


/*
 * will be used as a sentinel
 */
static const bucket_update_t LAST_UPDATE = {0,0};

/******** Bucket array typedef **************/
struct bucket_array_t {
  bucket_update_t ** bucket_write;    // Contains pointers to first empty
                                      // location in each bucket
  bucket_update_t ** bucket_start;    // Contains pointers to beginning of
                                      // buckets
  bucket_update_t ** bucket_read;     // Contains pointers to first unread
                                      // location in each bucket
  slice_index_t    * slice_index;     // For each slice that gets sieved,
                                      // new index is added here
  bucket_update_t ** slice_start;     // For each slice there are
                                      // n_bucket pointers, each pointer
                                      // tells where in the corresponding 
                                      // bucket the updates from that slice
                                      // start
  uint32_t           n_bucket;        // Number of buckets
  uint64_t           bucket_size;     // The allocated size of one bucket.
  size_t             size_b_align;    // (sizeof(void *) * n_bucket + 63) & ~63
                                      // to align bucket_* on cache line
  size_t big_size;                    // size of bucket update memory
  slice_index_t      nr_slices;       // Number of different slices
  slice_index_t      alloc_slices;    // number of checkpoints (each of size
                                      // size_b_align) we have allocated

  static const slice_index_t initial_slice_alloc = 256;
  static const slice_index_t increase_slice_alloc = 128;

private:
  /* Get a pointer to the pointer-set for the i_slice-th slice */
  bucket_update_t ** get_slice_pointers(const slice_index_t i_slice) const {
    ASSERT_ALWAYS(i_slice < nr_slices);
    ASSERT_ALWAYS(size_b_align % sizeof(bucket_update_t *) == 0);
    return (slice_start + i_slice * size_b_align / sizeof(bucket_update_t *));
  }
  void realloc_slice_start(size_t);
public:
  bucket_array_t() : bucket_write(NULL), n_bucket(0), bucket_size(0) {}

  /* Return a begin iterator over the bucket_update_t entries in i_bucket-th
     bucket, generated by the i_slice-th slice */
  const bucket_update_t *begin(const size_t i_bucket, const slice_index_t i_slice) const {
    ASSERT_ALWAYS(i_slice < nr_slices);
    return get_slice_pointers(i_slice)[i_bucket];
  }

  /* Return an end iterator over the bucket_update_t entries in i_bucket-th
     bucket, generated by the i_slice-th slice */
  const bucket_update_t *end(const size_t i_bucket, const slice_index_t i_slice) const {
    ASSERT_ALWAYS(i_slice < nr_slices);
    return (i_slice + 1 < nr_slices) ? get_slice_pointers(i_slice + 1)[i_bucket] :
      bucket_write[i_bucket];
  }
  slice_index_t get_slice_index(const slice_index_t i_slice) const {
    ASSERT_ALWAYS(i_slice < nr_slices);
    return slice_index[i_slice];
  }
  void
  add_slice_index(const slice_index_t new_slice_index)
  {
    /* Write new set of pointers for the new factor base slice */
    ASSERT_ALWAYS(nr_slices <= alloc_slices);
    if (nr_slices == alloc_slices) {
      /* We're out of allocated space for the checkpoints. Realloc to bigger
         size. We add space for increase_slice_alloc additional entries. */
      realloc_slice_start(increase_slice_alloc);
    }
    aligned_medium_memcpy((uint8_t *)slice_start + size_b_align * nr_slices, bucket_write, size_b_align);
    slice_index[nr_slices++] = new_slice_index;
  }
};

/* Same than previous, for kilo & mega buckets in fill_in_k/m_buckets.
   These buckets are used only for 2 and 3 sort passes buckets */ 
typedef struct {
  k_bucket_update_t ** bucket_write;
  k_bucket_update_t ** bucket_start;
  k_bucket_update_t ** logp_idx;
  uint32_t             n_bucket;
  uint64_t             bucket_size;
  size_t               size_b_align;
} k_bucket_array_t;

typedef struct {
  m_bucket_update_t ** bucket_write;
  m_bucket_update_t ** bucket_start;
  m_bucket_update_t ** logp_idx;
  uint32_t             n_bucket;
  uint64_t             bucket_size;
  size_t               size_b_align;
} m_bucket_array_t;


/* A class that stores updates in a single "bucket".
   It's really just a container class with pre-allocated array for storage,
   a persistent read and write pointer. */
template <class UPDATE_TYPE>
class bucket_single {
  UPDATE_TYPE *start; /* start is a "strong" reference */
  UPDATE_TYPE *read;  /* read and write are "weak" references into the allocated memory */
  UPDATE_TYPE *write;
  size_t _size;
public:
  bucket_single (const size_t size) : _size(size)
  {
    start = new UPDATE_TYPE[size];
    read = start;
    write = start;
  }

  ~bucket_single() {
    delete[] start;
    start = read  = write = NULL;
    _size = 0;
  }

  /* A few of the standard container methods */
  const UPDATE_TYPE * begin() const {return start;}
  const UPDATE_TYPE * end() const {return write;}
  size_t size() const {return write - start;}

  /* Main writing function: appends update to bucket number i.
   * If SAFE_BUCKETS is not #defined, then there is no checking that there is
   * enough room for the update. This could lead to a segfault, with the
   * current implementation!
   */
  void push_update (const UPDATE_TYPE &update)
  {
      *(write++) = update;
#ifdef SAFE_BUCKETS
      if (start + _size <= write) {
          fprintf(stderr, "# Warning: hit end of bucket\n");
          write--;
      }
#endif
  }
  const UPDATE_TYPE &get_next_update () {
#ifdef SAFE_BUCKETS
    ASSERT_ALWAYS (read < write);
#endif
    return *read++; 
  }
  void rewind_by_1() {if (read > start) read--;}
  bool is_end() const { return read == write; }

  void sort ();
};

/* Stores info containing the complete prime instead of only the low 16 bits */

class bucket_primes_t : public bucket_single<bucket_prime_t> {
    typedef bucket_single<bucket_prime_t> super;
public:  
  bucket_primes_t (const size_t size) : super(size){}
  ~bucket_primes_t(){}
  void purge (const bucket_array_t BA, 
          int i, const fb_part *fb, const unsigned char *S);
};

/* Stores info containing both slice index and offset instead of only the offset */

class bucket_array_complete : public bucket_single<bucket_complete_update_t> {
    typedef bucket_single<bucket_complete_update_t> super;
public:  
  bucket_array_complete (const size_t size) : super(size){}
  ~bucket_array_complete(){}
  void purge (const bucket_array_t BA, int i, const unsigned char *S);
};


/* Compute a checksum over the bucket region.

   We import the bucket region into an mpz_t and take it modulo
   checksum_prime. The checksums for different bucket regions are added up,
   modulo checksum_prime. This makes the combined checksum independent of
   the order in which buckets are processed, but it is dependent on size of
   the bucket region. Note that the selection of the sieve region, i.e., of J
   depends somewhat on the number of threads, as we want an equal number of
   bucket regions per thread. Thus the checksums are not necessarily
   comparable between runs with different numbers of threads. */

class sieve_checksum {
  static const unsigned int checksum_prime = 4294967291u; /* < 2^32 */
  unsigned int checksum;
  void update(const unsigned int);

  public:
  sieve_checksum() : checksum(0) {}
  unsigned int get_checksum() {return checksum;}

  /* Combine two checksums */ 
  void update(const sieve_checksum &other) {
    /* Simply (checksum+checksum2) % checksum_prime, but using
       ularith_addmod_ul_ul() to handle sums >= 2^32 correctly. */
    this->update(other.checksum);
  }
  /* Update checksum with the pointed-to data */
  void update(const unsigned char *, size_t);
};


/******** MAIN FUNCTIONS **************/

/* Set an allocated array of <nb_buckets> buckets each having size
 * max_bucket_fill_ratio * BUCKET_REGION.
 * Must be freed with clear_buckets().
 */
extern void init_buckets(bucket_array_t *BA, k_bucket_array_t *kBA, m_bucket_array_t *mBA,
                         double max_bucket_fill_ratio, uint32_t nb_buckets);
/* Only one (clever) function to clear the buckets. */
extern void clear_buckets(bucket_array_t *BA, k_bucket_array_t *kBA, m_bucket_array_t *mBA);


/* Main reading iterator: returns the first unread update in bucket i.
 * If SAFE_BUCKETS is not #defined, there is no checking that you are reading
 * at most as much as what you wrote. If keeping the count while pushing
 * is impossible, the following functions can help:
 * - get the count after the last push using nb_of_updates()
 * - put a sentinel after your bucket using push_sentinel() and check if
 * the value returned by get_next_bucket_update() is LAST_UPDATE.
 * - call the is_end() functions that tells you if you can apply
 *   get_next_bucket_update().
 */
static inline bucket_update_t
get_next_bucket_update(bucket_array_t BA, const int i);
static inline int
nb_of_updates(const bucket_array_t BA, const int i);
static inline void
push_sentinel(bucket_array_t BA, const int i);
static inline int
is_end(const bucket_array_t BA, const int i);

/* If you need to read twice a bucket, you can rewind it: */
static inline void
rewind_bucket(bucket_array_t BA, const int i);

/* If you want to read the most recently read update again, rewind by 1: */
static inline void
rewind_bucket_by_1 (bucket_array_t BA, const int i);

/* If you want to access updates in a non-sequential way: */
static inline bucket_update_t
get_kth_bucket_update(const bucket_array_t BA, const int i, const int k);

/* Functions for handling entries with x and complete prime p */

extern size_t bucket_misalignment(const size_t sz, const size_t sr);

/* We also forward-define some auxiliary functions which are defined in
 * bucket.c (alongside with the non-inlined functions already listed).
 */
extern double buckets_max_full (const bucket_array_t BA);


/******** Bucket array implementation **************/

#include "utils/misc.h"

static inline void
push_bucket_update(bucket_array_t BA, const int i, 
                   const bucket_update_t update)
{
    *(BA.bucket_write[i])++ = update; /* Pretty! */
    if (0)
      printf("Pushed (x=%u, hint=%u) to bucket %d, nr_slices = %u\n",
             update.x, update.hint, i, (unsigned int) BA.nr_slices);
#ifdef SAFE_BUCKETS
    if (BA.bucket_start[i] + BA.bucket_size <= BA.bucket_write[i]) {
        fprintf(stderr, "# Warning: hit end of bucket nb %d\n", i);
        BA.bucket_write[i]--;
    }
#endif
}

static inline void
rewind_bucket(bucket_array_t BA, const int i)
{
    BA.bucket_read[i] = BA.bucket_start[i];
}

static inline void
rewind_bucket_by_1 (bucket_array_t BA, const int i)
{
  if (BA.bucket_read[i] > BA.bucket_start[i])
    BA.bucket_read[i]--;
}

static inline bucket_update_t
get_next_bucket_update(bucket_array_t BA, const int i)
{
    bucket_update_t rep = *(BA.bucket_read[i])++;
#ifdef SAFE_BUCKETS
    if (BA.bucket_read[i] > BA.bucket_write[i]) {
        fprintf(stderr, "# Warning: reading too many updates in bucket nb %d\n", i);
        BA.bucket_read[i]--;
        return LAST_UPDATE;
    }
#endif
    return rep;
}

static inline bucket_update_t
get_kth_bucket_update(const bucket_array_t BA, const int i, const int k)
{
    bucket_update_t rep = (BA.bucket_start[i])[k];
#ifdef SAFE_BUCKETS
    if (BA.bucket_start[i] + k >= BA.bucket_write[i]) {
        fprintf(stderr, "# Warning: reading outside valid updates in bucket nb %d\n", i);
        return LAST_UPDATE;
    }
#endif
    return rep;
}

static inline int
nb_of_updates(const bucket_array_t BA, const int i)
{
    return (BA.bucket_write[i] - BA.bucket_start[i]);
}

static inline void
push_sentinel(bucket_array_t BA, const int i)
{
    push_bucket_update(BA, i, LAST_UPDATE);
}

static inline int
is_end(const bucket_array_t BA, const int i)
{
    return (BA.bucket_read[i] == BA.bucket_write[i]);
}

#endif	/* BUCKET_H_ */
