//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with this program.  If not, see http://www.gnu.org/licenses/.
//

#ifndef SYMMATRIX_H_
#define SYMMATRIX_H_

#include <vector>
#include <stdexcept>

/**
 * Class for storing symmetric matrices efficiently
 *
 * @author Martin Raiber
 */
template<class T>
class symmatrix {
public:
	symmatrix(void)
	{
		n=0;
	}

	symmatrix(unsigned int n) {
		setn(n);
	}

	T& get(unsigned int r, unsigned int c) {
		unsigned int i=idx(r, c);
		if(i<s.size())
		{
			return s[i];
		}
		else
		{
			throw std::out_of_range("symmatrix out of range");
		}
	}

	void set(unsigned int r, unsigned int c, T d) {
		unsigned int i=idx(r, c);
		if(i<s.size())
		{
			s[i] = d;
		}
		else
		{
			throw std::out_of_range("symmatrix out of range");
		}
	}

	void setn(unsigned int pN)
	{
		n=pN;
		s.resize(idx(pN, pN));
	}
private:
	unsigned int idx(unsigned int r, unsigned int c) {
		if(r>c)
		{
			int t=r;
			r=c;
			c=t;
		}
		return (r * (2 * n + 1) - r * r) / 2 + c;
	}

	std::vector<T> s;
	unsigned int n;
};

#endif /* SYMMATRIX_H_ */
