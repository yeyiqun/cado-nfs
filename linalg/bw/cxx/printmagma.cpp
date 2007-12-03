#include "constants.hpp"
#include "matrix_header.hpp"
#include "matrix_line.hpp"
#include "must_open.hpp"
#include "parsing_tools.hpp"
#include "arguments.hpp"
#include "common_arguments.hpp"
#include "detect_params.hpp"

#include <iostream>
#include <iterator>

#include <sys/types.h>
#include <dirent.h>


using namespace std;
using namespace boost;

void pvec(const std::string& n, const vector<long>& co)
{
    if (co.empty()) {
        return;
    }
    cout << fmt("%:=Vector([GF(p) | \n") % n;
    copy(co.begin(), co.end() - 1,
            ostream_iterator<long>(cout, ", "));
    cout << co.back() << "]);\n";
}

template<typename T>
void pmatpol(const std::string& s, const vector<T>& co, int m, int n)
{
    if (co.size() % (m*n) != 0) {
        cerr << fmt("% has size % which is not a multiple of %*%\n")
            % s % co.size() % m % n;
    }
    cout << fmt("%_mats:=[\n") % s;
    for(unsigned int d = 0 ; d < co.size() ; d+=m*n) {
        if (d) cout << ",\n";
        cout << "[";
        for(int i = 0 ; i < m*n ; i++) {
            if (i) cout << ",";
            cout << co[d+i];
        }
        cout << "]";
    }
    cout << "];\n";
    cout << fmt("%:=&+[X^(i-1)*RMatrixSpace(KP,%,%)!(%_mats[i]) : "
            "i in [1..#%_mats]];\n") % s % m % n % s % s;
}

template<typename T>
bool rvec(const std::string& f, vector<T>& co)
{
    ifstream y;
    if (!open(y, f)) return false;
    copy(istream_iterator<T>(y),
            istream_iterator<T>(),
            back_insert_iterator<vector<T> >(co));
    return true;
}

bool r_avec(vector<long>& co, int m, int n)
{
    for(int i = 0 ; i < m ; i++) {
        /* rows are always in separate files. */
        for(int j = 0 ; j < n ; ) {
            /* columns might come together ! */
            std::string a_nm = files::a % i % j;
            int ny;
            long t;
            {
                ifstream a(a_nm.c_str());
                if (!a.is_open()) {
                    cerr << fmt("no file %\n") % a_nm;
                    return false;
                }
                std::string line;
                getline(a, line);
                istringstream ss(line);
                for(ny = 0 ; ss >> t ; ny++);
                a.close();
            }
            if (ny == 0) {
                cerr << fmt("problem with file %\n") % a_nm;
            }
            ifstream a(a_nm.c_str());
            for(int d = 0 ; !a.eof() ; d++) {
                int k;
                for(k = 0 ; k < ny && a >> t ; k++) {
                    if ((int) co.size() <= d*m*n) {
                        co.insert(co.end(), (d+1)*m*n - co.size(), 0);
                    }
                    co[d*m*n+i*n+j+k] = t;
                }
                if (k != ny && !a.eof()) {
                    std::string line;
                    getline(a, line);
                    cerr << fmt("error parsing %\n") % a_nm;
                    return false;
                }
            }

            j += ny;
        }
    }
    return true;
}

void do_pi(std::string const& mname, std::string const& fname, int m, int n)
{
    vector<unsigned long> co;
    if (!rvec(fname, co)) {
        cerr << fmt("Uh, % vanished in the air\n");
    }
    pmatpol(mname, co, m+n, m+n);
}

void
try_pi(int m, int n)
{
    DIR * pi_dir;
    struct dirent * curr;

    if ((pi_dir=opendir("."))==NULL) {
        perror(".");
        return;
    }

    for(;(curr=readdir(pi_dir))!=NULL;) {
        int s,e;
        if (sscanf(curr->d_name,"pi-%d-%d",&s,&e)==2) {
            do_pi(fmt("pi_%_%")%s%e, curr->d_name, m, n);
        }
    }

    closedir(pi_dir);

}


int main(int argc, char * argv[])
{
    ios_base::sync_with_stdio(false);
    cerr.tie(&cout);
    cout.rdbuf()->pubsetbuf(0,0);
    cerr.rdbuf()->pubsetbuf(0,0);

    common_arguments co;
    no_arguments nothing;

    process_arguments(argc, argv, co, nothing);

    unsigned int nr;
    unsigned int nc;

    do {
        ifstream mtx;
        string mstr;
        if (!open(mtx, files::matrix)) break;
        get_matrix_header(mtx, nr, nc, mstr);

        cout << fmt("p:=%;\n") % mstr;
        cout << fmt("M:=SparseMatrix(GF(p), %, %, [\n") % nr % nc;
        istream_iterator<matrix_line> mit(mtx);
        unsigned int p;
        vector<pair<pair<uint32_t, uint32_t>, int32_t> > coeffs;

        for(p = 0 ; mit != istream_iterator<matrix_line>() ; p++) {
            matrix_line l = *mit++;
            for(unsigned int k = 0 ; k < l.size() ; k++) {
                coeffs.push_back(
                        make_pair(
                            make_pair(
                                (1+p),
                                (1+l[k].first)),
                            l[k].second));
            }
        }

        for(unsigned int k = 0 ; k < coeffs.size() - 1; k++) {
            cout << fmt("<%, %, %>, ")
                % coeffs[k].first.first
                % coeffs[k].first.second
                % coeffs[k].second;
        }
        cout << fmt("<%, %, %>")
            % coeffs.back().first.first
            % coeffs.back().first.second
            % coeffs.back().second;

        cout << "]);\n";
        cout << "M:=Matrix(M);\n";
    } while (0);

    unsigned int m,n;
    detect_mn(m,n);

    if (m && n) {
        cout << "K:=GF(p);\n";
        cout << fmt("m:=%;\n") % m;
        cout << fmt("n:=%;\n") % n;
        cout << "b:=m+n;\n";
        cout << fmt("N:=%;\n") % nr;
        cout << "Kmb:=KMatrixSpace(K,m,b);\n";
        cout << "Kmn:=KMatrixSpace(K,m,n);\n";
        cout << "Knb:=KMatrixSpace(K,m,b);\n";
        cout << "Kbb:=KMatrixSpace(K,b,b);\n";
        cout << "KP<X>:=PolynomialRing(K);\n";
        cout << "KPmn:=RMatrixSpace(KP,m,n);\n";
        cout << "KPbb:=RMatrixSpace(KP,b,b);\n";
        cout << "KPmb:=RMatrixSpace(KP,m,b);\n";
        cout << "KPnb:=RMatrixSpace(KP,m,b);\n";
        cout << "Kn1:=KMatrixSpace(K,m,1);\n";
        cout << "KPn1:=RMatrixSpace(KP,m,1);\n";
    }

    if (m == 0) {
        cerr << "No X files found\n";
    }

    for(unsigned int j = 0 ; j < m ; j++) {
        ifstream x;
        unsigned int c;
        if (!open(x, files::x % j)) break;
        vector<long> co(nr, 0);
        x >> wanted('e') >> c;
        co[c] = 1;
        pvec(fmt("X%")%j, co);
    }

    if (m) {
        cout << "XX:=Matrix([";
        for(unsigned int j = 0 ; j < m-1 ; j++) cout << fmt("X%, ")%j;
        cout << fmt("X%]);\n")%(m-1);
    }

    if (n == 0) {
        cerr << "No Y files found\n";
    }

    for(unsigned int j = 0 ; j < n ; j++) {
        vector<long> co;
        if (!rvec(files::y % j, co)) break;
        pvec(fmt("Y%")%j, co);
    }

    if (n) {
        cout << "YY:=Matrix([";
        for(unsigned int j = 0 ; j < n-1 ; j++) cout << fmt("Y%, ")%j;
        cout << fmt("Y%]);\n")%(n-1);
    }

    do {
        vector<long> co;
        if (!rvec(files::m0, co)) break;
        pvec("CM0", co);
    } while (0);
    do {
        vector<long> co;
        if (!rvec(files::x0, co)) break;
        pvec("CX0", co);
    } while (0);

    if (!m || !n)
        return 0;

    /* Also dump master information if found */

    do {
        vector<long> co;
        if (!rvec(files::f_init, co)) break;
        pmatpol("F_INIT", co, n, m+n);
    } while (0);

    do {
        vector<long> co;
        if (!r_avec(co, m, n)) break;
        pmatpol("A", co, m, n);
    } while (0);

    try_pi(m,n);
}
/* vim:set sw=4 sta et: */
