/*
 * uppermatrix.h
 *
 *  Created on: 03.06.2010
 *      Author: Martin
 */

#ifndef UPPERMATRIX_H_
#define UPPERMATRIX_H_

#include "symmatrix.h"

/**
 * Class for storing upper matrices efficiently (symmetric matrices where the diagonal is missing)
 */
template<class T>
class uppermatrix {
public:
	uppermatrix(void){}
	uppermatrix(unsigned int pN) : n(pN)
	{
		m.setn(pN-1);
	}

	void setn(unsigned int pN)
	{
		n=pN;
		m.setn(pN-1);
	}

	T& get(unsigned int r, unsigned int c)
	{
		if(r!=c)
		{
			if(r>c)
			{
				int t=r;
				r=c;
				c=t;
			}

			return m.get(r,c-1);
		}
		else
		{
			return zero;
		}
	}

	void set(unsigned int r, unsigned int c, T d)
	{
		if(r!=c)
		{
			if(r>c)
			{
				int t=r;
				r=c;
				c=t;
			}

			m.set(r,c-1,d);
		}
	}

private:
	symmatrix<T> m;
	unsigned int n;
	T zero;
};

#endif /* UPPERMATRIX_H_ */
