#include "cado.h"

#ifdef HAVE_SSE2

#include <stdlib.h>
#include <string.h>

#include "las-unsieve.h"
#include "ularith.h"
#include "las-norms.h"
#include "las-debug.h"
#include "gcd.h"
#include "memory.h"

static const int verify_gcd = 0; /* Enable slow but thorough test */

static inline int 
sieve_info_test_lognorm_sse2(__m128i * S0, const __m128i pattern0,
                             const __m128i *S1, const __m128i pattern1)
{
    const __m128i zero = _mm_set1_epi8(0);
    const __m128i ff = _mm_set1_epi8(0xff);
    const __m128i sign_conversion = _mm_set1_epi8(-128);
    __m128i a = *S0;
    __m128i r = *S1;
    __m128i m1, m2;
    /* _mm_cmpgt_epi8() performs signed comparison, but we have unsigned
       bytes. We can switch to signed in a way that preserves ordering by
       flipping the MSB, e.g., 0xFF (255 unsigned) becomes 0x7F (+127 signed), 
       and 0x00 (0 unsigned) becomes 0x80 (-128 signed).

       If a byte in the first operand is greater than the corresponding byte in
       the second operand, the corresponding byte in the result is set to all 1s
       (i.e., to 0xFF); otherwise, it is set to all 0s.
       
       Normally, S[x] <= bound means a sieve survivor. However, for skipping over
       locations where 3 | gcd(i,j), we set the bound to 0 in the pattern at
       those locations. We then need a comparison that never produces a survivor
       in those locations, even when S[x] is in fact 0. Thus we initialise the
       pattern to bound + 1, then set the bound to 0 where 3 | gcd(i,j), and
       change the comparison to S[x] < bound, which is guaranteed not to let any
       survivors through where the pattern byte is 0. */
    m1 = _mm_cmpgt_epi8 (pattern0, _mm_xor_si128(a, sign_conversion));
    m2 = _mm_cmpgt_epi8 (pattern1, _mm_xor_si128(r, sign_conversion));
    /* m1 is 0xFF where pattern[x] > S0[x], i.e., where it survived.
       Same for S1. */
    /* Logically AND the two masks: survivor only where both sided survived */
    m1 = _mm_and_si128(m1, m2);

    /* m1 is 0xFF in those locations where the sieve entry survived on
       both sides */
    /* For the OR mask we need the bit complement, via m1 XOR 0xFF */
    m2 = _mm_xor_si128(m1, ff);
    *S0 = _mm_or_si128(a, m2);
    /* Do we want to update this one? */
    // *S1 = _mm_or_si128(r, m2);

    /* Compute number of non-zero bytes. We want 1 is those bytes that
    survived, and 0 in the others. m1 has 0xFF in those bytes that
    survived, 0 in the others. First sign flip: 0xFF -> 0x1 */
    m1 = _mm_sub_epi8(zero, m1);
    /* Using Sum of Absolute Differences with 0, which for us gives the
    number of non-zero bytes. This Sum of Absolute Differences uses
    unsigned arithmetic, thus we needed the sign flip first */
    m1 = _mm_sad_epu8(m1, zero);
    /* Sum is stored in two parts */
    int nr_set = _mm_extract_epi16(m1, 0) + _mm_extract_epi16(m1, 4);
    /* Return number of bytes that were not set to 255 */
    return nr_set;
}


/* In SS[2][x_start] ... SS[2][x_start * x_step - 1], look for survivors.
   If SSE is available, the bounds check was already done. If no SSE2 is
   available, we still have to do it. In both cases, we still have to test
   divisibility of the resulting i value by the trial-divided primes.
   Return the number of survivors found. */
static inline int
search_single_survivors(unsigned char * const SS[2],
        const unsigned char bound[2] MAYBE_UNUSED, const unsigned int log_I,
        const unsigned int j, const int N MAYBE_UNUSED,
        const int x_start, const int x_step, const unsigned int nr_div,
        unsigned int (*div)[2])
{
  int survivors = 0;
  for (int x = x_start; x < x_start + x_step; x++) {
      if (SS[0][x] == 255)
          continue;
      survivors++;

      /* The very small prime used in the bound pattern, and unsieving larger
         primes have not identified this as gcd(i,j) > 1. It remains to check
         the trial-divided primes. */
      const unsigned int i = abs (x - (1 << (log_I - 1)));
      int divides = 0;
      switch (nr_div) {
          case 6: divides |= (i * div[5][0] <= div[5][1]);
          case 5: divides |= (i * div[4][0] <= div[4][1]);
          case 4: divides |= (i * div[3][0] <= div[3][1]);
          case 3: divides |= (i * div[2][0] <= div[2][1]);
          case 2: divides |= (i * div[1][0] <= div[1][1]);
          case 1: divides |= (i * div[0][0] <= div[0][1]);
          case 0: while(0){};
      }

      if (divides)
      {
          if (verify_gcd)
              ASSERT_ALWAYS(bin_gcd_int64_safe (i, j) != 1);
#ifdef TRACE_K
          if (trace_on_spot_Nx(N, x)) {
              verbose_output_print(TRACE_CHANNEL, 0, "# Slot [%u] in bucket %u has non coprime (i,j)=(%d,%u)\n",
                      x, N, i, j);
          }
#endif
          SS[0][x] = 255;
      } else {
          if (verify_gcd)
              ASSERT_ALWAYS(bin_gcd_int64_safe (i, j) == 1);
#ifdef TRACE_K
          if (trace_on_spot_Nx(N, x)) {
              verbose_output_print(TRACE_CHANNEL, 0, "# Slot [%u] in bucket %u is survivor with coprime (i,j)\n",
                      x, N);
          }
#endif
      }
  }
  return survivors;
}


/* This function works for all j */
static int
search_survivors_in_line1(unsigned char * const SS[2],
        const unsigned char bound[2], const unsigned int log_I,
        const unsigned int j, const int N MAYBE_UNUSED, j_div_srcptr j_div,
        const unsigned int td_max)
{
    unsigned int div[6][2], nr_div;

    nr_div = extract_j_div(div, j, j_div, 3, td_max);
    ASSERT_ALWAYS(nr_div <= 6);

    const __m128i sse2_sign_conversion = _mm_set1_epi8(-128);
    /* The reason for the bound+1 here is documented in
       sieve_info_test_lognorm_sse2() */
    __m128i patterns[2] = {
        _mm_xor_si128(_mm_set1_epi8(bound[0] + 1), sse2_sign_conversion),
        _mm_xor_si128(_mm_set1_epi8(bound[1] + 1), sse2_sign_conversion)
    };
    const int x_step = sizeof(__m128i);

    /* If j is even, set all the even entries on the bound pattern to
       unsigned 0 -> signed -128 */
    if (j % 2 == 0)
        for (size_t i = 0; i < sizeof(__m128i); i += 2)
            ((unsigned char *)&patterns[0])[i] = 0x80;
    int survivors = 0;

    for (int x_start = 0; x_start < (1 << log_I); x_start += x_step)
    {
        /* Do bounds check using SSE pattern, set non-survivors in SS[0] array
           to 255 */
        int sse_surv = 
            sieve_info_test_lognorm_sse2((__m128i*) (SS[0] + x_start), patterns[0],
                                         (__m128i*) (SS[1] + x_start), patterns[1]);
        if (sse_surv == 0)
            continue;
        int surv = search_single_survivors(SS, bound, log_I, j, N, x_start,
            x_step, nr_div, div);
        ASSERT(sse_surv == surv);
        survivors += surv;
    }
    return survivors;
}


/* This function assumes j % 3 == 0. It uses an SSE bound pattern where 
   i-coordinates with i % 3 == 0 are set to a bound of 0. */
int
search_survivors_in_line3(unsigned char * const SS[2], 
        const unsigned char bound[2], const unsigned int log_I,
        const unsigned int j, const int N MAYBE_UNUSED, j_div_srcptr j_div,
        const unsigned int td_max)
{
    const __m128i sse2_sign_conversion = _mm_set1_epi8(-128);
    __m128i patterns[2][3];
    const int x_step = sizeof(__m128i);
    const int pmin = 5;
    int next_pattern = 0;
    int survivors = 0;

    /* We know that 3 does not divide j in the code of this function, and we
       don't store 2. Allowing 5 distinct odd prime factors >3 thus handles
       all j < 1616615, which is the smallest integer with 6 such factors */
    unsigned int div[5][2];
    unsigned int nr_div;

    nr_div = extract_j_div(div, j, j_div, pmin, td_max);
    ASSERT_ALWAYS(nr_div <= 5);

    patterns[0][0] = patterns[0][1] = patterns[0][2] = 
        _mm_xor_si128(_mm_set1_epi8(bound[0] + 1), sse2_sign_conversion);
    patterns[1][0] = patterns[1][1] = patterns[1][2] = 
        _mm_xor_si128(_mm_set1_epi8(bound[1] + 1), sse2_sign_conversion);

    if (j % 2 == 0)
        for (size_t i = 0; i < 3 * sizeof(__m128i); i += 2)
            ((unsigned char *)&patterns[0][0])[i] = 0x80;

    /* Those locations in patterns[0] that correspond to i being a multiple
       of 3 are set to 0. Byte 0 of patterns[0][0] corresponds to i = -I/2.
       We want d s.t. -I/2 + d == 0 (mod 3), or d == I/2 (mod 3). With
       I = 2^log_I and 2 == -1 (mod 3), we have d == -1^(log_I-1) (mod 3),
       or d = 2 if log_I is even and d = 1 if log_I is odd.
       We use the sign conversion trick (i.e., XOR 0x80), so to get an
       effective bound of unsigned 0, we need to set the byte to 0x80. */
    size_t d = 2 - log_I % 2;
    for (size_t i = 0; i < sizeof(__m128i); i++)
        ((unsigned char *)&patterns[0][0])[3*i + d] = 0x80;

    for (int x_start = 0; x_start < (1 << log_I); x_start += x_step)
    {
        int sse_surv = 
            sieve_info_test_lognorm_sse2((__m128i*) (SS[0] + x_start), patterns[0][next_pattern],
                                         (__m128i*) (SS[1] + x_start), patterns[1][next_pattern]);
        if (++next_pattern == 3)
            next_pattern = 0;
        if (sse_surv == 0)
            continue;
        int surv = search_single_survivors(SS, bound, log_I, j, N, x_start,
            x_step, nr_div, div);
        ASSERT(sse_surv == surv);
        survivors += surv;
    }
    return survivors;
}


/* This function assumes j % 3 != 0 and j % 5 == 0. It uses an SSE bound 
   pattern where i-coordinates with i % 5 == 0 are set to a bound of 0,
   and trial divides only by primes > 5. */
int
search_survivors_in_line5(unsigned char * const SS[2], 
        const unsigned char bound[2], const unsigned int log_I,
        const unsigned int j, const int N MAYBE_UNUSED, 
        j_div_srcptr j_div, const unsigned int td_max)
{
    const __m128i sse2_sign_conversion = _mm_set1_epi8(-128);
    const int nr_patterns = 5;
    __m128i patterns[2][nr_patterns];
    const int x_step = sizeof(__m128i);
    const int pmin = 7;
    int next_pattern = 0;
    int survivors = 0;

    /* We know that 3 and 5 do not divide j in the code of this function, and
       we don't store 2. Allowing 5 distinct odd prime factors >5 thus handles
       all j < 7436429 */
    unsigned int div[5][2];
    unsigned int nr_div;

    nr_div = extract_j_div(div, j, j_div, pmin, td_max);
    ASSERT_ALWAYS(nr_div <= 5);

    for (int i = 0; i < nr_patterns; i++) {
        patterns[0][i] = _mm_xor_si128(_mm_set1_epi8(bound[0] + 1), sse2_sign_conversion);
        patterns[1][i] = _mm_xor_si128(_mm_set1_epi8(bound[1] + 1), sse2_sign_conversion);
    }

    if (j % 2 == 0)
        for (size_t i = 0; i < nr_patterns * sizeof(__m128i); i += 2)
            ((unsigned char *)&patterns[0][0])[i] = 0x80;

    /* Those locations in patterns[0] that correspond to i being a multiple
       of 5 are set to 0. Byte 0 of patterns[0][0] corresponds to i = -I/2.
       We want d s.t. -I/2 + d == 0 (mod 5), or d == I/2 (mod 5). With
       I = 2^log_I and ord_5(2) == 4 (mod 5), we have d == 2^((log_I-1)%4)
       (mod 5), so we want a function: 0->3, 1->1, 2->2, 3->4.
       We use the sign conversion trick (i.e., XOR 0x80), so to get an
       effective bound of unsigned 0, we need to set the byte to 0x80. */
    static const unsigned char d_lut[] = {3,1,2,4};
    size_t d = d_lut[log_I % 4];
    for (size_t i = 0; i < sizeof(__m128i); i++)
        ((unsigned char *)&patterns[0][0])[nr_patterns*i + d] = 0x80;

    for (int x_start = 0; x_start < (1 << log_I); x_start += x_step)
    {
        int sse_surv = 
            sieve_info_test_lognorm_sse2((__m128i*) (SS[0] + x_start), patterns[0][next_pattern],
                                         (__m128i*) (SS[1] + x_start), patterns[1][next_pattern]);
        if (++next_pattern == nr_patterns)
            next_pattern = 0;
        if (sse_surv == 0)
            continue;
        int surv = search_single_survivors(SS, bound, log_I, j, N, x_start,
            x_step, nr_div, div);
        ASSERT(sse_surv == surv);
        survivors += surv;
    }
    return survivors;
}


#define USE_PATTERN_3 1
#define USE_PATTERN_5 1

int
search_survivors_in_line_sse2(unsigned char * const SS[2], 
        const unsigned char bound[2], const unsigned int log_I,
        const unsigned int j, const int N, j_div_srcptr j_div,
        const unsigned int td_max)
{
#if USE_PATTERN_3
    if (j % 3 == 0)
      return search_survivors_in_line3(SS, bound, log_I, j, N, j_div, td_max);
#if USE_PATTERN_5
    else if (j % 5 == 0)
      return search_survivors_in_line5(SS, bound, log_I, j, N, j_div, td_max);
#endif
    else
#endif
      return search_survivors_in_line1(SS, bound, log_I, j, N, j_div, td_max);
}

#endif /* HAVE_SSE2 */
