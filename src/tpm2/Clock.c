/********************************************************************************/
/*										*/
/*			     				*/
/*			     Written by Ken Goldman				*/
/*		       IBM Thomas J. Watson Research Center			*/
/*            $Id: Clock.c 221 2015-04-13 21:19:40Z kgoldman $			*/
/*										*/
/*  Licenses and Notices							*/
/*										*/
/*  1. Copyright Licenses:							*/
/*										*/
/*  - Trusted Computing Group (TCG) grants to the user of the source code in	*/
/*    this specification (the "Source Code") a worldwide, irrevocable, 		*/
/*    nonexclusive, royalty free, copyright license to reproduce, create 	*/
/*    derivative works, distribute, display and perform the Source Code and	*/
/*    derivative works thereof, and to grant others the rights granted herein.	*/
/*										*/
/*  - The TCG grants to the user of the other parts of the specification 	*/
/*    (other than the Source Code) the rights to reproduce, distribute, 	*/
/*    display, and perform the specification solely for the purpose of 		*/
/*    developing products based on such documents.				*/
/*										*/
/*  2. Source Code Distribution Conditions:					*/
/*										*/
/*  - Redistributions of Source Code must retain the above copyright licenses, 	*/
/*    this list of conditions and the following disclaimers.			*/
/*										*/
/*  - Redistributions in binary form must reproduce the above copyright 	*/
/*    licenses, this list of conditions	and the following disclaimers in the 	*/
/*    documentation and/or other materials provided with the distribution.	*/
/*										*/
/*  3. Disclaimers:								*/
/*										*/
/*  - THE COPYRIGHT LICENSES SET FORTH ABOVE DO NOT REPRESENT ANY FORM OF	*/
/*  LICENSE OR WAIVER, EXPRESS OR IMPLIED, BY ESTOPPEL OR OTHERWISE, WITH	*/
/*  RESPECT TO PATENT RIGHTS HELD BY TCG MEMBERS (OR OTHER THIRD PARTIES)	*/
/*  THAT MAY BE NECESSARY TO IMPLEMENT THIS SPECIFICATION OR OTHERWISE.		*/
/*  Contact TCG Administration (admin@trustedcomputinggroup.org) for 		*/
/*  information on specification licensing rights available through TCG 	*/
/*  membership agreements.							*/
/*										*/
/*  - THIS SPECIFICATION IS PROVIDED "AS IS" WITH NO EXPRESS OR IMPLIED 	*/
/*    WARRANTIES WHATSOEVER, INCLUDING ANY WARRANTY OF MERCHANTABILITY OR 	*/
/*    FITNESS FOR A PARTICULAR PURPOSE, ACCURACY, COMPLETENESS, OR 		*/
/*    NONINFRINGEMENT OF INTELLECTUAL PROPERTY RIGHTS, OR ANY WARRANTY 		*/
/*    OTHERWISE ARISING OUT OF ANY PROPOSAL, SPECIFICATION OR SAMPLE.		*/
/*										*/
/*  - Without limitation, TCG and its members and licensors disclaim all 	*/
/*    liability, including liability for infringement of any proprietary 	*/
/*    rights, relating to use of information in this specification and to the	*/
/*    implementation of this specification, and TCG disclaims all liability for	*/
/*    cost of procurement of substitute goods or services, lost profits, loss 	*/
/*    of use, loss of data or any incidental, consequential, direct, indirect, 	*/
/*    or special damages, whether under contract, tort, warranty or otherwise, 	*/
/*    arising in any way out of use or reliance upon this specification or any 	*/
/*    information herein.							*/
/*										*/
/*  (c) Copyright IBM Corp. and others, 2012-2015				*/
/*										*/
/********************************************************************************/

/* rev 119 */

#include <stdint.h>
#include <time.h>

/* added for portability because Linux clock is 32 bits */

static uint64_t tpmclock(void);

#ifdef TPM_WINDOWS

static uint64_t tpmclock(void)
{
    return clock();
}

#endif

#ifdef TPM_POSIX

/* return time in milliseconds */

#include <sys/time.h>

static uint64_t tpmclock(void)
{
    struct timeval      tval;
    uint64_t		tpmtime;

    gettimeofday(&tval, NULL);   /* get the time */
    tpmtime = (tval.tv_sec * 1000) + (tval.tv_usec / 1000);
    return tpmtime;
}

#endif

// C.3	Clock.c
// C.3.1.	Introduction

// This file contains the routines that are used by the simulator to mimic a hardware clock on a
// TPM. In this implementation, all the time values are measured in millisecond. However, the precision
// of the clock functions may be implementation dependent.

// C.3.2.	Includes and Data Definitions

#include <time.h>
#include "PlatformData.h"
#include "Platform.h"

// C.3.3.	Functions
// C.3.3.1.	_plat__ClockReset()
// Set the current clock time as initial time.  This function is called at a power on event to reset the clock

LIB_EXPORT void
_plat__ClockReset(
		  void
		  )
{
    // Implementation specific: Microsoft C set CLOCKS_PER_SEC to be 1/1000,
    // so here the measurement of clock() is in millisecond.
    s_initClock = tpmclock();
    s_adjustRate = CLOCK_NOMINAL;
    
    return;
}

// C.3.3.2.	_plat__ClockTimeFromStart()
// Function returns the compensated time from the start of the command when _plat__ClockTimeFromStart() was called.

unsigned long long
_plat__ClockTimeFromStart(
			  void
			  )
{
    unsigned long long  currentClock = tpmclock();
    return ((currentClock - s_initClock) * CLOCK_NOMINAL) / s_adjustRate;
}

// C.3.3.3.	_plat__ClockTimeElapsed()
// Get the time elapsed from current to the last time the _plat__ClockTimeElapsed() is called.  For the first _plat__ClockTimeElapsed() call after a power on event, this call report the elapsed time from power on to the current call

LIB_EXPORT unsigned long long
_plat__ClockTimeElapsed(
			void
			)
{
    unsigned long long  elapsed;
    unsigned long long  currentClock = tpmclock();
    elapsed = ((currentClock - s_initClock) * CLOCK_NOMINAL) / s_adjustRate;
    s_initClock += (elapsed * s_adjustRate) / CLOCK_NOMINAL;
    
#ifdef  DEBUGGING_TIME
    // Put this in so that TPM time will pass much faster than real time when
    // doing debug.
    // A value of 1000 for DEBUG_TIME_MULTIPLER will make each ms into a second
    // A good value might be 100
    elapsed *= DEBUG_TIME_MULTIPLIER
#endif
	       return elapsed;
}

// C.3.3.4.	_plat__ClockAdjustRate()
// Adjust the clock rate

LIB_EXPORT void
_plat__ClockAdjustRate(
		       int              adjust         // IN: the adjust number.  It could be positive
		       //     or negative
		       )
{
    // We expect the caller should only use a fixed set of constant values to
    // adjust the rate
    switch(adjust)
	{
	  case CLOCK_ADJUST_COARSE:
	    s_adjustRate += CLOCK_ADJUST_COARSE;
	    break;
	  case -CLOCK_ADJUST_COARSE:
	    s_adjustRate -= CLOCK_ADJUST_COARSE;
	    break;
	  case CLOCK_ADJUST_MEDIUM:
	    s_adjustRate += CLOCK_ADJUST_MEDIUM;
	    break;
	  case -CLOCK_ADJUST_MEDIUM:
	    s_adjustRate -= CLOCK_ADJUST_MEDIUM;
	    break;
	  case CLOCK_ADJUST_FINE:
	    s_adjustRate += CLOCK_ADJUST_FINE;
	    break;
	  case -CLOCK_ADJUST_FINE:
	    s_adjustRate -= CLOCK_ADJUST_FINE;
	    break;
	  default:
	    // ignore any other values;
	    break;
	}
    
    if(s_adjustRate > (CLOCK_NOMINAL + CLOCK_ADJUST_LIMIT))
	s_adjustRate = CLOCK_NOMINAL + CLOCK_ADJUST_LIMIT;
    if(s_adjustRate < (CLOCK_NOMINAL - CLOCK_ADJUST_LIMIT))
	s_adjustRate = CLOCK_NOMINAL-CLOCK_ADJUST_LIMIT;
    
    return;
}

