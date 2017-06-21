/*
 * uppermatrix.cc
 *
 *  Created on: 03.06.2010
 *      Author: Martin
 */
#include "uppermatrix.h"
#include <iostream>

void test_uppermatrix(void)
{
	uppermatrix<int> m(3);
	m.set(0,1,5);
	m.set(0,2,10);
	m.set(1,2,4);

	bool err=false;
	if(m.get(0,1)!=5)err=true;
	if(m.get(0,2)!=10)err=true;
	if(m.get(1,2)!=4)err=true;
	if(m.get(1,0)!=5)err=true;
	if(m.get(2,0)!=10)err=true;
	if(m.get(2,1)!=4)err=true;

	if(err==true)
		std::cout << "uppermatrix test failed!" << std::endl;
}
