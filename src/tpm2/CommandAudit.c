/********************************************************************************/
/*										*/
/*			     				*/
/*			     Written by Ken Goldman				*/
/*		       IBM Thomas J. Watson Research Center			*/
/*            $Id: CommandAudit.c 55 2015-02-05 22:03:16Z kgoldman $		*/
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

// CommandAudit.c
// 8.1.1	Introduction
// This file contains the functions that support command audit.
// 8.1.2	Includes

#include "InternalRoutines.h"

// 8.1.3	Functions
// 8.1.3.1	CommandAuditPreInstall_Init()

// This function initializes the command audit list. This function is simulates the behavior of
// manufacturing. A function is used instead of a structure definition because this is easier than
// figuring out the ini tialization value for a bit array.

// This function would not be implemented outside of a manufacturing or simulation environment.

void
CommandAuditPreInstall_Init(
			    void
			    )
{
    // Clear all the audit commands
    MemorySet(gp.auditCommands, 0x00, sizeof(gp.auditCommands));
	
    // TPM_CC_SetCommandCodeAuditStatus always being audited
    CommandAuditSet(TPM_CC_SetCommandCodeAuditStatus);
	
    // Set initial command audit hash algorithm to be context integrity hash
    // algorithm
    gp.auditHashAlg = CONTEXT_INTEGRITY_HASH_ALG;
	
    // Set up audit counter to be 0
    gp.auditCounter = 0;
	
    // Write command audit persistent data to NV
    NvWriteReserved(NV_AUDIT_COMMANDS, &gp.auditCommands);
    NvWriteReserved(NV_AUDIT_HASH_ALG, &gp.auditHashAlg);
    NvWriteReserved(NV_AUDIT_COUNTER, &gp.auditCounter);
	
    return;
}

// 8.1.3.2	CommandAuditStartup()
// This function clears the command audit digest on a TPM Reset.

void
CommandAuditStartup(
		    STARTUP_TYPE     type           // IN: start up type
		    )
{
    if(type == SU_RESET)
	{
	    // Reset the digest size to initialize the digest
	    gr.commandAuditDigest.t.size = 0;
	}
    
}

// 8.1.3.3	CommandAuditSet()

// This function will SET the audit flag for a command. This function will not SET the audit flag
// for a command that is not implemented. This ensures that the audit status is not SET when
// TPM2_GetCapability() is used to read the list of audited commands.

// This function is only used by TPM2_SetCommandCodeAuditStatus().

// The actions in TPM2_SetCommandCodeAuditStatus() are expected to cause the changes to be saved to
// NV after it is setting and clearing bits.

// Return Value	Meaning
// TRUE	the command code audit status was changed
// FALSE	the command code audit status was not changed

BOOL
CommandAuditSet(
		TPM_CC           commandCode    // IN: command code
		)
{
    COMMAND_INDEX        commandIndex = CommandCodeToCommandIndex(commandCode);
    
    // Only SET a bit if the corresponding command is implemented
    if(commandIndex != UNIMPLEMENTED_COMMAND_INDEX)
	{
	    // Can't audit shutdown
	    if(commandCode != TPM_CC_Shutdown)
		{
		    if(!BitIsSet(commandIndex, gp.auditCommands, sizeof(gp.auditCommands)))
			{
			    // Set bit
			    BitSet(commandIndex, gp.auditCommands, sizeof(gp.auditCommands));
			    return TRUE;
			}
		}
	}
    // No change
    return FALSE;
}

// 8.1.3.4	CommandAuditClear()
// This function will CLEAR the audit flag for a command. It will not CLEAR the audit flag for TPM_CC_SetCommandCodeAuditStatus().
// This function is only used by TPM2_SetCommandCodeAuditStatus().
// The actions in TPM2_SetCommandCodeAuditStatus() are expected to cause the changes to be saved to NV after it is setting and clearing bits.
// Return Value	Meaning
// TRUE	the command code audit status was changed
// FALSE	the command code audit status was not changed

BOOL
CommandAuditClear(
		  TPM_CC           commandCode    // IN: command code
		  )
{
    COMMAND_INDEX       commandIndex = CommandCodeToCommandIndex(commandCode);
    
    // Do nothing if the command is not implemented
    if(commandIndex != UNIMPLEMENTED_COMMAND_INDEX)
	{
	    // The bit associated with TPM_CC_SetCommandCodeAuditStatus() cannot be
	    // cleared
	    if(commandCode != TPM_CC_SetCommandCodeAuditStatus)
		{
		    if(BitIsSet(commandIndex, gp.auditCommands, sizeof(gp.auditCommands)))
			{
			    // Clear bit
			    BitClear(commandIndex, gp.auditCommands, sizeof(gp.auditCommands));
			    return TRUE;
			}
		}
	}
    // No change
    return FALSE;
}

// 8.1.3.5	CommandAuditIsRequired()
// This function indicates if the audit flag is SET for a command.
// Return Value	Meaning
// TRUE	if command is audited
// FALSE	if command is not audited

BOOL
CommandAuditIsRequired(
		       COMMAND_INDEX    commandIndex   // IN: command index
		       )
{
    // Check the bit map.  If the bit is SET, command audit is required
    return(BitIsSet(commandIndex, gp.auditCommands, sizeof(gp.auditCommands)));
}

// 8.1.3.6	CommandAuditCapGetCCList()
// This function returns a list of commands that have their audit bit SET.
// The list starts at the input commandCode.
// Return Value	Meaning
// YES	if there are more command code available
// NO	all the available command code has been returned

TPMI_YES_NO
CommandAuditCapGetCCList(
			 TPM_CC           commandCode,   // IN: start command code
			 UINT32           count,         // IN: count of returned TPM_CC
			 TPML_CC         *commandList    // OUT: list of TPM_CC
			 )
{
    TPMI_YES_NO     more = NO;
    COMMAND_INDEX   commandIndex;
    
    // Initialize output handle list
    commandList->count = 0;
    
    // The maximum count of command we may return is MAX_CAP_CC
    if(count > MAX_CAP_CC) count = MAX_CAP_CC;
    
    // Find the implemented command that has a command code that is the same or
    // higher than the input
    // Collect audit commands
    for(commandIndex = GetClosestCommandIndex(commandCode);
	commandIndex != UNIMPLEMENTED_COMMAND_INDEX;
	commandIndex = GetNextCommandIndex(commandIndex))
	{
	    if(CommandAuditIsRequired(commandIndex))
		{
		    if(commandList->count < count)
			{
			    // If we have not filled up the return list, add this command
			    // code to its
			    TPM_CC      cc = s_ccAttr[commandIndex].commandIndex;
			    if(s_ccAttr[commandIndex].V)
				cc += (1 << 29);
			    commandList->commandCodes[commandList->count] = cc;
			    commandList->count++;
			}
		    else
			{
			    // If the return list is full but we still have command
			    // available, report this and stop iterating
			    more = YES;
			    break;
			}
		}
	}
    
    return more;
    
}

// 8.1.3.7	CommandAuditGetDigest

// This command is used to create a digest of the commands being audited. The commands are processed
// in ascending numeric order with a list of TPM_CC being added to a hash. This operates as if all
// the audited command cod es were concatenated and then hashed.

void
CommandAuditGetDigest(
		      TPM2B_DIGEST    *digest         // OUT: command digest
		      )
{
    TPM_CC                       commandCode;
    COMMAND_INDEX                commandIndex;
    HASH_STATE                   hashState;
    
    // Start hash
    digest->t.size = CryptStartHash(gp.auditHashAlg, &hashState);
    
    // Add command code
    for(commandIndex = 0; commandIndex < COMMAND_COUNT; commandIndex++)
	{
	    if(CommandAuditIsRequired(commandIndex))
		{
		    commandCode = GetCommandCode(commandIndex);
		    CryptUpdateDigestInt(&hashState, sizeof(commandCode), &commandCode);
		}
	}
    
    // Complete hash
    CryptCompleteHash2B(&hashState, &digest->b);
    
    return;
}
