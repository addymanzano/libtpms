/********************************************************************************/
/*										*/
/*			     				*/
/*			     Written by Ken Goldman				*/
/*		       IBM Thomas J. Watson Research Center			*/
/*            $Id: PowerPlat.c 55 2015-02-05 22:03:16Z kgoldman $               */
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

// C.7	PowerPlat.c
// C.7.1.	Includes and Function Prototypes

#include    "PlatformData.h"
#include    "Platform.h"

// C.7.2.	Functions
// C.7.2.1.	_plat__Signal_PowerOn()
// Signal platform power on

LIB_EXPORT int
_plat__Signal_PowerOn(
		      void
		      )
{
    // Start clock
    _plat__ClockReset();
    
    // Initialize locality
    s_locality = 0;
    
    // Command cancel
    s_isCanceled = FALSE;
    
    // Need to indicate that we lost power
    s_powerLost = TRUE;
    
    return 0;
}

// C.7.2.2.	_plat__WasPowerLost()
// Test whether power was lost before a _TPM_Init()

LIB_EXPORT BOOL
_plat__WasPowerLost(
		    BOOL             clear
		    )
{
    BOOL        retVal = s_powerLost;
    if(clear)
	s_powerLost = FALSE;
    return retVal;
}

// C.7.2.3.	_plat_Signal_Reset()
// This a TPM reset without a power loss.

LIB_EXPORT int
_plat__Signal_Reset(
		    void
		    )
{
    // Need to reset the clock
    _plat__ClockReset();
    
    // if we are doing reset but did not have a power failure, then we should
    // not need to reload NV ...
    return 0;
}

// C.7.2.4.	_plat__Signal_PowerOff()
// Signal platform power off

LIB_EXPORT void
_plat__Signal_PowerOff(
		       void
		       )
{
    // Prepare NV memory for power off
    _plat__NVDisable();
    
    return;
}