/* MPFQ generated file -- do not edit */

#define _POSIX_C_SOURCE 200112L
#include "abase_p_4_t.h"

/* Active handler: simd_gfp */
/* Automatically generated code  */
/* Active handler: Mpfq::defaults */
/* Active handler: Mpfq::defaults::vec */
/* Active handler: Mpfq::defaults::poly */
/* Active handler: Mpfq::gfp::field */
/* Active handler: Mpfq::gfp::elt */
/* Active handler: Mpfq::defaults::mpi_flat */
/* Options used:{
   family=[ { cpp_ifdef=COMPILE_MPFQ_PRIME_FIELDS, tag=p_4, }, ],
   fieldtype=prime,
   n=4,
   nn=9,
   opthw=,
   tag=p_4,
   type=plain,
   vbase_stuff={
    choose_byfeatures=<code>,
    families=[
     [ u64k1, u64k2, ],
     [ { cpp_ifdef=COMPILE_MPFQ_PRIME_FIELDS, tag=p_1, }, ],
     [ { cpp_ifdef=COMPILE_MPFQ_PRIME_FIELDS, tag=p_2, }, ],
     [ { cpp_ifdef=COMPILE_MPFQ_PRIME_FIELDS, tag=p_3, }, ],
     [ { cpp_ifdef=COMPILE_MPFQ_PRIME_FIELDS, tag=p_4, }, ],
     [ { cpp_ifdef=COMPILE_MPFQ_PRIME_FIELDS, tag=p_8, }, ],
     [ { cpp_ifdef=COMPILE_MPFQ_PRIME_FIELDS, tag=pz, }, ],
     ],
    member_templates_restrict={
     p_1=[ { cpp_ifdef=COMPILE_MPFQ_PRIME_FIELDS, tag=p_1, }, ],
     p_2=[ { cpp_ifdef=COMPILE_MPFQ_PRIME_FIELDS, tag=p_2, }, ],
     p_3=[ { cpp_ifdef=COMPILE_MPFQ_PRIME_FIELDS, tag=p_3, }, ],
     p_4=[ { cpp_ifdef=COMPILE_MPFQ_PRIME_FIELDS, tag=p_4, }, ],
     p_8=[ { cpp_ifdef=COMPILE_MPFQ_PRIME_FIELDS, tag=p_8, }, ],
     pz=[ { cpp_ifdef=COMPILE_MPFQ_PRIME_FIELDS, tag=pz, }, ],
     u64k1=[ u64k1, u64k2, ],
     u64k2=[ u64k1, u64k2, ],
     },
    vc:includes=[ <stdarg.h>, ],
    },
   virtual_base={
    filebase=abase_vbase,
    global_prefix=abase_,
    name=abase_vbase,
    substitutions=[
     [ (?^:abase_p_4_elt \*), void *, ],
     [ (?^:abase_p_4_src_elt\b), const void *, ],
     [ (?^:abase_p_4_elt\b), void *, ],
     [ (?^:abase_p_4_dst_elt\b), void *, ],
     [ (?^:abase_p_4_elt_ur \*), void *, ],
     [ (?^:abase_p_4_src_elt_ur\b), const void *, ],
     [ (?^:abase_p_4_elt_ur\b), void *, ],
     [ (?^:abase_p_4_dst_elt_ur\b), void *, ],
     [ (?^:abase_p_4_vec \*), void *, ],
     [ (?^:abase_p_4_src_vec\b), const void *, ],
     [ (?^:abase_p_4_vec\b), void *, ],
     [ (?^:abase_p_4_dst_vec\b), void *, ],
     [ (?^:abase_p_4_vec_ur \*), void *, ],
     [ (?^:abase_p_4_src_vec_ur\b), const void *, ],
     [ (?^:abase_p_4_vec_ur\b), void *, ],
     [ (?^:abase_p_4_dst_vec_ur\b), void *, ],
     [ (?^:abase_p_4_poly \*), void *, ],
     [ (?^:abase_p_4_src_poly\b), const void *, ],
     [ (?^:abase_p_4_poly\b), void *, ],
     [ (?^:abase_p_4_dst_poly\b), void *, ],
     ],
    },
   vtag=p_4,
   w=64,
   } */


/* Functions operating on the field structure */

/* Element allocation functions */

/* Elementary assignment functions */

/* Assignment of random values */

/* Arithmetic operations on elements */

/* Operations involving unreduced elements */

/* Comparison functions */

/* Input/output functions */

/* Vector functions */

/* Polynomial functions */

/* Functions related to SIMD operation */

/* Member templates related to SIMD operation */

/* MPI interface */

/* Object-oriented interface */
/* *simd_gfp::code_for_member_template_dotprod */
void abase_p_4_p_4_dotprod(abase_p_4_dst_field K0 MAYBE_UNUSED, abase_p_4_dst_field K1 MAYBE_UNUSED, abase_p_4_dst_vec xw, abase_p_4_src_vec xu1, abase_p_4_src_vec xu0, unsigned int n)
{
        abase_p_4_elt_ur s,t;
        abase_p_4_elt_ur_init(K0, &s);
        abase_p_4_elt_ur_init(K0, &t);
        abase_p_4_elt_ur_set_zero(K0, s);
        for(unsigned int i = 0 ; i < n ; i++) {
            abase_p_4_mul_ur(K0, t, xu0[i], xu1[i]);
            abase_p_4_elt_ur_add(K0, s, s, t);
        }
        abase_p_4_reduce(K0, xw[0], s);
        abase_p_4_elt_ur_clear(K0, &s);
        abase_p_4_elt_ur_clear(K0, &t);
}

/* *simd_gfp::code_for_member_template_addmul_tiny */
void abase_p_4_p_4_addmul_tiny(abase_p_4_dst_field K MAYBE_UNUSED, abase_p_4_dst_field L MAYBE_UNUSED, abase_p_4_dst_vec w, abase_p_4_src_vec u, abase_p_4_dst_vec v, unsigned int n)
{
        abase_p_4_elt s;
        abase_p_4_init(K, &s);
        for(unsigned int i = 0 ; i < n ; i++) {
            abase_p_4_mul(K, s, u[i], v[0]);
            abase_p_4_add(K, w[i], w[i], s);
        }
        abase_p_4_clear(K, &s);
}

/* *simd_gfp::code_for_member_template_transpose */
void abase_p_4_p_4_transpose(abase_p_4_dst_field K MAYBE_UNUSED, abase_p_4_dst_field L MAYBE_UNUSED, abase_p_4_dst_vec w, abase_p_4_src_vec u)
{
    abase_p_4_set(K, w[0], u[0]);
}


/* vim:set ft=cpp: */