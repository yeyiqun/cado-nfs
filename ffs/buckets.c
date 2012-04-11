#include <stdlib.h>
#include <string.h>

#include "buckets.h"
#include "ijvec.h"
#include "macros.h"



/* Bucket updates.
 *
 * Each update contains:
 * - pos:  the position (i, j) of the update, represented using an unsigned
 *         integer of UPDATE_POS_BITS bits.
 * - hint: a prime hint, to quickly recover the degree of the corresponding
 *         prime ideal when applying the update. The hint is represented using
 *         an unsigned integer of UPDATE_HINT_BITS bits.
 *
 * The actual size in memory of a bucket update is UPDATE_POS_BITS +
 * UPDATE_HINT_BITS, rounded up to a multiple of UPDATE_ALIGN bytes.
 *****************************************************************************/

// Size of the two fields and of a bucket update, in bits.
#define UPDATE_POS_BITS  14
#define UPDATE_HINT_BITS  0
#define UPDATE_BITS     (UPDATE_POS_BITS + UPDATE_HINT_BITS)

// Memory alignment of bucket updates, in bytes.
#define UPDATE_ALIGN      1

// Smallest unsigned integer in which an update will fit.
#if   UPDATE_BITS <= 16
typedef uint16_t update_t;
#elif UPDATE_BITS <= 32
typedef uint32_t update_t;
#else
typedef uint64_t update_t;
#endif

// In memory, an update will be packed into an array of bytes.
struct __update_packed_struct {
  uint8_t d[((UPDATE_BITS-1) / (8*UPDATE_ALIGN) + 1) * UPDATE_ALIGN];
};

// Internal type for converting between normal and packed representations
// of updates.
typedef union {
  update_t        update;
  update_packed_t packed;
} __update_conv_t;

// Pack an update.
static inline
update_packed_t update_pack(update_t update)
{ __update_conv_t conv = { .update = update };
  return conv.packed; }

// Unpack an update.
static inline
update_t update_unpack(update_packed_t packed)
{ __update_conv_t conv = { .packed = packed };
  return conv.update; }


// Types for positions and hints as exported by the functions.
typedef unsigned pos_t;
typedef unsigned hint_t;

// Construct an update from a prime hint only, leaving space for adjoining the
// position later on.
static inline
update_t update_set_hint(hint_t hint)
{ ASSERT(hint < (hint_t)1<<UPDATE_HINT_BITS);
  return (update_t)hint << UPDATE_POS_BITS; }

// Adjoin a position to a hint-only update.
// /!\ The result is undefined if the update already contains a position.
static inline
update_t update_adj_pos(update_t update, pos_t pos)
{ ASSERT(pos < (pos_t)1<<UPDATE_POS_BITS);
  return update | pos; }

// Construct an update from a position and a prime hint.
static inline
update_t update_set(pos_t pos, hint_t hint)
{ return update_adj_pos(update_set_hint(hint), pos); }

// Retrieve the position from an update.
static inline
pos_t update_get_pos(update_t update)
{ return update & (((pos_t)1<<UPDATE_POS_BITS)-1); }

// Retrieve the prime hint from an update.
static inline
hint_t update_get_hint(update_t update)
{ return (update >> UPDATE_POS_BITS) & (((hint_t)1<<UPDATE_HINT_BITS)-1); }



/* Array of buckets: basic management.
 *****************************************************************************/

// Initialize structure and allocate buckets.
void buckets_init(buckets_ptr buckets, unsigned I, unsigned J,
                  unsigned max_size, unsigned min_degp, unsigned max_degp)
{
  unsigned n        = 1 + ((ijvec_get_max_pos(I, J)-1) >> UPDATE_POS_BITS);
  buckets->n        = n;
  buckets->max_size = max_size;
  buckets->min_degp = min_degp;
  buckets->max_degp = max_degp;
  buckets->start    =
    (update_packed_t **)malloc(n * sizeof(update_packed_t *));
  ASSERT_ALWAYS(buckets->start != NULL);
  for (unsigned k = 0; k < n; ++k) {
    buckets->start[k] =
      (update_packed_t *)malloc(max_size * sizeof(update_packed_t));
    ASSERT_ALWAYS(buckets->start[k] != NULL);
  }
  unsigned ndegp = max_degp - min_degp;
  buckets->degp_end =
    (update_packed_t **)malloc(ndegp * n * sizeof(update_packed_t *));
  ASSERT_ALWAYS(buckets->degp_end);
}


// Clean up memory.
void buckets_clear(buckets_ptr buckets)
{
  free(buckets->start[0]);
  free(buckets->start);
  free(buckets->degp_end);
}


// Return the size of a bucket region.
unsigned bucket_region_size()
{
  return 1u << UPDATE_POS_BITS;
}


// Print information about the buckets.
void print_bucket_info(buckets_srcptr buckets)
{
  printf("# buckets info:\n");
  printf("#   nb of buckets   = %u\n", buckets->n);
  printf("#   size of buckets = %u\n", buckets->max_size);
  printf("#   size of bucket-region (ie, 2^UPDATE_POS_BITS) = %d\n",
         bucket_region_size());
  printf("#   number of bits for the hint = %d\n", UPDATE_HINT_BITS);
  printf("#   bit-size of a bucket-update = %d (rounded to %lu)\n",
      UPDATE_BITS, 8*sizeof(update_packed_t));
}



/* Array of buckets: filling buckets.
 *****************************************************************************/

// In the case of sublattices, compute the starting point for the sieve
// by gothp for the current sublattice.
// If there is no starting point return 0.
static int compute_starting_point(ijvec_ptr V0, ijbasis_ptr euclid,
                                  sublat_srcptr sublat)
{
    if (!use_sublat(sublat)) {
        ijvec_set_zero(V0);
        return 1;
    }
    int hatI = euclid->I;
    int hatJ = euclid->J;
    // TODO: FIXME: WARNING: this whole block is to be rewritten
    // completely!

    // There must be some Thm that says that a combination of
    // the first two or of the second two basis vectors is
    // enough to find a valid starting point (assuming that
    // the basis is sorted in increasing order for deg j).
    // Cf a .tex that is to be written.
    // If p is too large, then the code below is broken (and
    // anyway, this is a weird idea to sieve with such
    // parameters).

    // Try with the first 2 fundamental vectors and if this does not
    // work, try with the second 2 vectors.
    // TODO: question: does it ensures that vectors will be
    // visited in increasing order of j ?
    // FIXME: What if there are only 2 fundamental vectors?
    //
    //

    // Naive approach: check all combinations.
    ij_t ijmod, xi, yi;
    ij_set_16(ijmod, sublat->modulus);
    ij_set_16(xi, sublat->lat[sublat->n][0]);
    ij_set_16(yi, sublat->lat[sublat->n][1]);
    int found = 0;

    // case of just one vector in euclid.vec
    if (euclid->dim == 1) {
      ij_t rem;
      ij_rem(rem, euclid->v[0]->i, ijmod);
      if (ij_eq(rem, xi)) {
        ij_rem(rem, euclid->v[0]->j, ijmod);
        if (ij_eq(rem, yi)) {
          // Got a valid point!
          ij_sub(V0->i, euclid->v[0]->i, xi);
          ij_div(V0->i, V0->i, ijmod);
          ij_sub(V0->j, euclid->v[0]->j, yi);
          ij_div(V0->j, V0->j, ijmod);
          found = 1;
        }
      }
    }

    for (unsigned int ind = 0; (!found) && ind < euclid->dim-1; ++ind) {
      for (int i0 = 0; (!found) && i0 < 2; ++i0)
        for (int i1 = 0; (!found) && i1 < 2; ++i1)
          for (int i2 = 0; (!found) && i2 < 2; ++i2)
            for (int i3 = 0; (!found) && i3 < 2; ++i3){
              ijvec_t W, tmp;
              ijvec_set_zero(W);
              int i01 = i0 ^ i1;
              int i23 = i2 ^ i3;
              if (i0) ijvec_add(W,W,euclid->v[ind]);
              if (i2) ijvec_add(W,W,euclid->v[ind+1]);
              if (i01) {
                ijvec_mul_ti(tmp,euclid->v[ind],1);
                ijvec_add(W, W, tmp);
              }
              if (i23) {
                ijvec_mul_ti(tmp,euclid->v[ind+1],1);
                ijvec_add(W, W, tmp);
              }
              if ((ij_deg(W->i) >= hatI)
                  || (ij_deg(W->j) >= hatJ))
                continue;
              ij_t rem;
              ij_rem(rem, W->i, ijmod);
              if (!ij_eq(rem, xi))
                continue;
              ij_rem(rem, W->j, ijmod);
              if (!ij_eq(rem, yi))
                continue;
              // Got a valid point!
              ij_sub(W->i, W->i, xi);
              ij_div(V0->i, W->i, ijmod);
              ij_sub(W->j, W->j, yi);
              ij_div(V0->j, W->j, ijmod);
              found = 1;
            }
    }
    return found;
}


// Fill the buckets with updates corresponding to divisibility by elements of
// the factor base.
void buckets_fill(buckets_ptr buckets, factor_base_srcptr FB,
                  sublat_srcptr sublat, unsigned I, unsigned J)
{
  ijbasis_t basis;
  ijbasis_t euclid;
  unsigned  hatI, hatJ;
  hatI = I + sublat->deg;
  hatJ = J + sublat->deg;

  ijbasis_init(basis,     I,    J);
  ijbasis_init(euclid, hatI, hatJ);

  // Pointers to the current writing position in each bucket.
  update_packed_t **ptr =
    (update_packed_t **)malloc(buckets->n * sizeof(update_packed_t *));
  ASSERT_ALWAYS(ptr != NULL);
  for (unsigned k = 0; k < buckets->n; ++k)
    ptr[k] = buckets->start[k];

  // Skip prime ideals of degree less than min_degp.
  unsigned i;
  for (i = 0; i < FB->n && FB->elts[i]->degp < buckets->min_degp; ++i);

  fbideal_srcptr gothp = FB->elts[i];
  // Go through the factor base by successive deg(gothp).
  for (unsigned degp = buckets->min_degp; degp < buckets->max_degp; ++degp) {
    // Not supported yet.
    if (use_sublat(sublat) && degp == 1) continue;

    // Go through each prime ideal of degree degp.
    for (; i < FB->n && gothp->degp == degp; ++i, ++gothp) {
      // Not supported yet.
      if (gothp->proj) continue;

      ijbasis_compute(euclid, basis, gothp);
      ijvec_t v;
      if (use_sublat(sublat)) {
        int st = compute_starting_point(v, euclid, sublat);
        if (!st)
          continue; // next factor base prime.
      }
      else
        ijvec_set_zero(v);


      // Enumeration of the p-lattice is done via:
      // - nested for loops for the first ENUM_LATTICE_NESTED coordinates, and
      // - p-ary Gray codes for the remaining coordinates.
#     define ENUM_LATTICE_NESTED 8
#     define ENUM_LATTICE(ctx, n)                              \
        for (int i = 0; (!i || basis->dim > n) && i < FP_SIZE; \
             ++i, ijvec_add(v, v, basis->v[n]))

      ij_t s, t;
      ij_set_zero(t);
      int rc = basis->dim > ENUM_LATTICE_NESTED;
      do {

        // Nested enumeration on ENUM_LATTICE_NESTED levels.
        FOR(ENUM_LATTICE, , ENUM_LATTICE_NESTED) {
          if (ij_is_monic(v->j) || ij_is_zero(v->j)) {
            ijpos_t  pos = ijvec_get_pos(v, I, J);
            unsigned k   = pos >> UPDATE_POS_BITS;
            pos_t    p   = pos & (((pos_t)1<<UPDATE_POS_BITS)-1);
            ASSERT(ptr[k] - buckets->start[k] < buckets->max_size);
            *ptr[k]++ = update_pack(update_set(p, 0));
          }
        }

        // Gray code enumeration: using ij_set_next, the degree of the difference
        // with the previous one indicates which basis vector should be added to
        // the current lattice point.
        // rc is set to 0 when all vectors have been enumerated.
        ij_set(s, t);
        rc = rc && ij_set_next(t, t, basis->dim-ENUM_LATTICE_NESTED);
        if (rc) {
          ij_diff(s, s, t);
          ijvec_add(v, v, basis->v[ij_deg(s)+ENUM_LATTICE_NESTED]);
        }
      } while (rc);

#     undef ENUM_LATTICE
    }

    // Mark the last position for this degree in the degp_end array.
    for (unsigned i = degp - buckets->min_degp, k = 0; k < buckets->n; ++k)
      buckets->degp_end[i*buckets->n + k] = ptr[k];
  }

  //for (unsigned k = 0; k < buckets->n; ++k)
  //  printf("# #updates[%u] = %u\n", k, bucket_size(buckets, k));

  ijbasis_clear(euclid);
  ijbasis_clear(basis);
}


// Apply all the updates from a given bucket to the sieve region S.
void bucket_apply(uint8_t *S, buckets_srcptr buckets, unsigned k)
{
  // Pointer to the current reading position in the bucket.
  update_packed_t *ptr = buckets->start[k];

  // Iterate through each group of updates having same deg(gothp).
  update_packed_t **end = buckets->degp_end+k;
  for (unsigned degp = buckets->min_degp; degp < buckets->max_degp;
       ++degp, end += buckets->n) {
    while (ptr != *end) {
      update_t update = update_unpack(*ptr++);
      pos_t    p      = update_get_pos(update);
      ASSERT((k == 0 && p == 0) || S[p] >= degp);
      S[p] -= degp;
    }
  }
}
