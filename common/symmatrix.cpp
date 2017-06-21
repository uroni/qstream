/*
 * symmatrix.cpp
 *
 *  Created on: 03.06.2010
 *      Author: Martin
 */

#include "symmatrix.h"
#include <iostream>

void test_symmatrix(void)
{
	symmatrix<int> m(3);
	m.set(0,0,1);
	m.set(0,1,5);
	m.set(0,2,10);
	m.set(1,1,3);
	m.set(1,2,4);
	m.set(2,2,1);

	bool err=false;
	if(m.get(0,0)!=1)err=true;
	if(m.get(0,1)!=5)err=true;
	if(m.get(0,2)!=10)err=true;
	if(m.get(1,1)!=3)err=true;
	if(m.get(1,2)!=4)err=true;
	if(m.get(2,2)!=1)err=true;
	if(m.get(1,0)!=5)err=true;
	if(m.get(2,0)!=10)err=true;
	if(m.get(2,1)!=4)err=true;

	if(err==true)
		std::cout << "symmatrix test failed!" << std::endl;
}
