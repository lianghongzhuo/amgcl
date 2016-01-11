#ifndef AMGCL_IO_MM_HPP
#define AMGCL_IO_MM_HPP

/*
The MIT License

Copyright (c) 2012-2015 Denis Demidov <dennis.demidov@gmail.com>

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/

/**
 * \file   amgcl/io/mm.hpp
 * \author Denis Demidov <dennis.demidov@gmail.com>
 * \brief  Readers for Matrix Market sparse matrices and dense vectors.
 */

#include <vector>
#include <string>
#include <fstream>
#include <sstream>

#include <boost/type_traits.hpp>
#include <boost/tuple/tuple.hpp>

#include <amgcl/util.hpp>
#include <amgcl/value_type/interface.hpp>

namespace amgcl {
namespace io {

/// Matrix market reader.
class mm_reader {
    public:
        /// Open the file, read the banner.
        mm_reader(const std::string &fname) : fname(fname), f(fname.c_str()) {
            precondition(f, "Failed to open file \"" + fname + "\"");

            // Read banner.
            std::string line;
            precondition(std::getline(f, line), format_error());

            std::istringstream is(line);
            std::string banner, mtx, coord, dtype, storage;

            precondition(
                    is >> banner >> mtx >> coord >> dtype >> storage,
                    format_error());

            precondition(banner  == "%%MatrixMarket", format_error("no banner"));
            precondition(mtx     == "matrix",         format_error("not a matrix"));
            precondition(storage == "general",        format_error("not general"));

            if (coord == "coordinate") {
                _sparse = true;
            } else if (coord == "array") {
                _sparse = false;
            } else {
                precondition(false, format_error("unsupported coordinate type"));
            }

            if (dtype == "real") {
                _complex = false;
                _integer = false;
            } else if (dtype == "complex") {
                _complex = true;
                _integer = false;
            } else if (dtype == "integer") {
                _complex = false;
                _integer = true;
            } else {
                precondition(false, format_error("unsupported data type"));
            }

            // Skip comments.
            std::streampos pos;
            do {
                pos = f.tellg();
                precondition(std::getline(f, line), format_error("unexpected eof"));
            } while (line[0] == '%');

            // Get back to the first non-comment line.
            f.seekg(pos);
        }

        /// Matrix in the file is sparse.
        bool is_sparse()  const { return _sparse; }

        /// Matrix in the file is complex-valued.
        bool is_complex() const { return _complex; }

        /// Matrix in the file is integer-valued.
        bool is_integer() const { return _integer; }

        /// Read sparse matrix from the file.
        template <typename Idx, typename Val>
        boost::tuple<size_t, size_t> operator()(
                std::vector<Idx> &ptr,
                std::vector<Idx> &col,
                std::vector<Val> &val
                )
        {
            precondition(_sparse, format_error("not a sparse matrix"));
            precondition(boost::is_complex<Val>::value == _complex,
                    _complex ?
                        "attempt to read complex values into real vector" :
                        "attempt to read real values into complex vector"
                        );
            precondition(boost::is_integral<Val>::value == _integer,
                    _integer ?
                        "attempt to read integer values into real vector" :
                        "attempt to read real values into integer vector"
                        );

            // Read sizes
            size_t n, m, nnz;
            std::string line;
            {
                precondition(std::getline(f, line), format_error("unexpected eof"));
                std::istringstream is(line);
                precondition(is >> n >> m >> nnz, format_error());
            }

            ptr.clear(); ptr.reserve(n+1);
            col.clear(); col.reserve(nnz);
            val.clear(); val.reserve(nnz);

            Idx last_i = 0;
            for(size_t k = 0; k < nnz; ++k) {
                precondition(std::getline(f, line), format_error("unexpected eof"));
                std::istringstream is(line);

                Idx i, j;
                Val v;

                precondition(is >> i >> j, format_error());
                v = read_value<Val>(is);

                while(last_i < i) {
                    ptr.push_back(col.size());
                    last_i++;
                }

                precondition(
                        static_cast<size_t>(i) == ptr.size(),
                        format_error("inconsistent data"));

                col.push_back(j-1);
                val.push_back(v);
            }

            ptr.push_back(col.size());

            precondition(
                    ptr.size() == n + 1 && col.size() == nnz,
                    format_error("inconsistent data"));

            return boost::make_tuple(n, m);
        }

        /// Read dense array from the file.
        template <typename Val>
        boost::tuple<size_t, size_t> operator()(std::vector<Val> &val) {
            precondition(!_sparse, format_error("not a dense array"));
            precondition(boost::is_complex<Val>::value == _complex,
                    _complex ?
                        "attempt to read complex values into real vector" :
                        "attempt to read real values into complex vector"
                        );
            precondition(boost::is_integral<Val>::value == _integer,
                    _integer ?
                        "attempt to read integer values into real vector" :
                        "attempt to read real values into integer vector"
                        );

            // Read sizes
            size_t n, m;
            std::string line;
            {
                precondition(std::getline(f, line), format_error("unexpected eof"));
                std::istringstream is(line);
                precondition(is >> n >> m, format_error());
            }

            val.resize(n * m);

            for(size_t j = 0; j < m; ++j) {
                for(size_t i = 0; i < n; ++i) {
                    precondition(std::getline(f, line), format_error("unexpected eof"));
                    std::istringstream is(line);
                    val[i * m + j] = read_value<Val>(is);
                }
            }

            return boost::make_tuple(n, m);
        }
    private:
        std::string   fname;
        std::ifstream f;

        bool _sparse;
        bool _complex;
        bool _integer;

        std::string format_error(const std::string &msg = "") const {
            std::string err_string = "MatrixMarket format error in \"" + fname + "\"";
            if (!msg.empty())
                err_string += " (" + msg + ")";
            return err_string;
        }

        template <typename T>
        typename boost::enable_if<typename boost::is_complex<T>::type, T>::type
        read_value(std::istream &s) {
            typename math::scalar_of<T>::type x,y;
            precondition(s >> x >> y, format_error());
            return Val(x,y);
        }

        template <typename T>
        typename boost::disable_if<typename boost::is_complex<T>::type, T>::type
        read_value(std::istream &s) {
            T x;
            precondition(s >> x, format_error());
            return x;
        }
};

namespace detail {
template <typename Val>
typename boost::enable_if<typename boost::is_complex<Val>::type, std::ostream&>::type
write_value(std::ostream &s, Val v) {
    return s << std::real(v) << " " << std::imag(v);
}

template <typename Val>
typename boost::disable_if<typename boost::is_complex<Val>::type, std::ostream&>::type
write_value(std::ostream &s, Val v) {
    return s << v;
}

} // namespace detail

/// Write dense array in Matrix Market format.
template <typename Val>
void mm_write(
        const std::string &fname,
        const Val *data,
        size_t rows,
        size_t cols = 1
        )
{
    std::ofstream f(fname.c_str());
    precondition(f, "Failed to open file \"" + fname + "\" for writing");

    // Banner
    f << "%%MatrixMarket matrix array ";
    if (boost::is_complex<Val>::value) {
        f << "complex ";
    } else if(boost::is_integral<Val>::value) {
        f << "integer ";
    } else {
        f << "real ";
    }
    f << "general\n";

    // Sizes
    f << rows << " " << cols << "\n";

    // Data
    for(size_t j = 0; j < cols; ++j) {
        for(size_t i = 0; i < rows; ++i) {
            detail::write_value(f, data[i * cols + j]) << "\n";
        }
    }
}

} // namespace io
} // namespace amgcl


#endif