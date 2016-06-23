#include "cado.h"

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include <pthread.h>
#include <vector>
#include <utility>
#include <sstream>
#include <algorithm>
#include <map>

#include "bwc_config.h"
#include "matmul.h"
#include "matmul-common.h"
#include "mpfq_layer.h"
#include "portability.h"

#include "matmul_facade.h"

#include "arith-modp.hpp"

typedef arith_modp::gfp<sizeof(abelt)/sizeof(unsigned long)> gfp;
typedef arith_modp::fast_type<gfp> fast_gfp;
typedef fast_gfp::elt fast_elt;
typedef fast_gfp::elt_ur fast_elt_ur;

/* This extension is used to distinguish between several possible
 * implementations of the product */
#define MM_EXTENSION   "-zone"
#define MM_MAGIC_FAMILY        0xb002UL
#define MM_MAGIC_VERSION       0x1001UL
#define MM_MAGIC (MM_MAGIC_FAMILY << 16 | MM_MAGIC_VERSION)

/* This selects the default behaviour as to which is our best code
 * for multiplying. If this flag is 1, then a multiplication matrix times
 * vector (direction==1) performs best if the in-memory structure
 * reflects the non-transposed matrix. Similarly, a vector times matrix
 * multiplication (direction==0) performs best if the in-memory structure
 * reflects the transposed matrix. When the flag is 1, the converse
 * happens.
 * This flag depends on the implementation, and possibly even on the cpu
 * type under certain circumstances.
 */
#define MM_DIR0_PREFERS_TRANSP_MULT   1

using namespace std;

/* Config block */

/* The implementation of the "dispatchers and combiners" here is rotten.
 * I mean to delete it at some point. Maybe replace it with something
 * better. The point is that the current code does not do what it should
 * do, namely write in several buckets simultaneously. So in essence,
 * it's just wasted energy.
 */
#define xxxDISPATCHERS_AND_COMBINERS

/* Size of the row blocks. This impacts both the immediate blocks, and
 * the dispatch/combine blocks */
size_t rowbatch = 2048;

/* Immediate blocks have this many columns. */
size_t colbatch0 = 65536;

/* Coefficients whose absolute value is below this bound are stored as
 * repetition of the column index several times.
 */
int coeff_repeat_bound = 4;

int debug_print = 0;

#ifdef DISPATCHERS_AND_COMBINERS
/* column indices below col_col_dispatcher_cutoff are treated as immediate
 * blocks.  Setting this cutoff to 1 is
 * sufficient to force the first vertical strip to be processed as
 * immediate blocks only, which is generally something we want
 * because there are so many coefficients there. */
size_t col_dispatcher_cutoff = 262144;

/* For the dispatcher/combiner split, we treat this number of columns at
 * a time.  */

/* 8k 0.82 cpu @100
 * 16k 0.74 cpu @100
 * 32k 0.71 cpu @100
 * 64k 0.70 cpu @100
 */
size_t colbatch1 = 65536;
#endif

static inline uint64_t cputicks()
{
        uint64_t r;
        __asm__ __volatile__(
                "rdtsc\n\t"
                "shlq $32, %%rdx\n\t"
                "orq %%rdx, %%rax\n\t"
                : "=a"(r)
                :
                : "rdx");
        return r;
}

/* {{{ some boilerplate related to saving cache files */

struct cachefile {
    FILE * f;
    cachefile(FILE * f) : f(f) {}
    template<typename T> struct basic_seq {
        cachefile& out(cachefile& c, T const * t, size_t n) const {
            for(size_t i = 0 ; i < n ; i++) c << t[i];
            return c;
        }
        cachefile& in(cachefile& c, T * t, size_t n) {
            for(size_t i = 0 ; i < n ; i++) c >> t[i];
            return c;
        }
    };
    template<typename T> struct seq : public basic_seq<T> {
    };
};

template<> struct cachefile::seq<uint16_t> : public cachefile::basic_seq<uint16_t> {
    typedef uint16_t T;
    cachefile& in(cachefile& c, T * t, size_t n) {
        MATMUL_COMMON_READ_MANY16(t, n, c.f);
        return c;
    }
    cachefile& out(cachefile& c, T const * t, size_t n) const {
        MATMUL_COMMON_WRITE_MANY16(t, n, c.f);
        return c;
    }
};

template<> struct cachefile::seq<uint32_t> : public cachefile::basic_seq<uint32_t> {
    typedef uint32_t T;
    cachefile& in(cachefile& c, T * t, size_t n) {
        MATMUL_COMMON_READ_MANY32(t, n, c.f);
        return c;
    }
    cachefile& out(cachefile& c, T const * t, size_t n) const {
        MATMUL_COMMON_WRITE_MANY32(t, n, c.f);
        return c;
    }
};

template<> struct cachefile::seq<pair<int, uint32_t>> : public cachefile::basic_seq<pair<int, uint32_t>> {
    typedef pair<int, uint32_t> T;
    cachefile& in(cachefile& c, T * t, size_t n) {
        MATMUL_COMMON_READ_MANY32(t, 2*n, c.f);
        return c;
    }
    cachefile& out(cachefile& c, T const * t, size_t n) const {
        MATMUL_COMMON_WRITE_MANY32(t, 2*n, c.f);
        return c;
    }
};

template<> struct cachefile::seq<pair<uint16_t, int32_t>> : public cachefile::basic_seq<pair<uint16_t, int32_t>> {
    typedef pair<uint16_t, int32_t> T;
    cachefile& in(cachefile& c, T * t, size_t n) {
        MATMUL_COMMON_READ_MANY16(t, 3*n, c.f);
        return c;
    }
    cachefile& out(cachefile& c, T const * t, size_t n) const {
        MATMUL_COMMON_WRITE_MANY16(t, 3*n, c.f);
        return c;
    }
};

template<> struct cachefile::seq<pair<uint16_t, uint16_t>> : public cachefile::basic_seq<pair<uint16_t, uint16_t>> {
    typedef pair<uint16_t, uint16_t> T;
    cachefile& in(cachefile& c, T * t, size_t n) {
        MATMUL_COMMON_READ_MANY16(t, 2*n, c.f);
        return c;
    }
    cachefile& out(cachefile& c, T const * t, size_t n) const {
        MATMUL_COMMON_WRITE_MANY16(t, 2*n, c.f);
        return c;
    }
};

template<> struct cachefile::seq<pair<int, pair<uint32_t, int32_t>>> : public cachefile::basic_seq<pair<int, pair<uint32_t, int32_t>>> {
    typedef pair<int, pair<uint32_t, int32_t>> T;
    cachefile& in(cachefile& c, T * t, size_t n) {
        MATMUL_COMMON_READ_MANY32(t, 3*n, c.f);
        return c;
    }
    cachefile& out(cachefile& c, T const * t, size_t n) const {
        MATMUL_COMMON_WRITE_MANY32(t, 3*n, c.f);
        return c;
    }
};

template<> struct cachefile::seq<pair<uint16_t, pair<uint16_t, int32_t>>> : public cachefile::basic_seq<pair<uint16_t, pair<uint16_t, int32_t>>> {
    typedef pair<uint16_t, pair<uint16_t, int32_t>> T;
    cachefile& in(cachefile& c, T * t, size_t n) {
        MATMUL_COMMON_READ_MANY16(t, 4*n, c.f);
        return c;
    }
    cachefile& out(cachefile& c, T const * t, size_t n) const {
        MATMUL_COMMON_WRITE_MANY16(t, 4*n, c.f);
        return c;
    }
};


cachefile& operator>>(cachefile & c, uint32_t & x) {
    MATMUL_COMMON_READ_ONE32(x, c.f);
    return c;
}

cachefile& operator<<(cachefile & c, uint32_t const & x) {
    MATMUL_COMMON_WRITE_ONE32(x, c.f);
    return c;
}

cachefile& operator>>(cachefile & c, int32_t & x) {
    MATMUL_COMMON_READ_ONE32(x, c.f);
    return c;
}

cachefile& operator<<(cachefile & c, int32_t const & x) {
    MATMUL_COMMON_WRITE_ONE32(x, c.f);
    return c;
}

cachefile& operator>>(cachefile & c, uint64_t & x) {
    MATMUL_COMMON_READ_ONE64(x, c.f);
    return c;
}

cachefile& operator<<(cachefile & c, uint64_t const & x) {
    MATMUL_COMMON_WRITE_ONE64(x, c.f);
    return c;
}

cachefile& operator>>(cachefile & c, int64_t & x) {
    MATMUL_COMMON_READ_ONE64(x, c.f);
    return c;
}

cachefile& operator<<(cachefile & c, int64_t const & x) {
    MATMUL_COMMON_WRITE_ONE64(x, c.f);
    return c;
}

template<typename T> cachefile& operator>>(cachefile & c, cachefile::seq<T> & s);
template<typename T> cachefile& operator<<(cachefile & c, cachefile::seq<T> const & s);

template<typename T> cachefile& operator>>(cachefile & c, vector<T>&v)
{
    size_t size;
    c >> size;
    v.insert(v.end(), size, T());
    return cachefile::seq<T>().in(c, &(v[0]), v.size());
}
template<typename T> cachefile& operator<<(cachefile & c, vector<T> const &v)
{
    c << v.size();
    return cachefile::seq<T>().out(c, &(v[0]), v.size());
}
// template<typename T> inline cachefile& operator>>(cachefile& c, T& z) { return z.cachefile_load(c); }
// template<typename T> inline cachefile& operator<<(cachefile& c, T const & z) { return z.cachefile_save(c); }
/* }}} */

struct placed_block {/*{{{*/
    unsigned int i0, j0;
    placed_block() { i0 = j0 = 0; }
    placed_block(unsigned int i0, unsigned int j0) : i0(i0), j0(j0) {}
    struct rowmajor_sorter {/*{{{*/
        inline bool operator()(placed_block const& a, placed_block const& b) const {
            return a.i0 < b.i0 || (a.i0 == b.i0 && a.j0 < b.j0);
        }
    };/*}}}*/
    struct colmajor_sorter {/*{{{*/
        inline bool operator()(placed_block const& a, placed_block const& b) const {
            return a.j0 < b.j0 || (a.j0 == b.j0 && a.i0 < b.i0);
        }
    };/*}}}*/
    cachefile& cachefile_load(cachefile& c) {/*{{{*/
        return c >> i0 >> j0;
    }/*}}}*/
    cachefile& cachefile_save(cachefile& c) const {/*{{{*/
        return c << i0 << j0;
    }/*}}}*/
};
inline cachefile& operator>>(cachefile& c, placed_block& z) { return z.cachefile_load(c); }
inline cachefile& operator<<(cachefile& c, placed_block const & z) { return z.cachefile_save(c); }
/*}}}*/

struct zone : public placed_block { /* {{{ (immediate zones) */
    typedef vector<pair<uint16_t, uint16_t>> qpm_t;
    typedef vector<pair<uint16_t, pair<uint16_t, int32_t>>> qg_t;
    qpm_t qp, qm;
    qg_t qg;
    zone() {}
    zone(unsigned int i0, unsigned int j0) : placed_block(i0, j0) {}
    inline bool empty() const { return qp.empty() && qm.empty() && qg.empty(); }
    inline size_t size() const { return qp.size() + qm.size() + qg.size(); }
    void mul(fast_elt_ur *, const fast_elt *, gfp::elt const&, gfp::preinv const&) const;
    void tmul(fast_elt_ur *, const fast_elt *, gfp::elt const&, gfp::preinv const&) const;

    struct sort_qpm {
        inline bool operator()(qpm_t::value_type const& a, qpm_t::value_type const& b) const {
            return a.second < b.second;
        }
    };

    struct sort_qg {
        inline bool operator()(qg_t::value_type const& a, qg_t::value_type const& b) const {
            return a.second.first < b.second.first;
        }
    };

    void sort() {
        std::sort(qp.begin(), qp.end(), sort_qpm());
        std::sort(qm.begin(), qm.end(), sort_qpm());
        std::sort(qg.begin(), qg.end(), sort_qg());
    }
    cachefile& cachefile_load(cachefile& c) {/*{{{*/
        return c >> (placed_block&) *this >> qp >> qm >> qg;
    }/*}}}*/
    cachefile& cachefile_save(cachefile& c) const {/*{{{*/
        return c << (placed_block const&) *this << qp << qm << qg;
    }/*}}}*/
};
inline cachefile& operator>>(cachefile& c, zone& z) { return z.cachefile_load(c); }
inline cachefile& operator<<(cachefile& c, zone const & z) { return z.cachefile_save(c); }
/*}}}*/
#ifdef DISPATCHERS_AND_COMBINERS
struct dispatcher : public vector<uint16_t>, public placed_block {/*{{{*/
    typedef vector<uint16_t> super;
    /* a dispatcher contains: (col id)* */
    cachefile& cachefile_load(cachefile& c) {/*{{{*/
        return c >> (placed_block&)*this >> (super&)*this;
    }/*}}}*/
    cachefile& cachefile_save(cachefile& c) const {/*{{{*/
        return c << (placed_block const&)*this << (super const&)*this;
    }/*}}}*/
};
inline cachefile& operator>>(cachefile& c, dispatcher& z) { return z.cachefile_load(c); }
inline cachefile& operator<<(cachefile& c, dispatcher const & z) { return z.cachefile_save(c); }
/*}}}*/
struct combiner : public placed_block {/*{{{*/
    /* which buffer will we read from, and how many values, from
     * where ?  */
    size_t index, offset, count;
    /* a combiner contains: (dest row id)* (index range is duplicated, in
     * order to account for the fact that we offset the index by a full
     * row batch to account for negative coefficients) */
    typedef uint16_t main_value_type;
    vector<main_value_type> main;
    vector<pair<uint16_t, int32_t>> aux;
    cachefile& cachefile_load(cachefile& c) {/*{{{*/
        return c >> (placed_block&)*this
            >> index >> offset >> count
            >> main >> aux;
    }/*}}}*/
    cachefile& cachefile_save(cachefile& c) const {/*{{{*/
        return c << (placed_block const&)*this
            << index << offset << count
            << main << aux;
    }/*}}}*/
};
inline cachefile& operator>>(cachefile& c, combiner& z) { return z.cachefile_load(c); }
inline cachefile& operator<<(cachefile& c, combiner const & z) { return z.cachefile_save(c); }
/*}}}*/
/* {{{ Temporary buffers which are used when reading the dispatchers.  */
struct temp_buffer {
    unsigned int j0;
    typedef vector<fast_elt, aligned_allocator<fast_elt> > vec_type;  
    vec_type v;
    vec_type::iterator ptr;
    temp_buffer(unsigned int j0, size_t n) : j0(j0), v(n), ptr(v.begin()) {}
    void rewind() { ptr = v.begin(); }
};
/* }}} */
#endif

struct block_of_rows : public placed_block {/*{{{*/
    /* A block of rows contains:
     *
     *  - immediate zones, presumably for leftmost blocks.
     *  - dispatcher blocks
     *  - combiner blocks
     *
     * It is processed in the order found in matmul_zone_data::mul
     */
    vector<zone> Z;
#ifdef DISPATCHERS_AND_COMBINERS
    vector<dispatcher> D;       /* can be empty */
    vector<combiner> C;
#endif
    block_of_rows() {}
    block_of_rows(unsigned int i0) : placed_block(i0, 0) {}
    cachefile& cachefile_load(cachefile& c) {/*{{{*/
        c >> (placed_block&)*this >> Z;
#ifdef DISPATCHERS_AND_COMBINERS
        c >> D >> C;
#endif
        return c;
    }/*}}}*/
    cachefile& cachefile_save(cachefile& c) const {/*{{{*/
        c << (placed_block const&)*this << Z;
#ifdef DISPATCHERS_AND_COMBINERS
        c << D << C;
#endif
        return c;
    }/*}}}*/
};/*}}}*/

struct matmul_zone_data {/*{{{*/
    /* repeat the fields from the public interface */
    struct matmul_public_s public_[1];
    /* now our private fields */
    abdst_field xab;

    vector<block_of_rows> blocks;

    vector<fast_elt, aligned_allocator<fast_elt> > alternate_src;
    vector<fast_elt, aligned_allocator<fast_elt> > alternate_dst;

#ifdef DISPATCHERS_AND_COMBINERS
    size_t maxmaxw = 0;
#endif

    /* {{{ timing data */
    struct twn {
        uint64_t tt;
        size_t w;
        unsigned int n;
        // twn() : tt(0), w(0), n(0) {}
    };

    typedef map<unsigned int, twn> tmap_t;
    tmap_t tmap;
    /* }}} */

    /* {{{ front-end */
    ~matmul_zone_data();
    matmul_zone_data(void* xx, param_list pl, int optimized_direction);
    void build_cache(uint32_t * data);
    int reload_cache();
    void save_cache();
    void mul(void * xdst, void const * xsrc, int d);
    void report(double scale MAYBE_UNUSED);
    void auxv(int op, va_list ap);
    void aux(int op, ...);
    /* }}} */
};
/*}}}*/
/**************************************************************************/
/*{{{ trampolines for C bindings */
void MATMUL_NAME(clear)(matmul_ptr mm0)
{
    delete (matmul_zone_data *) mm0;
}

matmul_ptr MATMUL_NAME(init)(void* xx, param_list pl, int optimized_direction)
{
    return (matmul_ptr) new matmul_zone_data(xx, pl, optimized_direction);
}

void MATMUL_NAME(build_cache)(matmul_ptr mm0, uint32_t * data)
{
    ((matmul_zone_data*)mm0)->build_cache(data);
}

int MATMUL_NAME(reload_cache)(matmul_ptr mm0)
{
    return ((matmul_zone_data*)mm0)->reload_cache();
}

void MATMUL_NAME(save_cache)(matmul_ptr mm0)
{
    ((matmul_zone_data*)mm0)->save_cache();
}

void MATMUL_NAME(mul)(matmul_ptr mm0, void * xdst, void const * xsrc, int d)
{
    ((matmul_zone_data*)mm0)->mul(xdst, xsrc, d);
}

void MATMUL_NAME(report)(matmul_ptr mm0 MAYBE_UNUSED, double scale MAYBE_UNUSED) {
    ((matmul_zone_data*)mm0)->report(scale);
}

void MATMUL_NAME(auxv)(matmul_ptr mm0 MAYBE_UNUSED, int op MAYBE_UNUSED, va_list ap MAYBE_UNUSED)
{
    ((matmul_zone_data*)mm0)->auxv(op, ap);
}

void MATMUL_NAME(aux)(matmul_ptr mm0, int op, ...)
{
    va_list ap;
    va_start(ap, op);
    ((matmul_zone_data*)mm0)->auxv(op, ap);
    va_end(ap);
}
/*}}}*/

/**************************************************************************/

matmul_zone_data::~matmul_zone_data() {/*{{{*/
    matmul_common_clear(public_);
}
/*}}}*/
matmul_zone_data::matmul_zone_data(void* xab, param_list pl, int optimized_direction) : xab((abdst_field) xab)/*{{{*/
{
    memset(&public_, 0, sizeof(public_));
    int suggest = optimized_direction ^ MM_DIR0_PREFERS_TRANSP_MULT;
    public_->store_transposed = suggest;
    if (pl) {
        param_list_parse_int(pl, "mm_store_transposed",
                &public_->store_transposed);
        if (public_->store_transposed != suggest) {
            fprintf(stderr, "Warning, mm_store_transposed"
                    " overrides suggested matrix storage ordering\n");
        }
    }
}
/*}}}*/

struct coeff_stats {/*{{{*/
    int bound;
    vector<uint64_t> ccount;
    coeff_stats(int c) : bound(c), ccount(2*c+1, 0) {}
    void operator()(int c) {
        if (c < 0 && c >= -bound) {
            ccount[bound + c]++;
        } else if (c > 0 && c <= bound) {
            ccount[bound + c]++;
        } else {
            ccount[0]++;
        }
    }
    void report(std::ostream& o, size_t nrows) {
        o << "coeff stats per row";
        int maxnz = bound;
        for( ; maxnz > 1 ; maxnz--)
            if (ccount[bound + maxnz] + ccount[bound - maxnz]) break;
        for(int i = 1 ; i <= maxnz ; i++) {
            o << " ±" << i << ":" << (double) (ccount[bound + i] + ccount[bound - i]) / nrows;
        }
        if (ccount[bound]) o << " other:" << (double) ccount[bound] / nrows;   
        o << "\n";
    }
};
/*}}}*/
struct sort_jc {/*{{{*/
    inline bool operator()(pair<uint32_t, int32_t> const& a, pair<uint32_t,int32_t> const& b) const {
        return a.first < b.first;
    }
};
/*}}}*/

#ifdef DISPATCHERS_AND_COMBINERS
void merge_dispatchers(vector<dispatcher>& all, size_t maxmaxw)/*{{{*/
{
    typedef dispatcher dispatcher;
    vector<dispatcher> merged;
    for(size_t k0 = 0, k1; k0 < all.size() ; ) {
        /* recompute maxw, since we haven't kept track */
        size_t maxw = 0;
        unsigned int j0 = all[k0].j0;
        for(k1 = k0 ; k1 < all.size() && all[k1].j0 == all[k0].j0 ; k1++) {
            maxw = max(maxw, all[k1].size());
        }
        /* the range [k0..k1[ has dispatchers for the same set of
         * columns. */
        unsigned int group = maxmaxw / maxw;
        unsigned int nm = 0;
        size_t old_k0 = k0;
        for( ; k0 < k1 ; ) {
            dispatcher D;
            D.i0 = all[k0].i0;
            D.j0 = all[k0].j0;
            /* concatenate. */
            for(unsigned int k2 = 0 ; k2 < group && k0 < k1 ; k2++, k0++) {
                D.insert(D.end(), all[k0].begin(), all[k0].end());
                /* free this memory as early as we can */
                all[k0].clear();
                /* XXX at this point, combiner must get the info that it
                 * should read in the buffer from index D.size()
                 */
            }
            merged.push_back(std::move(D));
            nm++;
        }
        if (debug_print)
            printf("j0=%u: maxw = %zu ; group %u together. Split %zd dispatchers [%zu..%zu[ into %u dispatchers\n", j0, maxw, group, k1 - old_k0, old_k0, k1, nm);
    }
    if (debug_print)
        printf("We now have %zu dispatchers, combined from %zu original\n",
                merged.size(), all.size());
    all.swap(merged);
}
/*}}}*/

pair<dispatcher, combiner> create_dispatcher_and_combiner(zone const& q)/*{{{*/
{
    dispatcher D;
    combiner C;
    D.i0 = C.i0 = q.i0;
    D.j0 = C.j0 = q.j0;
    for(auto const& ij : q.qp) {
        uint16_t destrow_id = ij.first;
        uint16_t col_id = ij.second;
        D.push_back(col_id);
        C.main.push_back(destrow_id);
    }
    for(auto const& ij : q.qm) {
        uint16_t destrow_id = ij.first;
        uint16_t col_id = ij.second;
        D.push_back(col_id);
        /* specific offset for negative coefficients */
        C.main.push_back(destrow_id + rowbatch);
    }
    for(auto const& ijc : q.qg) {
        uint16_t col_id = ijc.second.first;
        uint16_t destrow_id = ijc.first;
        int32_t coeff = ijc.second.second;
        D.push_back(col_id);
        C.aux.push_back(make_pair(destrow_id, coeff));
    }
    return make_pair(D, C);
}/*}}}*/
#endif

void matmul_zone_data::build_cache(uint32_t * data)/*{{{*/
{
    matmul_zone_data * mm = this;

    ASSERT_ALWAYS(data);

    unsigned int nrows_t = mm->public_->dim[ mm->public_->store_transposed];
    unsigned int ncols_t = mm->public_->dim[!mm->public_->store_transposed];

    uint32_t * ptr = data;

    /* count coefficients */
    mm->public_->ncoeffs = 0;

    double zavg = 0;

#ifdef DISPATCHERS_AND_COMBINERS
    /* immediate zones as well as combiners are stored right inside the
     * block_of_rows structures. As for dispatchers, we'll put them there
     * in a second pass.
     */
    vector<dispatcher> D;
#endif

    size_t n_immediate = 0;

    coeff_stats cstats(coeff_repeat_bound);

    for(unsigned int i0 = 0 ; i0 < nrows_t ; i0 += rowbatch) {
        /* Create the data structures for the horizontal strip starting
         * at row i0, column 0.
         */
        block_of_rows B(i0);

        /* Because this horizontal strip will be split in many blocks, we
         * need to have a batch of pointers for reading each row. */
        uint32_t * pp[rowbatch + 1];
        uint32_t * cc[rowbatch + 1];
        pp[0] = ptr;
        for(unsigned int k = 0 ; k < rowbatch ; k++) {
            cc[k] = pp[k] + 1;
            if (i0 + k < nrows_t) {
                uint32_t weight = *pp[k];
                pp[k+1] = pp[k] + 1 + 2*weight;
                mm->public_->ncoeffs += weight;
                /* This is very important. We must sort rows before
                 * processing. */
                pair<uint32_t, int32_t> * cb = (pair<uint32_t, int32_t> *) cc[k];
                pair<uint32_t, int32_t> * ce = cb + weight;
                sort(cb, ce, sort_jc());
            } else {
                pp[k+1] = pp[k];
            }
        }
        ptr = pp[rowbatch];
        for(unsigned int j0 = 0, colbatch ; j0 < ncols_t ; j0 += colbatch) {
#ifdef DISPATCHERS_AND_COMBINERS
            colbatch = (j0 < col_dispatcher_cutoff) ? colbatch0 : colbatch1;
#else
            colbatch = colbatch0;
#endif
            zone z(i0, j0);
            for(unsigned int k = 0 ; k < rowbatch ; k++) {
                for( ; cc[k] < pp[k+1] ; cc[k] += 2) {
                    uint32_t j = cc[k][0] - j0;
                    int32_t c = cc[k][1];
                    if (j >= colbatch) break;
                    cstats(c);
                    if (c < 0 && c >= -coeff_repeat_bound) {
                        for( ; c++ ; ) {
                            z.qm.push_back(make_pair(k, j));
                        }
                    } else if (c > 0 && c <= coeff_repeat_bound) {
                        for( ; c-- ; ) {
                            z.qp.push_back(make_pair(k, j));
                        }
                    } else {
                        z.qg.push_back(make_pair(k, make_pair(j, c)));
                    }
                }
            }
            z.sort();
            // printf("Zone %zu @[%u,%u]: %zu+%zu+%zu\n", B.Z.size(), z.i0, z.j0, z.qp.size(), z.qm.size(), z.qg.size());
            if (z.empty()) continue;

#ifdef DISPATCHERS_AND_COMBINERS
            if (j0 >= col_dispatcher_cutoff) {
                /* blocks which are not in the first strip go for
                 * the dispatcher/combiner split. */
                if (z.size() > maxmaxw)
                    maxmaxw = z.size();
                auto DC = create_dispatcher_and_combiner(z);
                D.push_back(std::move(DC.first));
                B.C.push_back(std::move(DC.second));
            } else
#endif
            {
                zavg += z.size();
                B.Z.push_back(std::move(z));
                n_immediate++;
            }
        }
        blocks.push_back(std::move(B));
    }
    ASSERT_ALWAYS(ptr - data == (ptrdiff_t) (nrows_t + 2 * mm->public_->ncoeffs));
#ifdef DISPATCHERS_AND_COMBINERS
    /* We now merge dispatchers */
    sort(D.begin(), D.end(), dispatcher::colmajor_sorter());
    merge_dispatchers(D, maxmaxw);
    sort(D.begin(), D.end(), dispatcher::rowmajor_sorter());
    /* And put the dispatchers to the proper place */
    for(size_t i = 0, j = 0; i < D.size() ; i++) {
        for ( ; D[i].i0 > blocks[j].i0 ; j++) ;
        blocks[j].D.push_back(std::move(D[i]));
    }
#endif

    free(data);
    ostringstream os;
    if (debug_print) {
        cstats.report(os, nrows_t);
        printf("Stats: [%" PRIu64 " coeffs]:"
                " %zu immediate zones of average weight %.1f\n%s",
                mm->public_->ncoeffs,
                n_immediate, zavg / n_immediate,
                os.str().c_str());
    }
}
/*}}}*/
/* cache load and save {{{ */
int matmul_zone_data::reload_cache()
{
    FILE * f = matmul_common_reload_cache_fopen(sizeof(abelt), public_, MM_MAGIC);
    if (f == NULL) { return 0; }
    cachefile c(f);
    c >> blocks;
#ifdef DISPATCHERS_AND_COMBINERS
    maxmaxw = 0;
    for(auto const& B : blocks) {
        for(auto const& D : B.D) {
            maxmaxw = std::max(D.size(), maxmaxw);
        }
    }
#endif
    fclose(f);

    return 1;
}

void matmul_zone_data::save_cache()
{
    FILE * f = matmul_common_save_cache_fopen(sizeof(abelt), public_, MM_MAGIC);
    cachefile c(f);
    c << blocks;
    fclose(f);
}
/* }}} */

#if 0
extern "C" {
extern void gfp3_dispatch_add(void * tdst, const void * tsrc, const void * p, size_t size);
extern void gfp3_dispatch_sub(void * tdst, const void * tsrc, const void * p, size_t size);
}

void __attribute__((noinline)) gfp3_dispatch_add (void * tdst, const void * tsrc, const void * p, size_t size)
{
    gfp::elt_ur * tdst0 = (gfp::elt_ur *) tdst; 
    const gfp::elt * tsrc0 = (const gfp::elt *) tsrc; 
    const zone::qpm_t::value_type * q = (const zone::qpm_t::value_type *) p;
    for( ; size-- ; q++) {
        gfp::add(tdst0[q->first], tsrc0[q->second]);
    }
}

void __attribute__((noinline)) gfp3_dispatch_sub (void * tdst, const void * tsrc, const void * p, size_t size)
{
    gfp::elt_ur * tdst0 = (gfp::elt_ur *) tdst; 
    const gfp::elt * tsrc0 = (const gfp::elt *) tsrc; 
    const zone::qpm_t::value_type * q = (const zone::qpm_t::value_type *) p;
    for( ; size-- ; q++) {
        gfp::sub(tdst0[q->first], tsrc0[q->second]);
    }
}
#endif
#if 1
extern "C" {
extern void gfp3_dispatch_add(void * tdst, const void * tsrc, const void * p, size_t size);
extern void gfp3_dispatch_sub(void * tdst, const void * tsrc, const void * p, size_t size);
}

void __attribute__((noinline)) gfp3_dispatch_add (void * tdst, const void * tsrc, const void * p, size_t size)
{
    fast_elt_ur * tdst0 = (fast_elt_ur *) tdst; 
    const fast_elt * tsrc0 = (const fast_elt *) tsrc; 
    const zone::qpm_t::value_type * q = (const zone::qpm_t::value_type *) p;
    for( ; size-- ; q++) {
        fast_gfp::add(tdst0[q->first], tsrc0[q->second]);
    }
}

void __attribute__((noinline)) gfp3_dispatch_sub (void * tdst, const void * tsrc, const void * p, size_t size)
{
    fast_elt_ur * tdst0 = (fast_elt_ur *) tdst; 
    const fast_elt * tsrc0 = (const fast_elt *) tsrc; 
    const zone::qpm_t::value_type * q = (const zone::qpm_t::value_type *) p;
    for( ; size-- ; q++) {
        fast_gfp::sub(tdst0[q->first], tsrc0[q->second]);
    }
}
#endif

void zone::mul(fast_elt_ur * tdst, const fast_elt * tsrc,
        gfp::elt const& prime, gfp::preinv const& preinverse) const
{
#if 1
    for(auto const& ij : qp) 
        fast_gfp::add(tdst[ij.first], tsrc[ij.second]);
    for(auto const& ij : qm) 
        fast_gfp::sub(tdst[ij.first], tsrc[ij.second]);
#else
    gfp3_dispatch_add((void*)tdst, (const void*)tsrc, (const void*)(&*qp.begin()), qp.size());
    gfp3_dispatch_sub((void*)tdst, (const void*)tsrc, (const void*)(&*qm.begin()), qm.size());
#endif
    for(auto const& ijc : qg) {
        uint16_t i = ijc.first;
        uint16_t j = ijc.second.first;
        int32_t c = ijc.second.second;
        if (c>0) {
            fast_gfp::addmul_ui(tdst[i], tsrc[j], c, prime, preinverse);
        } else {
            fast_gfp::submul_ui(tdst[i], tsrc[j], -c, prime, preinverse);
        }
    }
}

void zone::tmul(fast_elt_ur * tdst, const fast_elt * tsrc,
        gfp::elt const& prime, gfp::preinv const& preinverse) const
{
#if 1
    for(auto const& ij : qp) 
        fast_gfp::add(tdst[ij.second], tsrc[ij.first]);
    for(auto const& ij : qm) 
        fast_gfp::sub(tdst[ij.second], tsrc[ij.first]);
#else
    gfp3_dispatch_add((void*)tdst, (const void*)tsrc, (const void*)(&*qp.begin()), qp.size());
    gfp3_dispatch_sub((void*)tdst, (const void*)tsrc, (const void*)(&*qm.begin()), qm.size());
#endif
    for(auto const& ijc : qg) {
        uint16_t i = ijc.first;
        uint16_t j = ijc.second.first;
        int32_t c = ijc.second.second;
        if (c>0) {
            fast_gfp::addmul_ui(tdst[j], tsrc[i], c, prime, preinverse);
        } else {
            fast_gfp::submul_ui(tdst[j], tsrc[i], -c, prime, preinverse);
        }
    }
}

void matmul_zone_data::mul(void * xdst, void const * xsrc, int d)
{
    matmul_zone_data * mm = this;
    ASM_COMMENT("multiplication code");
    abdst_field x = mm->xab;

    gfp::preinv preinverse;
    gfp::elt prime;

    {
        mpz_t p;
        mpz_init(p);
        abfield_characteristic(x, p);
        prime = p;
        mpz_clear(p);
    }

    gfp::compute_preinv(preinverse, prime);

    const fast_elt * src;
    fast_elt * dst;

    size_t nsrc = mm->public_->dim[d];
    size_t ndst = mm->public_->dim[!d];
    
    if (arith_modp::details::is_same<gfp::elt, fast_elt>::value) {
        src = (const fast_elt *) xsrc;
        dst = (fast_elt *) xdst;
    } else {
        if (alternate_src.empty()) alternate_src.assign(nsrc, fast_elt());
        if (alternate_dst.empty()) alternate_dst.assign(ndst, fast_elt());
        ASSERT_ALWAYS(alternate_src.size() == nsrc);
        ASSERT_ALWAYS(alternate_dst.size() == ndst);
        /* we read items in xsrc exactly as they are, which is gfp::elt's.
         * And because those are convertible to fast_gfp::elt's, we'll
         * get our vector.
         */
        const gfp::elt * begin = (const gfp::elt *) xsrc;
        const gfp::elt * end = begin + nsrc;
        alternate_src.assign(begin, end);
        src = &alternate_src.front();
        dst = &alternate_dst.front();
    }


    /* d == 1: matrix times vector product */
    /* d == 0: vector times matrix product */

    /* However the matrix may be stored either row-major
     * (store_transposed == 0) or column-major (store_transposed == 1)
     */

    fast_elt::zero(dst, ndst);

    /* Processing order:
     *
     *  - vector data corresponding to the combiner blocks is
     *  read from the buffers, and stored to a buffer which stores a
     *  write window to the destination
     *  - immediate blocks are read an applied
     *  - the write window to the destination undergoes reduction, and
     *  final store.
     *  - dispatcher blocks (if any) are processed, thereby refilling
     *  buffers which were emptied when processing their last combiner.
     */

#ifdef DISPATCHERS_AND_COMBINERS
    vector<temp_buffer> buffers;
    unsigned int ncols_t = mm->public_->dim[!mm->public_->store_transposed];

    /* replay the sequence of starting column indices */
    for(unsigned int j0 = 0, colbatch ; j0 < ncols_t ; j0 += colbatch) {
        if (j0 < col_dispatcher_cutoff) {
            colbatch = colbatch0;
        } else {
            colbatch = colbatch1;
            buffers.push_back(std::move(temp_buffer(j0, maxmaxw)));
        }
    }
#endif

    /* TODO: missing in mpfq elt_ur_{add,sub}_elt */
    if (d == !mm->public_->store_transposed) {
#ifdef DISPATCHERS_AND_COMBINERS
        /* Doubling rowbatch is because we do a nasty trick with the
         * indices for the negative coefficients. Oddly enough, we don't
         * seem to do so currently for the immediate zones...
         */
        fast_elt_ur * tdst = new fast_elt_ur[2*rowbatch];
#else
        fast_elt_ur * tdst = new fast_elt_ur[rowbatch];
#endif

        ASM_COMMENT("critical loop");

        for(auto const& B : blocks) {
#ifdef DISPATCHERS_AND_COMBINERS
            fast_elt_ur::zero(tdst, 2 * rowbatch);
#else
            fast_elt_ur::zero(tdst, rowbatch);
#endif

#ifdef DISPATCHERS_AND_COMBINERS
            /* loop through all dispatcher strips, and pre-fill buffers if
             * we happen to need it. */
            for(size_t j = 0, k = 0; j < B.D.size() ; j++) {
                auto const& d(B.D[j]);
                ASSERT_ALWAYS(d.i0 == B.i0);
                for( ; buffers[k].j0 < d.j0 ; k++) ;

                /* Fill that buffer now ! */
                auto ptr = buffers[k].v.begin();
                for(auto const& x : d) {
                    fast_gfp::stream_store(&*ptr++, src[d.j0 + x]);
                    // *ptr++ = src[d.j0 + x];
                }
                buffers[k].rewind();
            }
#endif

            /* process immediate zones */
            for(auto const & z : B.Z) {
                z.mul(tdst, src + z.j0, prime, preinverse);
            }

#ifdef DISPATCHERS_AND_COMBINERS
            /* Read all combiner data, and apply it */
            for(size_t j = 0, k = 0 ; j < B.C.size() ; j++, k++) {
                for( ; buffers[k].j0 < B.C[j].j0 ; k++) ;
                for(auto const& x : B.C[j].main) {
                    ASSERT(buffers[k].ptr < buffers[k].v.end());
                    fast_gfp::add(tdst[x], *(gfp::elt *)&*buffers[k].ptr++);
                }
                /* Almost surely this loop will never run */
                for(auto const& xc : B.C[j].aux) {
                    auto x = xc.first;
                    auto c = xc.second;
                    if (c > 0) {
                        fast_gfp::addmul_ui(tdst[x], *buffers[k].ptr++, c, prime, preinverse);
                    } else {
                        fast_gfp::submul_ui(tdst[x], *buffers[k].ptr++, -c, prime, preinverse);
                    }
                }
            }
#endif

            /* reduce last batch. It could possibly be incomplete */
            size_t active = std::min(rowbatch,
                    (size_t) (mm->public_->dim[!d] - B.i0));
            for(size_t i = 0 ; i < active ; i++) {
#ifdef DISPATCHERS_AND_COMBINERS
                fast_gfp::sub_ur(tdst[i], tdst[i + rowbatch]);
#endif
                fast_gfp::reduce(dst[B.i0 + i], tdst[i], prime, preinverse);
            }
        }

        ASM_COMMENT("end of critical loop");
        delete[] tdst;
    } else {
#ifdef DISPATCHERS_AND_COMBINERS
        fprintf(stderr, "transposed product not yet implemented for the dispatch-combine trick");
        ASSERT_ALWAYS(0);
#endif
        fast_elt_ur * tdst = new fast_elt_ur[mm->public_->dim[!d]];
        fast_elt_ur::zero(tdst, mm->public_->dim[!d]);

        ASM_COMMENT("critical loop");

        for(auto const& B : blocks) {
            /* process immediate zones */
            for(auto const & z : B.Z) {
                z.tmul(tdst + z.j0, src + B.i0, prime, preinverse);
            }
        }
        for(size_t j = 0 ; j < mm->public_->dim[!d] ; j++) {
            fast_gfp::reduce(dst[j], tdst[j], prime, preinverse);
        }

        ASM_COMMENT("end of critical loop");
        delete[] tdst;
    }

    ASM_COMMENT("end of multiplication code");

    mm->public_->iteration[d]++;
}

void matmul_zone_data::report(double scale MAYBE_UNUSED)
{
    static pthread_mutex_t lk = PTHREAD_MUTEX_INITIALIZER;
    pthread_mutex_lock(&lk);
    unsigned int niter = public_->iteration[0] + public_->iteration[1];
    for(tmap_t::const_iterator it = tmap.begin() ; it != tmap.end() ; it++) {
        printf("j0=%u [%u zones]: avg %.1f cycles/c [%.1f coeffs avg] - %.1f Mcycles/iter\n",
                it->first, it->second.n / niter, it->second.tt / scale / it->second.w, (double) it->second.w / it->second.n, (double) it->second.tt / niter * 1.0e-6);
    }
    pthread_mutex_unlock(&lk);
}

void matmul_zone_data::auxv(int op MAYBE_UNUSED, va_list ap MAYBE_UNUSED)
{
}

void matmul_zone_data::aux(int op, ...)
{
    va_list ap;
    va_start(ap, op);
    auxv(op, ap);
    va_end(ap);
}

/* vim: set sw=4: */
