#ifndef BINARY_SSE2_TRAITS_HPP_
#define BINARY_SSE2_TRAITS_HPP_

#include <gmp.h>
#include <gmpxx.h>
#include <vector>
#include "traits_globals.hpp"
#include <ios>
#include <iomanip>
#include "matrix_repr_binary.hpp"

struct binary_sse2_traits {
	typedef matrix_repr_binary representation;
	static const int max_accumulate = UINT_MAX;
	static const int max_accumulate_wide = UINT_MAX;
#if defined(__OpenBSD__) && defined(__x86_64)
	/* I know it's outright stupid */
	typedef long inner_type;
#else
	typedef int64_t inner_type;
#endif
	typedef inner_type sse2_scalars __attribute__((vector_size(16)));

	typedef binary_field coeff_field;

	struct scalar_t { sse2_scalars p; };
	typedef scalar_t wide_scalar_t;

	struct name {
		const char * operator()() const {
			return "binary SSE-2 code [ 128 bits ]";
		}
	};

	static int can() {
		return globals::nbys == 128 && globals::modulus_ulong == 2;
	}

	/* FIXME -- this used to return an mpz_class */
	static inline int get_y(scalar_t const & x, int i) {
		inner_type foo[2] __attribute__((aligned(16)));
		memcpy(foo, &x.p, sizeof(sse2_scalars));
		BUG_ON(i >= 128 || i < 0);
		int bit = (foo[i >> 6] >> (i & 63)) & 1;
		return bit;
	}
	static inline void reduce(scalar_t & d, wide_scalar_t & s) {
		d.p = s.p;
	}
	static inline void reduce(scalar_t * dst, wide_scalar_t * src,
		unsigned int i0, unsigned int i1)
	{
		for(unsigned int i = i0 ; i < i1 ; i++) {
			reduce(dst[i], src[i - i0]);
		}
	}
	static inline void addmul(wide_scalar_t & dst, scalar_t const & src) {
		dst.p ^= src.p;
	}
	/* TODO: It's quite unfortunate to call this function. It does
	 * *NOT* get called in the most critical section, that is, when
	 * doing multiplication. However, the check loop uses it. Since
	 * the check is essentially useless in characteristic two, that's
	 * bad.
	 */
	static inline void addmul(wide_scalar_t & dst, scalar_t const & src, inner_type x)
	{
		sse2_scalars mask = (sse2_scalars) { -x, -x, };
		dst.p ^= mask & src.p;
	}

	static inline void zero(scalar_t * p, unsigned int n) {
		memset(p, 0, n * sizeof(scalar_t));
	}
	static inline void zero(scalar_t & x) { zero(&x, 1); }
	static inline void copy(scalar_t * q, const scalar_t * p, unsigned int n) {
		memcpy(q, p, n * sizeof(scalar_t));
	}
        /*      [duplicate]
	static inline void copy(wide_scalar_t * q, const wide_scalar_t * p, unsigned int n) {
		memcpy(q, p, n * sizeof(wide_scalar_t));
	}
        */


	static inline bool is_zero(scalar_t const& x) {
		inner_type foo[2] __attribute__((aligned(16)));
		memcpy(foo, &x.p, sizeof(sse2_scalars));
		for(unsigned int i = 0 ; i < 2 ; i++) {
			if (foo[i])
				return false;
		}
		return true;
	}
	/*
	static inline void assign(mpz_class& z, scalar_t const & x) {
		MPZ_SET_MPN(z.get_mpz_t(), x.p, width);
	}
	*/
	static inline void addmul_wide(scalar_t & dst,
			scalar_t const & a,
			scalar_t const & b)
	{
		dst.p ^= a.p & b.p;
	}
	static inline void
	assign(scalar_t & x, std::vector<mpz_class> const& z,  unsigned int i)
	{
		/* This one affects from a vector. We provide the
		 * position, in case we're doing SIMD.
		 */
		// WARNING("slow");
		/* FIXME -- should we go up to 128 here, or restrict to
		 * nbys ??? */
		inner_type foo[2] __attribute__((aligned(16))) = { 0, };
		for(unsigned int j = 0 ; j < 128 ; j++)
			foo[j>>6] ^= (inner_type) (z[i+j] != 0) << (j & 63);
		x.p = (sse2_scalars) { foo[0], foo[1], };
	}
	static inline void assign(std::vector<mpz_class>& z, scalar_t const & x) {
		inner_type foo[2] __attribute__((aligned(16)));
		memcpy(foo, &x.p, sizeof(sse2_scalars));
		BUG_ON(z.size() != 128);
		for(unsigned int i = 0 ; i < 128 ; i++) {
			z[i] = (foo[i>>6] >> (i & 63)) & 1UL;
		}
	}

	static std::ostream& print(std::ostream& o, scalar_t const& x) {
		inner_type foo[2] __attribute__((aligned(16)));
		memcpy(foo, &x.p, sizeof(sse2_scalars));
		/*
		 * TODO: allow some sort of compressed I/O. The problem
		 * is that it interfers a lot with other stuff. a 4x, or
		 * even 16x reduction of the output size would be nice...
		 */
#if 0
		std::ios_base::fmtflags f(o.flags());
		o << std::setw(16) << std::hex;
		for(int i = 0 ; i < 2 ; i++) {
			if (i) { o << " "; }
			o << foo[i];
		}
		o.flags(f);
#endif
		for(int i = 0 ; i < globals::nbys ; i++) {
			if (i) { o << " "; }
			o << ((foo[i>>6] >> (i & 63)) & 1UL);
		}
		return o;
	}
};

#endif	/* BINARY_SSE2_TRAITS_HPP_ */
