/* /////////////////////////////////////////////////////////////////////////////
 * File:    atomic.c
 *
 * Purpose: Implementation of Linux like atomic functions for the
 *          Win32 platform.
 *
 * Created: 21st November 2003
 * Updated: 12th September 2006
 *
 * Home:    http://synesis.com.au/software/
 *
 * Copyright (c) 2003-2006, Matthew Wilson and Synesis Software
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without 
 * modification, are permitted provided that the following conditions are met:
 *
 * - Redistributions of source code must retain the above copyright notice, this
 *   list of conditions and the following disclaimer. 
 * - Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 * - Neither the names of Matthew Wilson and Synesis Software nor the names of
 *   any contributors may be used to endorse or promote products derived from
 *   this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * ////////////////////////////////////////////////////////////////////////// */


#ifndef UNIXEM_DOCUMENTATION_SKIP_SECTION
# define _SYNSOFT_VER_C_ATOMIC_MAJOR    2
# define _SYNSOFT_VER_C_ATOMIC_MINOR    0
# define _SYNSOFT_VER_C_ATOMIC_REVISION 3
# define _SYNSOFT_VER_C_ATOMIC_EDIT     16
#endif /* !UNIXEM_DOCUMENTATION_SKIP_SECTION */

/* /////////////////////////////////////////////////////////////////////////////
 * Includes
 */

#include <asm/atomic.h>

#include <unixem/unixem.h>

#include <errno.h>
#include <windows.h>

/* ////////////////////////////////////////////////////////////////////////// */

#ifndef WIN32
# error This file is only currently defined for Win32 compilation units
#endif /* WIN32 */

/* /////////////////////////////////////////////////////////////////////////////
 * Constants and definitions
 */

/* /////////////////////////////////////////////////////////////////////////////
 * API functions
 */

void atomic_set(atomic_t volatile *v, int i)
{
    (void)InterlockedExchange((LPLONG)v, i);
}

int atomic_read(atomic_t volatile *v)
{
    return *v;
}

void atomic_add(int i, atomic_t volatile *v)
{
    (void)InterlockedExchangeAdd((LPLONG)v, i);
}

void atomic_sub(int i, atomic_t volatile *v)
{
    (void)InterlockedExchangeAdd((LPLONG)v, -i);
}

void atomic_inc(atomic_t volatile *v)
{
    (void)InterlockedIncrement((LPLONG)v);
}

void atomic_dec(atomic_t volatile *v)
{
    (void)InterlockedDecrement((LPLONG)v);
}

/** Returns the previous value */
int atomic_inc_and_test(atomic_t volatile *v)
{
    return -1 + InterlockedIncrement((LPLONG)v);
}

/** Returns the previous value */
int atomic_dec_and_test(atomic_t volatile *v)
{
    return 1 + InterlockedDecrement((LPLONG)v);
}

/** Returns the previous value */
int atomic_add_and_test(int i, atomic_t volatile *v);
/** Returns the previous value */
int atomic_sub_and_test(int i, atomic_t volatile *v);


/* ////////////////////////////////////////////////////////////////////////// */
