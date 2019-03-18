//
// Copyright(C) 1993-1996 Id Software, Inc.
// Copyright(C) 2005-2014 Simon Howard
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// DESCRIPTION:
//	Fixed point implementation.
//



#include "stdlib.h"

#include "doomtype.h"
#include "i_system.h"

#include "m_fixed.h"




// Fixme. __USE_C_FIXED__ or something.

fixed_t
FixedMul
( fixed_t	a,
  fixed_t	b )
{
    return ((int64_t) a * (int64_t) b) >> FRACBITS;
}



//
// FixedDiv, C version.
//

fixed_t FixedDiv(fixed_t a, fixed_t b)
{
    if ((abs(a) >> 14) >= abs(b))
    {
	return (a^b) < 0 ? INT_MIN : INT_MAX;
    }
    else
    {
	int64_t result;

	result = ((int64_t) a << FRACBITS) / b;

	return (fixed_t) result;
    }
}

/* CPhipps -
 * FixedMod - returns a % b, guaranteeing 0<=a<b
 * (notice that the C standard for % does not guarantee this)
 */

fixed_t FixedMod(fixed_t a, fixed_t b)
{
  if (b & (b-1)) {
    fixed_t r = a % b;
    return ((r<0) ? r+b : r);
  } else
    return (a & (b-1));
}

fixed_t Scale(fixed_t a, fixed_t b, fixed_t c)
{
	return (fixed_t)(((int64_t)a*b)/c);
}