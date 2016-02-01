/********************************************************************************/
/*										*/
/*			     				*/
/*			     Written by Ken Goldman				*/
/*		       IBM Thomas J. Watson Research Center			*/
/*            $Id: SessionProcess.c 471 2015-12-22 19:40:24Z kgoldman $		*/
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

// rev 124

// 6.4	SessionProcess.c
// 6.4.1	Introduction

// This file contains the subsystem that process the authorization sessions including implementation
// of the Dictionary Attack logic. ExecCommand() uses ParseSessionBuffer() to process the
// authorization session area of a command and BuildResponseSession() to create the authorization
// session area of a response.

// 6.4.2	Includes and Data Definitions

#define SESSION_PROCESS_C
#include "InternalRoutines.h"
#include "SessionProcess_fp.h"
#include "Platform.h"

#include "Unmarshal_fp.h"

// 6.4.3	Authorization Support Functions
// 6.4.3.1	IsDAExempted()
// This function indicates if a handle is exempted from DA logic. A handle is exempted if it is
// a)	a primary seed handle,
// b)	an object with noDA bit SET,
// c)	an NV Index with TPMA_NV_NO_DA bit SET, or
// d)	a PCR handle.
// Return Value	Meaning
// TRUE	handle is exempted from DA logic
// FALSE	handle is not exempted from DA logic

BOOL
IsDAExempted(
	     TPM_HANDLE       handle         // IN: entity handle
	     )
{
    BOOL        result = FALSE;
    
    switch(HandleGetType(handle))
	{
	  case TPM_HT_PERMANENT:
	    // All permanent handles, other than TPM_RH_LOCKOUT, are exempt from
	    // DA protection.
	    result =  (handle != TPM_RH_LOCKOUT);
	    break;
	    
	    // When this function is called, a persistent object will have been loaded
	    // into an object slot and assigned a transient handle.
	  case TPM_HT_TRANSIENT:
	      {
		  OBJECT      *object;
		  object = ObjectGet(handle);
		  result = (object->publicArea.objectAttributes.noDA == SET);
		  break;
	      }
	  case TPM_HT_NV_INDEX:
	      {
		  NV_INDEX        nvIndex;
		  NvGetIndexInfo(handle, &nvIndex);
		  result = (nvIndex.publicArea.attributes.TPMA_NV_NO_DA == SET);
		  break;
	      }
	  case TPM_HT_PCR:
	    // PCRs are always exempted from DA.
	    result = TRUE;
	    break;
	  default:
	    break;
	}
    return result;
}

// 6.4.3.2	IncrementLockout()

// This function is called after an authorization failure that involves use of an authValue. If the
// entity referenced by the handle is not exempt from DA protection, then the failedTries counter
// will be incremented.

// Error Returns	Meaning
// TPM_RC_AUTH_FAIL	authorization failure that caused DA lockout to increment
// TPM_RC_BAD_AUTH	authorization failure did not cause DA lockout to increment

static TPM_RC
IncrementLockout(
		 UINT32           sessionIndex
		 )
{
    TPM_HANDLE       handle = s_associatedHandles[sessionIndex];
    TPM_HANDLE       sessionHandle = s_sessionHandles[sessionIndex];
    TPM_RC           result;
    SESSION         *session = NULL;
    
    // Don't increment lockout unless the handle associated with the session
    // is DA protected or the session is bound to a DA protected entity.
    if(sessionHandle == TPM_RS_PW)
	{
	    if(IsDAExempted(handle))
		return TPM_RC_BAD_AUTH;
	    
	}
    else
	{
	    session = SessionGet(sessionHandle);
	    // If the session is bound to lockout, then use that as the relevant
	    // handle. This means that an auth failure with a bound session
	    // bound to lockoutAuth will take precedence over any other
	    // lockout check
	    if(session->attributes.isLockoutBound == SET)
		handle = TPM_RH_LOCKOUT;
	    
	    if(   session->attributes.isDaBound == CLEAR
		  && (IsDAExempted(handle) || session->attributes.includeAuth == CLEAR)
		  )

		// If the handle was changed to TPM_RH_LOCKOUT, this will not return
		// TPM_RC_BAD_AUTH
		return TPM_RC_BAD_AUTH;
	}
    
    if(handle == TPM_RH_LOCKOUT)
	{
	    pAssert(gp.lockOutAuthEnabled == TRUE);
	    
	    // lockout is no longer enabled
	    gp.lockOutAuthEnabled = FALSE;
	    
	    // For TPM_RH_LOCKOUT, if lockoutRecovery is 0, no need to update NV since
	    // the lockout auth will be reset at startup.
	    if(gp.lockoutRecovery != 0)
		{
		    result = NvIsAvailable();
		    if(result != TPM_RC_SUCCESS)
			{
			    // No NV access for now. Put the TPM in pending mode.
			    s_DAPendingOnNV = TRUE;
			}
		    else
			{
			    // Update NV.
			    NvWriteReserved(NV_LOCKOUT_AUTH_ENABLED, &gp.lockOutAuthEnabled);
			    g_updateNV = TRUE;
			}
		}
	}
    else
	{
	    if(gp.recoveryTime != 0)
		{
		    gp.failedTries++;
		    result = NvIsAvailable();
		    if(result != TPM_RC_SUCCESS)
			{
			    // No NV access for now.  Put the TPM in pending mode.
			    s_DAPendingOnNV = TRUE;
			}
		    else
			{
			    // Record changes to NV.
			    NvWriteReserved(NV_FAILED_TRIES, &gp.failedTries);
			    g_updateNV = TRUE;
			}
		}
	}
    
    // Register a DA failure and reset the timers.
    DARegisterFailure(handle);
    
    return TPM_RC_AUTH_FAIL;
}

// 6.4.3.3	IsSessionBindEntity()

// This function indicates if the entity associated with the handle is the entity, to which this
// session is bound. The binding would occur by making the bind parameter in TPM2_StartAuthSession()
// not equal to TPM_RH_NULL. The binding only occurs if the session is an HMAC session. The bind
// value is a combination of the Name and the authValue of the entity.

// Return Value	Meaning
// TRUE	handle points to the session start entity
// FALSE	handle does not point to the session start entity

static BOOL
IsSessionBindEntity(
			    TPM_HANDLE       associatedHandle,  // IN: handle to be authorized
			    SESSION         *session            // IN: associated session
		    )
{
    TPM2B_NAME     entity;             // The bind value for the entity
	
    // If the session is not bound, return FALSE.
    if(session->attributes.isBound)
	{
	    // Compute the bind value for the entity.
	    SessionComputeBoundEntity(associatedHandle, &entity);
		
	    // Compare to the bind value in the session.
	    return Memory2BEqual(&entity.b, &session->u1.boundEntity.b);
	}
    return FALSE;
}

// 6.4.3.4	IsPolicySessionRequired()

// Checks if a policy session is required for a command. If a command requires DUP or ADMIN role
// authorization, then the handle that requires that role is the first handle in the command. This
// simplifies this checking. If a new command is created that requires multiple ADMIN role
// authorizations, then it will have to be special-cased in this function. A policy session is
// required if:

// a)	the command requires the DUP role,
// b)	the command requires the ADMIN role and the authorized entity is an object and its adminWithPolicy bit is SET, or
// c)	the command requires the ADMIN role and the authorized entity is a permanent handle or an NV Index.
// d)	The authorized entity is a PCR belonging to a policy group, and has its policy initialized
// Return Value	Meaning
// TRUE	policy session is required
// FALSE	policy session is not required

static BOOL
IsPolicySessionRequired(
			COMMAND_INDEX    commandIndex,  // IN: command index
			UINT32           sessionIndex   // IN: session index
			)
{
    AUTH_ROLE       role = CommandAuthRole(commandIndex, sessionIndex);
    TPM_HT          type = HandleGetType(s_associatedHandles[sessionIndex]);
    
    if(role == AUTH_DUP)
	return TRUE;
    
    if(role == AUTH_ADMIN)
	{
	    // We allow an exception for admin role in a transient object. If the object
	    // allows admin role actions with auth, then policy is not requried.
	    // For all other cases, there is no way to override the command requirement
	    // that a policy be used
	    if(type == TPM_HT_TRANSIENT)
		{
		    OBJECT      *object = ObjectGet(s_associatedHandles[sessionIndex]);
		    
		    if(object->publicArea.objectAttributes.adminWithPolicy == CLEAR)
			return FALSE;
		}
	    return TRUE;
	}
    
    if(type == TPM_HT_PCR)
	{
	    if(PCRPolicyIsAvailable(s_associatedHandles[sessionIndex]))
		{
		    TPM2B_DIGEST        policy;
		    TPMI_ALG_HASH       policyAlg;
		    policyAlg = PCRGetAuthPolicy(s_associatedHandles[sessionIndex],
						 &policy);
		    if(policyAlg != TPM_ALG_NULL)
			return TRUE;
		}
	}
    return FALSE;
}

// 6.4.3.5	IsAuthValueAvailable()
// This function indicates if authValue is available and allowed for USER role authorization of an entity.

// This function is similar to IsAuthPolicyAvailable() except that it does not check the size of the
// authValue as IsAuthPolicyAvailable() does (a null authValue is a valid auth, but a null policy is
// not a valid policy).

// This function does not check that the handle reference is valid or if the entity is in an enabled
// hierarchy. Those checks are assumed to have been performed during the handle unmarshaling.

// Return Value	Meaning
// TRUE	authValue is available
// FALSE	authValue is not available

static BOOL
IsAuthValueAvailable(
		     TPM_HANDLE       handle,        // IN: handle of entity
		     COMMAND_INDEX    commandIndex,  // IN: command index
		     UINT32           sessionIndex   // IN: session index
		     )
{
    BOOL             result = FALSE;
   
    switch(HandleGetType(handle))
	{
	  case TPM_HT_PERMANENT:
	    switch(handle)
		{
		    // At this point hierarchy availability has already been
		    // checked so primary seed handles are always available here
		  case TPM_RH_OWNER:
		  case TPM_RH_ENDORSEMENT:
		  case TPM_RH_PLATFORM:
#ifdef VENDOR_PERMANENT
		    // This vendor defined handle associated with the
		    // manufacturer's shared secret
		  case VENDOR_PERMANENT:
#endif
		    // The DA checking has been performed on LockoutAuth but we
		    // bypass the DA logic if we are using lockout policy. The
		    // policy would allow execution to continue and lockoutAuth
		    // could be used, even if direct use of lockoutAuth is disabled
		  case TPM_RH_LOCKOUT:

		    // NullAuth is always available.
		  case TPM_RH_NULL:
		    
		    result = TRUE;
		    break;
		  default:
		    // Otherwise authValue is not available.
		    break;
		}
	    break;
	  case TPM_HT_TRANSIENT:
	    // A persistent object has already been loaded and the internal
	    // handle changed.
	      {
		  OBJECT          *object;
		  object = ObjectGet(handle);
		  
		  // authValue is always available for a sequence object.
		  // An alternative for this is to SET
		  // object->publicArea.objectAttributes.userWithAuth when the
		  // sequence is started.
		  if(ObjectIsSequence(object))
		      {
			  result =  TRUE;
			  break;
		      }
		  // authValue is available for an object if it has its sensitive
		  // portion loaded and
		  //  1. userWithAuth bit is SET, or
		  //  2. ADMIN role is required
		  if(   object->attributes.publicOnly == CLEAR
			&&    (object->publicArea.objectAttributes.userWithAuth == SET
			       || (CommandAuthRole(commandIndex, sessionIndex) == AUTH_ADMIN
				   &&  object->publicArea.objectAttributes.adminWithPolicy
				   == CLEAR)))
		      result = TRUE;
	      }
	      break;
	  case TPM_HT_NV_INDEX:
	    // NV Index.
	      {
		  NV_INDEX        nvIndex;
		  NvGetIndexInfo(handle, &nvIndex);
		  if(IsWriteOperation(commandIndex))
		      {
			  if (nvIndex.publicArea.attributes.TPMA_NV_AUTHWRITE == SET)
			      result = TRUE;
			  
		      }
		  else
		      {
			  if (nvIndex.publicArea.attributes.TPMA_NV_AUTHREAD == SET)
			      result = TRUE;
		      }
	      }
	      break;
	  case TPM_HT_PCR:
	    // PCR handle.
	    // authValue is always allowed for PCR
	    result =  TRUE;
	    break;
	  default:
	    // Otherwise, authValue is not available
	    break;
	}
    return result;
}

// 6.4.3.6	IsAuthPolicyAvailable()
// This function indicates if an authPolicy is available and allowed.

// This function does not check that the handle reference is valid or if the entity is in an enabled
// hierarchy. Those checks are assumed to have been performed during the handle unmarshaling.

// Return Value	Meaning
// TRUE	authPolicy is available
// FALSE	authPolicy is not available

static BOOL
IsAuthPolicyAvailable(
		      TPM_HANDLE       handle,        // IN: handle of entity
		      COMMAND_INDEX    commandIndex,  // IN: command index
		      UINT32           sessionIndex   // IN: session index
		      )
{
    BOOL            result = FALSE;
    switch(HandleGetType(handle))
	{
	  case TPM_HT_PERMANENT:
	    switch(handle)
		{
		    // At this point hierarchy availability has already been checked.
		  case TPM_RH_OWNER:
		    if (gp.ownerPolicy.t.size != 0)
			result = TRUE;
		    break;
		    
		  case TPM_RH_ENDORSEMENT:
		    if (gp.endorsementPolicy.t.size != 0)
			result = TRUE;
		    break;
		    
		  case TPM_RH_PLATFORM:
		    if (gc.platformPolicy.t.size != 0)
			result = TRUE;
		    break;
		  case TPM_RH_LOCKOUT:
		    if(gp.lockoutPolicy.t.size != 0)
			result = TRUE;
		    break;
		  default:
		    break;
		}
	    break;
	  case TPM_HT_TRANSIENT:
	      {
		  // Object handle.
		  // An evict object would already have been loaded and given a
		  // transient object handle by this point.
		  OBJECT  *object = ObjectGet(handle);
		  // Policy authorization is not available for an object with only
		  // public portion loaded.
		  if(object->attributes.publicOnly == CLEAR)
		      {
			  // Policy authorization is always available for an object but
			  // is never available for a sequence.
			  if(!ObjectIsSequence(object))
			      result = TRUE;
		      }
		  break;
	      }
	  case TPM_HT_NV_INDEX:
	    // An NV Index.
	      {
		  NV_INDEX         nvIndex;
		  NvGetIndexInfo(handle, &nvIndex);
		  // If the policy size is not zero, check if policy can be used.
		  if(nvIndex.publicArea.authPolicy.t.size != 0)
		      {
			  // If policy session is required for this handle, always
			  // uses policy regardless of the attributes bit setting
			  if(IsPolicySessionRequired(commandIndex, sessionIndex))
			      result = TRUE;
			  // Otherwise, the presence of the policy depends on the NV
			  // attributes.
			  else if(IsWriteOperation(commandIndex))
			      {
				  if (   nvIndex.publicArea.attributes.TPMA_NV_POLICYWRITE
					 == SET)
				      result = TRUE;
			      }
			  else
			      {
				  if (    nvIndex.publicArea.attributes.TPMA_NV_POLICYREAD
					  ==  SET)
				      result = TRUE;
			      }
		      }
	      }
	      break;
	  case TPM_HT_PCR:
	    // PCR handle.
	    if(PCRPolicyIsAvailable(handle))
		result = TRUE;
	    break;
	  default:
	    break;
	}
    return result;
}
// 6.4.4	Session Parsing Functions
// 6.4.4.1	ComputeCpHash()
// This function computes the cpHash as defined in Part 2 and described in Part 1.

static void
ComputeCpHash(
	      TPMI_ALG_HASH    hashAlg,           // IN: hash algorithm
	      COMMAND_INDEX    commandIndex,      // IN: command index
	      UINT32           handleNum,         // IN: number of handles
	      TPM_HANDLE       handles[],         // IN: array of handles
	      UINT32           parmBufferSize,    // IN: size of input parameter area
	      BYTE            *parmBuffer,        // IN: input parameter area
	      TPM2B_DIGEST    *cpHash,            // OUT: cpHash
	      TPM2B_DIGEST    *nameHash           // OUT: name hash of command
	      )
{
    UINT32           i;
    HASH_STATE       hashState;
    TPM2B_NAME       name;
    TPM_CC           commandCode = GetCommandCode(commandIndex);
    
    // cpHash = hash(commandCode [ || authName1
    //                           [ || authName2
    //                           [ || authName 3 ]]]
    //                           [ || parameters])
    // A cpHash can contain just a commandCode only if the lone session is
    // an audit session.
    
    // Start cpHash.
    cpHash->t.size = CryptStartHash(hashAlg, &hashState);
    
    //  Add commandCode.
    CryptUpdateDigestInt(&hashState, sizeof(TPM_CC), &commandCode);
    
    //  Add authNames for each of the handles.
    for(i = 0; i < handleNum; i++)
	{
	    name.t.size = EntityGetName(handles[i], &name.t.name);
	    CryptUpdateDigest2B(&hashState, &name.b);
	}
    
    //  Add the parameters.
    CryptUpdateDigest(&hashState, parmBufferSize, parmBuffer);
    
    //  Complete the hash.
    CryptCompleteHash2B(&hashState, &cpHash->b);
    
    // If the nameHash is needed, compute it here.
    if(nameHash != NULL)
	{
	    // Start name hash. hashState may be reused.
	    nameHash->t.size = CryptStartHash(hashAlg, &hashState);
	    
	    //  Adding names.
	    for(i = 0; i < handleNum; i++)
		{
		    name.t.size = EntityGetName(handles[i], &name.t.name);
		    CryptUpdateDigest2B(&hashState, &name.b);
		}
	    //  Complete hash.
	    CryptCompleteHash2B(&hashState, &nameHash->b);
	}
    return;
}

// 6.4.4.2	CheckPWAuthSession()

// This function validates the authorization provided in a PWAP session.  It compares the input
// value to authValue of the authorized entity. Argument sessionIndex is used to get handles handle
// of the referenced entities from s_inputAuthValues[] and s_associatedHandles[].

// Error Returns	Meaning
// TPM_RC_AUTH_FAIL	auth fails and increments DA failure count
// TPM_RC_BAD_AUTH	auth fails but DA does not apply

static TPM_RC
CheckPWAuthSession(
		   UINT32           sessionIndex   // IN: index of session to be processed
		   )
{
    TPM2B_AUTH      authValue;
    TPM_HANDLE      associatedHandle = s_associatedHandles[sessionIndex];
    
    // Strip trailing zeros from the password.
    MemoryRemoveTrailingZeros(&s_inputAuthValues[sessionIndex]);
    
    // Get the auth value and size.
    authValue.t.size = EntityGetAuthValue(associatedHandle, &authValue.t.buffer);
    
    // Success if the digests are identical.
    if(Memory2BEqual(&s_inputAuthValues[sessionIndex].b, &authValue.b))
	{
	    return TPM_RC_SUCCESS;
	}
    else                    // if the digests are not identical
	{
	    // Invoke DA protection if applicable.
	    return IncrementLockout(sessionIndex);
	}
}

// 6.4.4.3	ComputeCommandHMAC()
// This function computes the HMAC for an authorization session in a command.

static void
ComputeCommandHMAC(
		   UINT32           sessionIndex,  // IN: index of session to be processed
		   TPM2B_DIGEST    *cpHash,        // IN: cpHash
		   TPM2B_DIGEST    *hmac           // OUT: authorization HMAC
		   )
{
    TPM2B_TYPE(KEY, (sizeof(AUTH_VALUE) * 2));
    TPM2B_KEY        key;
    BYTE             marshalBuffer[sizeof(TPMA_SESSION)];
    BYTE            *buffer;
    UINT32           marshalSize;
    HMAC_STATE       hmacState;
    TPM2B_NONCE     *nonceDecrypt;
    TPM2B_NONCE     *nonceEncrypt;
    SESSION         *session;
    
    nonceDecrypt = NULL;
    nonceEncrypt = NULL;
    
    // Determine if extra nonceTPM values are going to be required.
    // If this is the first session (sessionIndex = 0) and it is an authorization
    // session that uses an HMAC, then check if additional session nonces are to be
    // included.
    if(   sessionIndex == 0
	  && s_associatedHandles[sessionIndex] != TPM_RH_UNASSIGNED)
	{
	    // If there is a decrypt session and if this is not the decrypt session,
	    // then an extra nonce may be needed.
	    if(   s_decryptSessionIndex != UNDEFINED_INDEX
		  && s_decryptSessionIndex != sessionIndex)
		{
		    // Will add the nonce for the decrypt session.
		    SESSION *decryptSession
			= SessionGet(s_sessionHandles[s_decryptSessionIndex]);
		    nonceDecrypt = &decryptSession->nonceTPM;
		}
	    // Now repeat for the encrypt session.
	    if(   s_encryptSessionIndex != UNDEFINED_INDEX
		  && s_encryptSessionIndex != sessionIndex
		  && s_encryptSessionIndex != s_decryptSessionIndex)
		{
		    // Have to have the nonce for the encrypt session.
		    SESSION *encryptSession
			= SessionGet(s_sessionHandles[s_encryptSessionIndex]);
		    nonceEncrypt = &encryptSession->nonceTPM;
		}
	}
    
    // Continue with the HMAC processing.
    session = SessionGet(s_sessionHandles[sessionIndex]);
    
    // Generate HMAC key.
    MemoryCopy2B(&key.b, &session->sessionKey.b, sizeof(key.t.buffer));
    
    // Check if the session has an associated handle and if the associated entity
    // is the one to which the session is bound. If not, add the authValue of
    // this entity to the HMAC key.
    // If the session is bound to the object or the session is a policy session
    // with no authValue required, do not include the authValue in the HMAC key.
    // Note: For a policy session, its isBound attribute is CLEARED.
    
    // Include the entity authValue if is is needed
    if(session->attributes.includeAuth == SET)
	{
	    // add the authValue to the HMAC key
	    pAssert((sizeof(AUTH_VALUE) + key.t.size) <= sizeof(key.t.buffer));
	    key.t.size =   key.t.size
			   + EntityGetAuthValue(s_associatedHandles[sessionIndex],
						(AUTH_VALUE *)&(key.t.buffer[key.t.size]));
	}

     // if the HMAC key size is 0, a NULL string HMAC is allowed
    if(    key.t.size == 0
	   && s_inputAuthValues[sessionIndex].t.size == 0)
	{
	    hmac->t.size = 0;
	    return;
	}
    
    // Start HMAC
    hmac->t.size = CryptStartHMAC2B(session->authHashAlg, &key.b, &hmacState);
    
    //  Add cpHash
    CryptUpdateDigest2B(&hmacState, &cpHash->b);
    
    //  Add nonces as requires
    CryptUpdateDigest2B(&hmacState, &s_nonceCaller[sessionIndex].b);
    CryptUpdateDigest2B(&hmacState, &session->nonceTPM.b);
    if(nonceDecrypt != NULL)
	CryptUpdateDigest2B(&hmacState, &nonceDecrypt->b);
    if(nonceEncrypt != NULL)
	CryptUpdateDigest2B(&hmacState, &nonceEncrypt->b);
    
    //  Add sessionAttributes
    buffer = marshalBuffer;
    marshalSize = TPMA_SESSION_Marshal(&(s_attributes[sessionIndex]),
				       &buffer, NULL);
    CryptUpdateDigest(&hmacState, marshalSize, marshalBuffer);
    
    // Complete the HMAC computation
    CryptCompleteHMAC2B(&hmacState, &hmac->b);
    
    return;
}

// 6.4.4.4	CheckSessionHMAC()

// This function checks the HMAC of in a session. It uses ComputeCommandHMAC() to compute the
// expected HMAC value and then compares the result with the HMAC in the authorization session. The
// authorization is successful if they are the same.

// If the authorizations are not the same, IncrementLockout() is called. It will return
// TPM_RC_AUTH_FAIL if the failure caused the failureCount to increment. Otherwise, it will return
// TPM_RC_BAD_AUTH.

//     Error Returns	Meaning
//     TPM_RC_AUTH_FAIL	auth failure caused failureCount increment
//     TPM_RC_BAD_AUTH	auth failure did not cause failureCount increment

static TPM_RC
CheckSessionHMAC(
		 UINT32           sessionIndex,  // IN: index of session to be processed
		 TPM2B_DIGEST    *cpHash         // IN: cpHash of the command
		 )
{
    TPM2B_DIGEST        hmac;           // authHMAC for comparing
    
    // Compute authHMAC
    ComputeCommandHMAC(sessionIndex, cpHash, &hmac);
    
    // Compare the input HMAC with the authHMAC computed above.
    if(!Memory2BEqual(&s_inputAuthValues[sessionIndex].b,  &hmac.b))
	{
	    // If an HMAC session has a failure, invoke the anti-hammering
	    // if it applies to the authorized entity or the session.
	    // Otherwise, just indicate that the authorization is bad.
	    return IncrementLockout(sessionIndex);
	}
    return TPM_RC_SUCCESS;
}

// 6.4.4.5	CheckPolicyAuthSession()

// This function is used to validate the authorization in a policy session. This function performs
// the following comparisons to see if a policy authorization is properly provided. The check are:

// a)	compare policyDigest in session with authPolicy associated with the entity to be authorized;
// b)	compare timeout if applicable;
// c)	compare commandCode if applicable;
// d)	compare cpHash if applicable; and
// e)	see if PCR values have changed since computed.

// If all the above checks succeed, the handle is authorized. The order of these comparisons is not
// important because any failure will result in the same error code.

//       Error Returns	Meaning
//       TPM_RC_PCR_CHANGED	PCR value is not current
//       TPM_RC_POLICY_FAIL	policy session fails
//       TPM_RC_LOCALITY	command locality is not allowed
//       TPM_RC_POLICY_CC	CC doesn't match
// TPM_RC_EXPIRED	policy session has expired
// TPM_RC_PP	PP is required but not asserted
// TPM_RC_NV_UNAVAILABLE	NV is not available for write
// TPM_RC_NV_RATE	NV is rate limiting

static TPM_RC
CheckPolicyAuthSession(
		       UINT32           sessionIndex,  // IN: index of session to be processed
		       COMMAND_INDEX    commandIndex,  // IN: command index
		       TPM2B_DIGEST    *cpHash,        // IN: cpHash using the algorithm of this
		       //     session
		       TPM2B_DIGEST    *nameHash       // IN: nameHash using the session algorithm
		       )
{
    TPM_RC           result = TPM_RC_SUCCESS;
    SESSION         *session;
    TPM2B_DIGEST     authPolicy;
    TPMI_ALG_HASH    policyAlg;
    UINT8            locality;
    TPM_CC           cc = GetCommandCode(commandIndex);
    
    // Initialize pointer to the auth session.
    session = SessionGet(s_sessionHandles[sessionIndex]);
    
    // If the command is TPM2_PolicySecret(), make sure that
    // either password or authValue is required
    if( 	cc == TPM_CC_PolicySecret
	    &&  session->attributes.isPasswordNeeded == CLEAR
	    &&  session->attributes.isAuthValueNeeded == CLEAR)
	return TPM_RC_MODE;
    
    // See if the PCR counter for the session is still valid.
    if( !SessionPCRValueIsCurrent(s_sessionHandles[sessionIndex]) )
	return TPM_RC_PCR_CHANGED;
    
    // Get authPolicy.
    policyAlg = EntityGetAuthPolicy(s_associatedHandles[sessionIndex],
				    &authPolicy);
    // Compare authPolicy.
    if(!Memory2BEqual(&session->u2.policyDigest.b, &authPolicy.b))
	return TPM_RC_POLICY_FAIL;
    
    // Policy is OK so check if the other factors are correct
    
    // Compare policy hash algorithm.
    if(policyAlg != session->authHashAlg)
	return TPM_RC_POLICY_FAIL;
    
    // Compare timeout.
    if(session->timeOut != 0)
	{
	    // Cannot compare time if clock stop advancing.  An TPM_RC_NV_UNAVAILABLE
	    // or TPM_RC_NV_RATE error may be returned here.
	    result = NvIsAvailable();
	    if(result != TPM_RC_SUCCESS)
		return result;
	    
	    if(session->timeOut < go.clock)
		return TPM_RC_EXPIRED;
	}
    
    // If command code is provided it must match
    if(session->commandCode != 0)
	{
	    if(session->commandCode != cc)
		return TPM_RC_POLICY_CC;
	}
    else
	{
	    // If command requires a DUP or ADMIN authorization, the session must have
	    // command code set.
	    AUTH_ROLE   role = CommandAuthRole(commandIndex, sessionIndex);
	    if(role == AUTH_ADMIN || role == AUTH_DUP)
		return TPM_RC_POLICY_FAIL;
	}
    // Check command locality.
    {
	BYTE         sessionLocality[sizeof(TPMA_LOCALITY)];
	BYTE        *buffer = sessionLocality;
	
	// Get existing locality setting in canonical form
	TPMA_LOCALITY_Marshal(&session->commandLocality, &buffer, NULL);
	
	// See if the locality has been set
	if(sessionLocality[0] != 0)
	    {
		// If so, get the current locality
		locality = _plat__LocalityGet();
		if (locality < 5)
		    {
			if(    ((sessionLocality[0] & (1 << locality)) == 0)
			       || sessionLocality[0] > 31)
			    return TPM_RC_LOCALITY;
		    }
		else if (locality > 31)
		    {
			if(sessionLocality[0] != locality)
			    return TPM_RC_LOCALITY;
		    }
		else
		    {
			// Could throw an assert here but a locality error is just
			// as good. It just means that, whatever the locality is, it isn't
			// the locality requested so...
			return TPM_RC_LOCALITY;
		    }
	    }
    } // end of locality check
    
    // Check physical presence.
    if(   session->attributes.isPPRequired == SET
	  && !_plat__PhysicalPresenceAsserted())
	return TPM_RC_PP;
    
    // Compare cpHash/nameHash if defined, or if the command requires an ADMIN or
    // DUP role for this handle.
    if(session->u1.cpHash.b.size != 0)
	{
	    if(session->attributes.iscpHashDefined)
		{
		    // Compare cpHash.
		    if(!Memory2BEqual(&session->u1.cpHash.b, &cpHash->b))
			return TPM_RC_POLICY_FAIL;
		}
	    else
		{
		    // Compare nameHash.
		    // When cpHash is not defined, nameHash is placed in its space.
		    if(!Memory2BEqual(&session->u1.cpHash.b, &nameHash->b))
			return TPM_RC_POLICY_FAIL;
		}
	}
    if(session->attributes.checkNvWritten)
	{
	    NV_INDEX        nvIndex;
	    
	    // If this is not an NV index, the policy makes no sense so fail it.
	    if(HandleGetType(s_associatedHandles[sessionIndex])!= TPM_HT_NV_INDEX)
		return TPM_RC_POLICY_FAIL;
	    
	    // Get the index data
	    NvGetIndexInfo(s_associatedHandles[sessionIndex], &nvIndex);
	    
	    // Make sure that the TPMA_WRITTEN_ATTRIBUTE has the desired state
	    if(   (nvIndex.publicArea.attributes.TPMA_NV_WRITTEN == SET)
		  != (session->attributes.nvWrittenState == SET))
		return TPM_RC_POLICY_FAIL;
	}
    
    return TPM_RC_SUCCESS;
}

// 6.4.4.6	RetrieveSessionData()

// This function will unmarshal the sessions in the session area of a command. The values are placed
// in the arrays that are defined at the beginning of this file. The normal unmarshaling errors are
// possible.

// Error Returns	Meaning
// TPM_RC_SUCCSS	unmarshaled without error
// TPM_RC_SIZE	the number of bytes unmarshaled is not the same as the value for authorizationSize in the command

static TPM_RC
RetrieveSessionData (
		     COMMAND_INDEX    commandIndex,  // IN: command index
		     UINT32          *sessionCount,  // OUT: number of sessions found
		     BYTE            *sessionBuffer, // IN: pointer to the session buffer
		     INT32            bufferSize     // IN: size of the session buffer
		     )
{
    int          sessionIndex;
    int          i;
    TPM_RC       result;
    SESSION     *session;
    TPM_HT       sessionType;
    
    s_decryptSessionIndex = UNDEFINED_INDEX;
    s_encryptSessionIndex = UNDEFINED_INDEX;
    s_auditSessionIndex = UNDEFINED_INDEX;
    
    for(sessionIndex = 0; bufferSize > 0; sessionIndex++)
	{
	    // If maximum allowed number of sessions has been parsed, return a size
	    // error with a session number that is larger than the number of allowed
	    // sessions
	    if(sessionIndex == MAX_SESSION_NUM)
		return TPM_RCS_SIZE + TPM_RC_S + g_rcIndex[sessionIndex+1];
	    
	    // make sure that the associated handle for each session starts out
	    // unassigned
	    s_associatedHandles[sessionIndex] = TPM_RH_UNASSIGNED;
	    
	    // First parameter: Session handle.
	    result = TPMI_SH_AUTH_SESSION_Unmarshal(&s_sessionHandles[sessionIndex],
						    &sessionBuffer, &bufferSize, TRUE);
	    if(result != TPM_RC_SUCCESS)
		return result + TPM_RC_S + g_rcIndex[sessionIndex];
	    
	    // Second parameter: Nonce.
	    result = TPM2B_NONCE_Unmarshal(&s_nonceCaller[sessionIndex],
					   &sessionBuffer, &bufferSize);
	    if(result != TPM_RC_SUCCESS)
		return result + TPM_RC_S + g_rcIndex[sessionIndex];
	    
	    // Third parameter: sessionAttributes.
	    result = TPMA_SESSION_Unmarshal(&s_attributes[sessionIndex],
					    &sessionBuffer, &bufferSize);
	    if(result != TPM_RC_SUCCESS)
		return result + TPM_RC_S + g_rcIndex[sessionIndex];
	    
	    // Fourth parameter: authValue (PW or HMAC).
	    result = TPM2B_AUTH_Unmarshal(&s_inputAuthValues[sessionIndex],
					  &sessionBuffer, &bufferSize);
	    if(result != TPM_RC_SUCCESS)
		return result + TPM_RC_S + g_rcIndex[sessionIndex];
	    
	    if(s_sessionHandles[sessionIndex] == TPM_RS_PW)
		{
		    // A PWAP session needs additional processing.
		    //     Can't have any attributes set other than continueSession bit
		    if(   s_attributes[sessionIndex].encrypt
			  || s_attributes[sessionIndex].decrypt
			  || s_attributes[sessionIndex].audit
			  || s_attributes[sessionIndex].auditExclusive
			  || s_attributes[sessionIndex].auditReset
			  )
			return TPM_RCS_ATTRIBUTES + TPM_RC_S + g_rcIndex[sessionIndex];
		    
		    //     The nonce size must be zero.
		    if(s_nonceCaller[sessionIndex].t.size != 0)
			return TPM_RCS_NONCE + TPM_RC_S + g_rcIndex[sessionIndex];
		    
		    continue;
		}
	    // For not password sessions...
	    
	    // Find out if the session is loaded.
	    if(!SessionIsLoaded(s_sessionHandles[sessionIndex]))
		return TPM_RC_REFERENCE_S0 + sessionIndex;
	    
	    sessionType = HandleGetType(s_sessionHandles[sessionIndex]);
	    session = SessionGet(s_sessionHandles[sessionIndex]);
	    // Check if the session is an HMAC/policy session.
	    if(   (   session->attributes.isPolicy == SET
		      && sessionType == TPM_HT_HMAC_SESSION
		      )
		  || (   session->attributes.isPolicy == CLEAR
			 &&  sessionType == TPM_HT_POLICY_SESSION
			 )
		  )
		return TPM_RCS_HANDLE + TPM_RC_S + g_rcIndex[sessionIndex];
	    
	    // Check that this handle has not previously been used.
	    for(i = 0; i < sessionIndex; i++)
		{
		    if(s_sessionHandles[i] == s_sessionHandles[sessionIndex])
			return TPM_RCS_HANDLE + TPM_RC_S + g_rcIndex[sessionIndex];
		}
	    
	    // If the session is used for parameter encryption or audit as well, set
	    // the corresponding indices.
	    
	    // First process decrypt.
	    if(s_attributes[sessionIndex].decrypt)
		{
		    // Check if the commandCode allows command parameter encryption.
		    if(DecryptSize(commandIndex) == 0)
			return TPM_RCS_ATTRIBUTES + TPM_RC_S + g_rcIndex[sessionIndex];
		    
		    // Encrypt attribute can only appear in one session
		    if(s_decryptSessionIndex != UNDEFINED_INDEX)
			return TPM_RCS_ATTRIBUTES + TPM_RC_S + g_rcIndex[sessionIndex];
		    
		    // Can't decrypt if the session's symmetric algorithm is TPM_ALG_NULL
		    if(session->symmetric.algorithm == TPM_ALG_NULL)
			return TPM_RCS_SYMMETRIC + TPM_RC_S + g_rcIndex[sessionIndex];
		    
		    // All checks passed, so set the index for the session used to decrypt
		    // a command parameter.
		    s_decryptSessionIndex = sessionIndex;
		}
	    
	    // Now process encrypt.
	    if(s_attributes[sessionIndex].encrypt)
		{
		    // Check if the commandCode allows response parameter encryption.
		    if(EncryptSize(commandIndex) == 0)
			return TPM_RCS_ATTRIBUTES + TPM_RC_S + g_rcIndex[sessionIndex];
		    
		    // Encrypt attribute can only appear in one session.
		    if(s_encryptSessionIndex != UNDEFINED_INDEX)
			return TPM_RCS_ATTRIBUTES + TPM_RC_S + g_rcIndex[sessionIndex];
		    
		    // Can't encrypt if the session's symmetric algorithm is TPM_ALG_NULL
		    if(session->symmetric.algorithm == TPM_ALG_NULL)
			return TPM_RCS_SYMMETRIC + TPM_RC_S + g_rcIndex[sessionIndex];
		    
		    // All checks passed, so set the index for the session used to encrypt
		    // a response parameter.
		    s_encryptSessionIndex = sessionIndex;
		}
	    
	    // At last process audit.
	    if(s_attributes[sessionIndex].audit)
		{
		    // Audit attribute can only appear in one session.
		    if(s_auditSessionIndex != UNDEFINED_INDEX)
			return TPM_RCS_ATTRIBUTES + TPM_RC_S + g_rcIndex[sessionIndex];
		    
		    // An audit session can not be policy session.
		    if(   HandleGetType(s_sessionHandles[sessionIndex])
			  == TPM_HT_POLICY_SESSION)
			return TPM_RCS_ATTRIBUTES + TPM_RC_S + g_rcIndex[sessionIndex];
		    
		    // If this is a reset of the audit session, or the first use
		    // of the session as an audit session, it doesn't matter what
		    // the exclusive state is. The session will become exclusive.
		    if(   s_attributes[sessionIndex].auditReset == CLEAR
			  && session->attributes.isAudit == SET)
			{
			    // Not first use or reset. If auditExlusive is SET, then this
			    // session must be the current exclusive session.
			    if(   s_attributes[sessionIndex].auditExclusive == SET
				  && g_exclusiveAuditSession != s_sessionHandles[sessionIndex])
				return TPM_RC_EXCLUSIVE;
			}
		    
		    s_auditSessionIndex = sessionIndex;
		}
	    
	    // Initialize associated handle as undefined. This will be changed when
	    // the handles are processed.
	    s_associatedHandles[sessionIndex] = TPM_RH_UNASSIGNED;
	    
	}
    
    // Set the number of sessions found.
    *sessionCount = sessionIndex;
    return TPM_RC_SUCCESS;
}

// 6.4.4.7	CheckLockedOut()

// This function checks to see if the TPM is in lockout. This function should only be called if the
// entity being checked is subject to DA protection. The TPM is in lockout if the NV is not
// available and a DA write is pending. Otherwise the TPM is locked out if checking for lockoutAuth
// (lockoutAuthCheck == TRUE) and use of lockoutAuth is disabled, or failedTries >= maxTries

//     Error Returns	Meaning
//     TPM_RC_NV_RATE	NV is rate limiting
//     TPM_RC_NV_UNAVAILABLE	NV is not available at this time
//     TPM_RC_LOCKOUT	TPM is in lockout

static TPM_RC
CheckLockedOut(
	       BOOL             lockoutAuthCheck   // IN: TRUE if checking is for lockoutAuth
	       )
{
    TPM_RC      result;
    
    // If NV is unavailable, and current cycle state recorded in NV is not
    // SHUTDOWN_NONE, refuse to check any authorization because we would
    // not be able to handle a DA failure.
    result = NvIsAvailable();
    if(result != TPM_RC_SUCCESS && gp.orderlyState != SHUTDOWN_NONE)
	return result;
    
    // Check if DA info needs to be updated in NV.
    if(s_DAPendingOnNV)
	{
	    // If NV is accessible, ...
	    if(result == TPM_RC_SUCCESS)
		{
		    // ... write the pending DA data and proceed.
		    NvWriteReserved(NV_LOCKOUT_AUTH_ENABLED,
				    &gp.lockOutAuthEnabled);
		    NvWriteReserved(NV_FAILED_TRIES, &gp.failedTries);
		    g_updateNV = TRUE;
		    s_DAPendingOnNV = FALSE;
		}
	    else
		{
		    // Otherwise no authorization can be checked.
		    return result;
		}
	}
    
    // Lockout is in effect if checking for lockoutAuth and use of lockoutAuth
    // is disabled...
    if(lockoutAuthCheck)
	{
	    if(gp.lockOutAuthEnabled == FALSE)
		return TPM_RC_LOCKOUT;
	}
    else
	{
	    // ... or if the number of failed tries has been maxed out.
	    if(gp.failedTries >= gp.maxTries)
		return TPM_RC_LOCKOUT;
	}
    return TPM_RC_SUCCESS;
}

// 6.4.4.8	CheckAuthSession()
// This function checks that the authorization session properly authorizes the use of the associated handle.
// Error Returns	Meaning
//     TPM_RC_LOCKOUT entity is protected by DA and TPM is in lockout, or TPM is locked out on NV
//     			update pending on DA parameters
//     TPM_RC_PP	Physical Presence is required but not provided
//     TPM_RC_AUTH_FAIL	HMAC or PW authorization failed with DA side-effects (can be a policy session)
//     TPM_RC_BAD_AUTH	HMAC or PW authorization failed without DA side-effects (can be a policy session)
//     TPM_RC_POLICY_FAIL	if policy session fails
//     TPM_RC_POLICY_CC	command code of policy was wrong
//     TPM_RC_EXPIRED	the policy session has expired
//     TPM_RC_PCR	???
//     TPM_RC_AUTH_UNAVAILABLE	authValue or authPolicy unavailable

static TPM_RC
CheckAuthSession(
		 COMMAND_INDEX    commandIndex,  // IN: command index
		 UINT32           sessionIndex,  // IN: index of session to be processed
		 TPM2B_DIGEST    *cpHash,        // IN: cpHash
		 TPM2B_DIGEST    *nameHash       // IN: nameHash
		 )
{
    TPM_RC           result;
    SESSION         *session = NULL;
    TPM_HANDLE       sessionHandle = s_sessionHandles[sessionIndex];
    TPM_HANDLE       associatedHandle = s_associatedHandles[sessionIndex];
    TPM_HT           sessionHandleType = HandleGetType(sessionHandle);
    
    pAssert(sessionHandle != TPM_RH_UNASSIGNED);
    
    // Take care of physical presence
    if(associatedHandle == TPM_RH_PLATFORM)
	{
	    // If the physical presence is required for this command, check for PP
	    // assertion. If it isn't asserted, no point going any further.
	    if(   PhysicalPresenceIsRequired(commandIndex)
		  && !_plat__PhysicalPresenceAsserted()
		  )
		return TPM_RC_PP;
	}
    if(sessionHandle != TPM_RS_PW)
	{
	    session = SessionGet(sessionHandle);
	    
	    // Set includeAuth to indicate if DA checking will be required and if the
	    // authValue will be included in any HMAC.
	    if(sessionHandleType == TPM_HT_POLICY_SESSION)
		{
		    // For a policy session, will check the DA status of the entity if either
		    // isAuthValueNeeded or isPasswordNeeded is SET.
		    session->attributes.includeAuth =
			session->attributes.isAuthValueNeeded
			||  session->attributes.isPasswordNeeded;
		}
	    else
		{
		    // For an HMAC session, need to check unless the session
		    // is bound.
		    session->attributes.includeAuth =
			!IsSessionBindEntity(s_associatedHandles[sessionIndex], session);
		}
	}
    // If the authorization session is going to use an authValue, then make sure
    // that access to that authValue isn't locked out.
    // Note: session == NULL for a PW session.
    if(     session == NULL
	    ||  session->attributes.includeAuth)
	{
	    // See if entity is subject to lockout.
	    if(!IsDAExempted(associatedHandle))
		{
		    // If NV is unavailable, and current cycle state recorded in NV is not
		    // SHUTDOWN_NONE, refuse to check any authorization because we would
		    // not be able to handle a DA failure.
		    result = CheckLockedOut(associatedHandle == TPM_RH_LOCKOUT);
		    if(result != TPM_RC_SUCCESS)
			return result;
		}
	}
    // Policy or HMAC+PW?
    if(sessionHandleType != TPM_HT_POLICY_SESSION)
	{
	    // for non-policy session make sure that a policy session is not required
	    if(IsPolicySessionRequired(commandIndex, sessionIndex))
		return TPM_RC_AUTH_TYPE;
	    
	    // The authValue must be available.
	    // Note: The authValue is going to be "used" even if it is an Empty Auth.
	    // and the session is bound.
	    if(!IsAuthValueAvailable(associatedHandle, commandIndex, sessionIndex))
		return TPM_RC_AUTH_UNAVAILABLE;
	}
    else
	{
	    // ... see if the entity has a policy, ...
	    // Note: IsAutPolciyAvalable will return FALSE if the sensitive area of the
	    // object is not loaded
	    if( !IsAuthPolicyAvailable(associatedHandle, commandIndex, sessionIndex))
		return TPM_RC_AUTH_UNAVAILABLE;
	    
	    // ... and check the policy session.
	    result = CheckPolicyAuthSession(sessionIndex, commandIndex,
					    cpHash, nameHash);
	    if(result != TPM_RC_SUCCESS)
		return result;
	}
    // Check authorization according to the type
    if(     session == NULL
	    ||  session->attributes.isPasswordNeeded == SET)
	result = CheckPWAuthSession(sessionIndex);
    else
	result = CheckSessionHMAC(sessionIndex, cpHash);
    
    return result;
}

#ifdef  TPM_CC_GetCommandAuditDigest
// 6.4.4.9	CheckCommandAudit()
// This function checks if the current command may trigger command audit, and if it is safe to perform the action.
//     Error Returns	Meaning
//     TPM_RC_NV_UNAVAILABLE	NV is not available for write
//     TPM_RC_NV_RATE	NV is rate limiting

static TPM_RC
CheckCommandAudit(
		  COMMAND_INDEX    commandIndex,      // IN: command index
		  UINT32           handleNum,         // IN: number of element in handle array
		  TPM_HANDLE       handles[],         // IN: array of handles
		  BYTE            *parmBufferStart,   // IN: start of parameter buffer
		  UINT32           parmBufferSize     // IN: size of parameter buffer
		  )
{
    TPM_RC      result = TPM_RC_SUCCESS;
    
    // If audit is implemented, need to check to see if auditing is being done
    // for this command.
    if(CommandAuditIsRequired(commandIndex))
	{
	    // If the audit digest is clear and command audit is required, NV must be
	    // available so that TPM2_GetCommandAuditDigest() is able to increment
	    // audit counter. If NV is not available, the function bails out to prevent
	    // the TPM from attempting an operation that would fail anyway.
	    if(    gr.commandAuditDigest.t.size == 0
		   || GetCommandCode(commandIndex) == TPM_CC_GetCommandAuditDigest)
		{
		    result = NvIsAvailable();
		    if(result != TPM_RC_SUCCESS)
			return result;
		}
	    ComputeCpHash(gp.auditHashAlg, commandIndex, handleNum,
			  handles, parmBufferSize, parmBufferStart,
			  &s_cpHashForCommandAudit, NULL);
	}
    
    return TPM_RC_SUCCESS;
}
#endif

// 6.4.4.10	ParseSessionBuffer()

// This function is the entry function for command session processing. It iterates sessions in
// session area and reports if the required authorization has been properly provided. It also
// processes audit session and passes the information of encryption sessions to parameter encryption
// module.

// Error Returns	Meaning
// various	parsing failure or authorization failure

TPM_RC
ParseSessionBuffer(
		   COMMAND_INDEX    commandIndex,          // IN: Command index
		   UINT32           handleNum,             // IN: number of element in handle array
		   TPM_HANDLE       handles[],             // IN: array of handles
		   BYTE            *sessionBufferStart,    // IN: start of session buffer
		   UINT32           sessionBufferSize,     // IN: size of session buffer
		   BYTE            *parmBufferStart,       // IN: start of parameter buffer
		   UINT32           parmBufferSize         // IN: size of parameter buffer
		   )
{
    TPM_RC           result;
    UINT32           i;
    INT32            size = 0;
    TPM2B_AUTH       extraKey;
    UINT32           sessionIndex;
    SESSION         *session = NULL;
    TPM2B_DIGEST     cpHash;
    TPM2B_DIGEST     nameHash;
    TPM_ALG_ID       cpHashAlg = TPM_ALG_NULL;  // algID for the last computed
    // cpHash
    
    // Check if a command allows any session in its session area.
    if(!IsSessionAllowed(commandIndex))
	return TPM_RC_AUTH_CONTEXT;
    
    // Default-initialization.
    s_sessionNum = 0;
    cpHash.t.size = 0;
    
    result = RetrieveSessionData(commandIndex, &s_sessionNum,
				 sessionBufferStart, sessionBufferSize);
    if(result != TPM_RC_SUCCESS)
	return result;
    
    // There is no command in the TPM spec that has more handles than
    // MAX_SESSION_NUM.
    pAssert(handleNum <= MAX_SESSION_NUM);
    
    // Associate the session with an authorization handle.
    for(i = 0; i < handleNum; i++)
	{
	    if(CommandAuthRole(commandIndex, i) != AUTH_NONE)
		{
		    // If the received session number is less than the number of handle
		    // that requires authorization, an error should be returned.
		    // Note: for all the TPM 2.0 commands, handles requiring
		    // authorization come first in a command input.
		    if(i > (s_sessionNum - 1))
			return TPM_RC_AUTH_MISSING;
		    
		    // Record the handle associated with the authorization session
		    s_associatedHandles[i] = handles[i];
		}
	}
    
    // Consistency checks are done first to avoid auth failure when the command
    // will not be executed anyway.
    for(sessionIndex = 0; sessionIndex < s_sessionNum; sessionIndex++)
	{
	    // PW session must be an authorization session
	    if(s_sessionHandles[sessionIndex] == TPM_RS_PW )
		{
		    if(s_associatedHandles[sessionIndex] == TPM_RH_UNASSIGNED)
			return TPM_RCS_HANDLE + TPM_RC_S + g_rcIndex[sessionIndex];

		    // a password session can't be audit, encrypt or decrypt
		    if(     s_attributes[sessionIndex].audit == SET
			    ||  s_attributes[sessionIndex].encrypt == SET
			    ||  s_attributes[sessionIndex].decrypt == SET)
			return TPM_RCS_ATTRIBUTES + TPM_RC_S + g_rcIndex[sessionIndex];
		}
	    else
		{
		    session = SessionGet(s_sessionHandles[sessionIndex]);
		    
		    // A trial session can not appear in session area, because it cannot
		    // be used for authorization, audit or encrypt/decrypt.
		    if(session->attributes.isTrialPolicy == SET)
			return TPM_RCS_ATTRIBUTES + TPM_RC_S + g_rcIndex[sessionIndex];
		    
		    // See if the session is bound to a DA protected entity
		    // NOTE: Since a policy session is never bound, a policy is still
		    // usable even if the object is DA protected and the TPM is in
		    // lockout.
		    if(session->attributes.isDaBound == SET)
			{
			    result = CheckLockedOut(session->attributes.isLockoutBound == SET);
			    if(result != TPM_RC_SUCCESS)
				return result;
			}
		    // If the current cpHash is the right one, don't re-compute.
		    if(cpHashAlg != session->authHashAlg)   // different so compute
			{
			    cpHashAlg = session->authHashAlg;   // save this new algID
			    ComputeCpHash(session->authHashAlg, commandIndex, handleNum,
					  handles, parmBufferSize, parmBufferStart,
					  &cpHash, &nameHash);
			}
		    // If this session is for auditing, save the cpHash.
		    if(s_attributes[sessionIndex].audit)
			s_cpHashForAudit = cpHash;
		}
	    
	    // if the session has an associated handle, check the auth
	    if(s_associatedHandles[sessionIndex] != TPM_RH_UNASSIGNED)
		{
		    result = CheckAuthSession(commandIndex, sessionIndex,
					      &cpHash, &nameHash);
		    if(result != TPM_RC_SUCCESS)
			return RcSafeAddToResult(result,
						 TPM_RC_S + g_rcIndex[sessionIndex]);
		}
	    else
		{
		    // a session that is not for authorization must either be encrypt,
		    // decrypt, or audit
		    if(     s_attributes[sessionIndex].audit == CLEAR
			    &&  s_attributes[sessionIndex].encrypt == CLEAR
			    &&  s_attributes[sessionIndex].decrypt == CLEAR)
			return TPM_RCS_ATTRIBUTES + TPM_RC_S + g_rcIndex[sessionIndex];
		    
		    // no authValue included in any of the HMAC computations
		    pAssert(session != NULL);
		    session->attributes.includeAuth = CLEAR;
		    
		    // check HMAC for encrypt/decrypt/audit only sessions
		    result =  CheckSessionHMAC(sessionIndex, &cpHash);
		    if(result != TPM_RC_SUCCESS)
			return RcSafeAddToResult(result,
						 TPM_RC_S + g_rcIndex[sessionIndex]);
		}
	}
    
#ifdef  TPM_CC_GetCommandAuditDigest
    // Check if the command should be audited.
    result = CheckCommandAudit(commandIndex, handleNum, handles,
			       parmBufferStart, parmBufferSize);
    if(result != TPM_RC_SUCCESS)
	return result;              // No session number to reference
#endif
    
    // Decrypt the first parameter if applicable. This should be the last operation
    // in session processing.
    // If the encrypt session is associated with a handle and the handle's
    // authValue is available, then authValue is concatenated with sessionKey to
    // generate encryption key, no matter if the handle is the session bound entity
    // or not.
    if(s_decryptSessionIndex != UNDEFINED_INDEX)
    {
        // If this is an authorization session, include the authValue in the
        // generation of the decryption key
        if(   s_associatedHandles[s_decryptSessionIndex] != TPM_RH_UNASSIGNED)
        {
            extraKey.b.size=
                EntityGetAuthValue(s_associatedHandles[s_decryptSessionIndex],
                                   &extraKey.t.buffer);
        }
        else
        {
            extraKey.b.size = 0;
        }
        size = DecryptSize(commandIndex);
        result = CryptParameterDecryption(s_sessionHandles[s_decryptSessionIndex],
                                          &s_nonceCaller[s_decryptSessionIndex].b,
                                          parmBufferSize, (UINT16)size,
                                          &extraKey,
                                          parmBufferStart);
        if(result != TPM_RC_SUCCESS)
            return RcSafeAddToResult(result,
                                     TPM_RC_S + g_rcIndex[s_decryptSessionIndex]);
    }

    return TPM_RC_SUCCESS;
}

// 6.4.4.11	CheckAuthNoSession()

// Function to process a command with no session associated. The function makes sure all the handles
// in the command require no authorization.

// Error Returns	Meaning
// TPM_RC_AUTH_MISSING	failure - one or more handles require auth

TPM_RC
CheckAuthNoSession(
    COMMAND_INDEX        commandIndex,          // IN: Command index
    UINT32               handleNum,             // IN: number of handles in command
    TPM_HANDLE           handles[],             // IN: array of handles
    BYTE                *parmBufferStart,       // IN: start of parameter buffer
    UINT32               parmBufferSize         // IN: size of parameter buffer
)
{
    UINT32 i;
    TPM_RC           result = TPM_RC_SUCCESS;
    
    // Check if the command requires authorization
    for(i = 0; i < handleNum; i++)
	{
	    if(CommandAuthRole(commandIndex, i) != AUTH_NONE)
		return TPM_RC_AUTH_MISSING;
	}
    
#ifdef  TPM_CC_GetCommandAuditDigest
    // Check if the command should be audited.
    result = CheckCommandAudit(commandIndex, handleNum, handles,
			       parmBufferStart, parmBufferSize);
    if(result != TPM_RC_SUCCESS)
	return result;
#endif
    
    // Initialize number of sessions to be 0
    s_sessionNum = 0;
    
    return TPM_RC_SUCCESS;
}

// 6.4.5	Response Session Processing
// 6.4.5.1	Introduction
// The following functions build the session area in a response, and handle the audit sessions (if present).
//     6.4.5.2	ComputeRpHash()
//     Function to compute rpHash (Response Parameter Hash). The rpHash is only computed if there is an HMAC authorization session and the return code is TPM_RC_SUCCESS.

static void
ComputeRpHash(
	      TPM_ALG_ID       hashAlg,           // IN: hash algorithm to compute rpHash
	      COMMAND_INDEX    commandIndex,      // IN: command index
	      UINT32           resParmBufferSize, // IN: size of response parameter buffer
	      BYTE            *resParmBuffer,     // IN: response parameter buffer
	      TPM2B_DIGEST    *rpHash             // OUT: rpHash
	      )
{
    // The command result in rpHash is always TPM_RC_SUCCESS.
    TPM_RC      responseCode = TPM_RC_SUCCESS;
    HASH_STATE  hashState;
    TPM_CC      commandCode = GetCommandCode(commandIndex);
    
    //   rpHash := hash(responseCode || commandCode || parameters)
    
    // Initiate hash creation.
    rpHash->t.size = CryptStartHash(hashAlg, &hashState);
    
    // Add hash constituents.
    CryptUpdateDigestInt(&hashState, sizeof(TPM_RC), &responseCode);
    CryptUpdateDigestInt(&hashState, sizeof(TPM_CC), &commandCode);
    CryptUpdateDigest(&hashState, resParmBufferSize, resParmBuffer);
    
    // Complete hash computation.
    CryptCompleteHash2B(&hashState, &rpHash->b);
    
    return;
}

// 6.4.5.3	InitAuditSession()
// This function initializes the audit data in an audit session.

static void
InitAuditSession(
		 SESSION         *session        // session to be initialized
		 )
{
    // Mark session as an audit session.
    session->attributes.isAudit = SET;
    
    // Audit session can not be bound.
    session->attributes.isBound = CLEAR;
    
    // Size of the audit log is the size of session hash algorithm digest.
    session->u2.auditDigest.t.size = CryptGetHashDigestSize(session->authHashAlg);
    
    // Set the original digest value to be 0.
    MemorySet(&session->u2.auditDigest.t.buffer,
	      0,
	      session->u2.auditDigest.t.size);
    
    return;
}

// 6.4.5.4	Audit()
// This function updates the audit digest in an audit session.

static void
Audit(
      SESSION         *auditSession,      // IN: loaded audit session
      COMMAND_INDEX    commandIndex,      // IN: command index
      UINT32           resParmBufferSize, // IN: size of response parameter buffer
      BYTE            *resParmBuffer      // IN: response parameter buffer
      )
{
    TPM2B_DIGEST     rpHash;            // rpHash for response
    HASH_STATE       hashState;
    
    // Compute rpHash
    ComputeRpHash(auditSession->authHashAlg,
		  commandIndex,
		  resParmBufferSize,
		  resParmBuffer,
		  &rpHash);
    
    // auditDigestnew :=  hash (auditDigestold || cpHash || rpHash)
    
    // Start hash computation.
    CryptStartHash(auditSession->authHashAlg, &hashState);
    
    // Add old digest.
    CryptUpdateDigest2B(&hashState, &auditSession->u2.auditDigest.b);
    
    // Add cpHash and rpHash.
    CryptUpdateDigest2B(&hashState, &s_cpHashForAudit.b);
    CryptUpdateDigest2B(&hashState, &rpHash.b);
    
    // Finalize the hash.
    CryptCompleteHash2B(&hashState, &auditSession->u2.auditDigest.b);
    
    return;
}
#ifdef  TPM_CC_GetCommandAuditDigest

// 6.4.5.5	CommandAudit()

// This function updates the command audit digest.
static void
CommandAudit(
	     COMMAND_INDEX    commandIndex,      // IN: command index
	     UINT32           resParmBufferSize, // IN: size of response parameter buffer
	     BYTE            *resParmBuffer      // IN: response parameter buffer
	     )
{
    if(CommandAuditIsRequired(commandIndex))
	{
	    TPM2B_DIGEST    rpHash;        // rpHash for response
	    HASH_STATE      hashState;
	    
	    // Compute rpHash.
	    ComputeRpHash(gp.auditHashAlg, commandIndex, resParmBufferSize,
			  resParmBuffer, &rpHash);
	    
	    // If the digest.size is one, it indicates the special case of changing
	    // the audit hash algorithm. For this case, no audit is done on exit.
	    // NOTE: When the hash algorithm is changed, g_updateNV is set in order to
	    // force an update to the NV on exit so that the change in digest will
	    // be recorded. So, it is safe to exit here without setting any flags
	    // because the digest change will be written to NV when this code exits.
	    if(gr.commandAuditDigest.t.size == 1)
		{
		    gr.commandAuditDigest.t.size = 0;
		    return;
		}
	    
	    // If the digest size is zero, need to start a new digest and increment
	    // the audit counter.
	    if(gr.commandAuditDigest.t.size == 0)
		{
		    gr.commandAuditDigest.t.size = CryptGetHashDigestSize(gp.auditHashAlg);
		    MemorySet(gr.commandAuditDigest.t.buffer,
			      0,
			      gr.commandAuditDigest.t.size);
		    
		    // Bump the counter and save its value to NV.
		    gp.auditCounter++;
		    NvWriteReserved(NV_AUDIT_COUNTER, &gp.auditCounter);
		    g_updateNV = TRUE;
		}
	    
	    // auditDigestnew :=  hash (auditDigestold || cpHash || rpHash)
	    
	    //  Start hash computation.
	    CryptStartHash(gp.auditHashAlg, &hashState);
	    
	    //  Add old digest.
	    CryptUpdateDigest2B(&hashState, &gr.commandAuditDigest.b);
	    
	    //  Add cpHash
	    CryptUpdateDigest2B(&hashState, &s_cpHashForCommandAudit.b);
	    
	    //  Add rpHash
	    CryptUpdateDigest2B(&hashState, &rpHash.b);
	    
	    //  Finalize the hash.
	    CryptCompleteHash2B(&hashState, &gr.commandAuditDigest.b);
	}
    return;
}
#endif

// 6.4.5.6	UpdateAuditSessionStatus()

// Function to update the internal audit related states of a session. It
// a) initializes the session as audit session and sets it to be exclusive if this is the first time
// it is used for audit or audit reset was requested;
// b)	reports exclusive audit session;
// c)	extends audit log; and
// d)	clears exclusive audit session if no audit session found in the command.

static void
UpdateAuditSessionStatus(
			 COMMAND_INDEX    commandIndex,      // IN: command index
			 UINT32           resParmBufferSize, // IN: size of response parameter buffer
			 BYTE            *resParmBuffer      // IN: response parameter buffer
			 )
{
    UINT32           i;
    TPM_HANDLE       auditSession = TPM_RH_UNASSIGNED;
    
    // Iterate through sessions
    for (i = 0; i < s_sessionNum; i++)
	{
	    SESSION     *session;
	    
	    // PW session do not have a loaded session and can not be an audit
	    // session either.  Skip it.
	    if(s_sessionHandles[i] == TPM_RS_PW) continue;
	    
	    session = SessionGet(s_sessionHandles[i]);
	    
	    // If a session is used for audit
	    if(s_attributes[i].audit == SET)
		{
		    // An audit session has been found
		    auditSession = s_sessionHandles[i];
		    
		    // If the session has not been an audit session yet, or
		    // the auditSetting bits indicate a reset, initialize it and set
		    // it to be the exclusive session
		    if(   session->attributes.isAudit == CLEAR
			  || s_attributes[i].auditReset == SET
			  )
			{
			    InitAuditSession(session);
			    g_exclusiveAuditSession = auditSession;
			}
		    else
			{
			    // Check if the audit session is the current exclusive audit
			    // session and, if not, clear previous exclusive audit session.
			    if(g_exclusiveAuditSession != auditSession)
				g_exclusiveAuditSession = TPM_RH_UNASSIGNED;
			}
		    
		    // Report audit session exclusivity.
		    if(g_exclusiveAuditSession == auditSession)
			{
			    s_attributes[i].auditExclusive = SET;
			}
		    else
			{
			    s_attributes[i].auditExclusive = CLEAR;
			}
		    
		    // Extend audit log.
		    Audit(session, commandIndex, resParmBufferSize, resParmBuffer);
		}
	}
    
    // If no audit session is found in the command, and the command allows
    // a session then, clear the current exclusive
    // audit session.
    if(auditSession == TPM_RH_UNASSIGNED && IsSessionAllowed(commandIndex))
	{
	    g_exclusiveAuditSession = TPM_RH_UNASSIGNED;
	}
    
    return;
}

// 6.4.5.7	ComputeResponseHMAC()
// Function to compute HMAC for authorization session in a response.

static void
ComputeResponseHMAC(
		    UINT32           sessionIndex,      // IN: session index to be processed
		    SESSION         *session,           // IN: loaded session
		    COMMAND_INDEX    commandIndex,      // IN: command index
		    TPM2B_NONCE     *nonceTPM,          // IN: nonceTPM
		    UINT32           resParmBufferSize, // IN: size of response parameter buffer
		    BYTE            *resParmBuffer,     // IN: response parameter buffer
		    TPM2B_DIGEST    *hmac               // OUT: authHMAC
		    )
{
    TPM2B_TYPE(KEY, (sizeof(AUTH_VALUE) * 2));
    TPM2B_KEY        key;       // HMAC key
    BYTE             marshalBuffer[sizeof(TPMA_SESSION)];
    BYTE            *buffer;
    UINT32           marshalSize;
    HMAC_STATE       hmacState;
    TPM2B_DIGEST     rp_hash;
    
    // Compute rpHash.
    ComputeRpHash(session->authHashAlg, commandIndex, resParmBufferSize,
		  resParmBuffer, &rp_hash);
    
    // Generate HMAC key
    MemoryCopy2B(&key.b, &session->sessionKey.b, sizeof(key.t.buffer));
    
    // Add the object authValue if required
    if(session->attributes.includeAuth == SET)
	{
	    // Note: includeAuth may be SET for a policy that is used in
	    // UndefineSpaceSpecial(). At this point, the Index has been deleted
	    // so the includeAuth will have no meaning. However, the
	    // s_associatedHandles[] value for the session is now set to TPM_RH_NULL so
	    // this will return the authValue associated with TPM_RH_NULL and that is
	    // and empty buffer.
	    pAssert((sizeof(AUTH_VALUE) + key.t.size) <= sizeof(key.t.buffer));
	    key.t.size = key.t.size +
			 EntityGetAuthValue(s_associatedHandles[sessionIndex],
					    (AUTH_VALUE *)&key.t.buffer[key.t.size]);
	}
    
    // if the HMAC key size is 0, the response HMAC is computed according to the
    // input HMAC
    if(     key.t.size == 0
	    &&  s_inputAuthValues[sessionIndex].t.size == 0)
	{
	    hmac->t.size = 0;
	    return;
	}
    
    // Start HMAC computation.
    hmac->t.size = CryptStartHMAC2B(session->authHashAlg, &key.b, &hmacState);
    
    // Add hash components.
    CryptUpdateDigest2B(&hmacState, &rp_hash.b);
    CryptUpdateDigest2B(&hmacState, &nonceTPM->b);
    CryptUpdateDigest2B(&hmacState, &s_nonceCaller[sessionIndex].b);
    
    // Add session attributes.
    buffer = marshalBuffer;
    marshalSize = TPMA_SESSION_Marshal(&s_attributes[sessionIndex], &buffer, NULL);
    CryptUpdateDigest(&hmacState, marshalSize, marshalBuffer);
    
    // Finalize HMAC.
    CryptCompleteHMAC2B(&hmacState, &hmac->b);
    
    return;
}

// 6.4.5.8	BuildSingleResponseAuth()
// Function to compute response for an authorization session.

static void
BuildSingleResponseAuth(
			UINT32           sessionIndex,      // IN: session index to be processed
			COMMAND_INDEX    commandIndex,      // IN: command index
			UINT32           resParmBufferSize, // IN: size of response parameter buffer
			BYTE            *resParmBuffer,     // IN: response parameter buffer
			TPM2B_AUTH      *auth               // OUT: authHMAC
			)
{
    // For password authorization, field is empty.
    if(s_sessionHandles[sessionIndex] == TPM_RS_PW)
	{
	    auth->t.size = 0;
	}
    else
	{
	    // Fill in policy/HMAC based session response.
	    SESSION     *session = SessionGet(s_sessionHandles[sessionIndex]);
	    
	    // If the session is a policy session with isPasswordNeeded SET, the auth
	    // field is empty.
	    if(HandleGetType(s_sessionHandles[sessionIndex]) == TPM_HT_POLICY_SESSION
	       && session->attributes.isPasswordNeeded == SET)
		auth->t.size = 0;
	    else
		// Compute response HMAC.
		ComputeResponseHMAC(sessionIndex,
				    session,
				    commandIndex,
				    &session->nonceTPM,
				    resParmBufferSize,
				    resParmBuffer,
				    auth);
	}
    
    return;
}

// 6.4.5.9	UpdateTPMNonce()
// Updates TPM nonce in both internal session or response if applicable.

static void
UpdateTPMNonce(
	       UINT16           noncesSize,    // IN: number of elements in 'nonces' array
	       TPM2B_NONCE      nonces[]       // OUT: nonceTPM
	       )
{
    UINT32      i;
    pAssert(noncesSize >= s_sessionNum);
    for(i = 0; i < s_sessionNum; i++)
	{
	    SESSION     *session;
	    // For PW session, nonce is 0.
	    if(s_sessionHandles[i] == TPM_RS_PW)
		{
		    nonces[i].t.size = 0;
		    continue;
		}
	    session = SessionGet(s_sessionHandles[i]);
	    // Update nonceTPM in both internal session and response.
	    CryptGenerateRandom(session->nonceTPM.t.size, session->nonceTPM.t.buffer);
	    nonces[i] = session->nonceTPM;
	}
    return;
}

// 6.4.5.10	UpdateInternalSession()

// Updates internal sessions:
// a)	Restarts session time
// b)	Clears a policy session since nonce is rolling.

static void
UpdateInternalSession(
		      void
		      )
{
    UINT32      i;
    for(i = 0; i < s_sessionNum; i++)
	{
	    // For PW session, no update.
	    if(s_sessionHandles[i] == TPM_RS_PW) continue;
	    
	    if(s_attributes[i].continueSession == CLEAR)
		{
		    // Close internal session.
		    SessionFlush(s_sessionHandles[i]);
		}
	    else
		{
		    // If nonce is rolling in a policy session, the policy related data
		    // will be re-initialized.
		    if(HandleGetType(s_sessionHandles[i]) == TPM_HT_POLICY_SESSION)
			{
			    SESSION     *session = SessionGet(s_sessionHandles[i]);
			    
			    // When the nonce rolls it starts a new timing interval for the
			    // policy session.
			    SessionResetPolicyData(session);
			    session->startTime = go.clock;
			}
		}
	}
    return;
}
/* 6.4.5.11	BuildResponseSession() */
/* Function to build Session buffer in a response. */

void
BuildResponseSession(
		     TPM_ST           tag,           // IN: tag
		     COMMAND_INDEX    commandIndex,      // IN: command index
		     UINT32           resHandleSize, // IN: size of response handle buffer
		     UINT32           resParmSize,   // IN: size of response parameter buffer
		     UINT32          *resSessionSize // OUT: response session area
		     )
{
    BYTE            *resParmBuffer;
    TPM2B_NONCE  responseNonces[MAX_SESSION_NUM];
    
    // Compute response parameter buffer start.
    resParmBuffer = MemoryGetResponseBuffer(commandIndex) + sizeof(TPM_ST) +
		    sizeof(UINT32) + sizeof(TPM_RC) + resHandleSize;
    
    // For TPM_ST_SESSIONS, there is parameterSize field.
    if(tag == TPM_ST_SESSIONS)
	resParmBuffer += sizeof(UINT32);
    
    // Session nonce should be updated before parameter encryption
    if(tag == TPM_ST_SESSIONS)
    {
        UpdateTPMNonce(MAX_SESSION_NUM, responseNonces);

        // Encrypt first parameter if applicable. Parameter encryption should
        // happen after nonce update and before any rpHash is computed.
        // If the encrypt session is associated with a handle, the authValue of
        // this handle will be concatenated with sessionKey to generate
        // encryption key, no matter if the handle is the session bound entity
        // or not. The authValue is added to sessionKey only when the authValue
        // is available.
        if(s_encryptSessionIndex != UNDEFINED_INDEX)
	    {
		UINT32          size;
		TPM2B_AUTH      extraKey;
		
		// If this is an authorization session, include the authValue in the
		// generation of the encryption key
		if(   s_associatedHandles[s_encryptSessionIndex] != TPM_RH_UNASSIGNED)
		    {
			extraKey.b.size =
			    EntityGetAuthValue(s_associatedHandles[s_encryptSessionIndex],
					       &extraKey.t.buffer);
		    }
		else
		    {
			extraKey.b.size = 0;
		    }
		size = EncryptSize(commandIndex);
		CryptParameterEncryption(s_sessionHandles[s_encryptSessionIndex],
					 &s_nonceCaller[s_encryptSessionIndex].b,
					 (UINT16)size,
					 &extraKey,
					 resParmBuffer);
	    }

    }
    // Audit session should be updated first regardless of the tag.
    // A command with no session may trigger a change of the exclusivity state.
    UpdateAuditSessionStatus(commandIndex, resParmSize, resParmBuffer);
    
    // Command Audit.
#ifdef TPM_CC_GetCommandAuditDigest
    CommandAudit(commandIndex, resParmSize, resParmBuffer);
#endif    

    // Process command with sessions.
    if(tag == TPM_ST_SESSIONS)
	{
	    UINT32           i;
	    BYTE            *buffer;
	    TPM2B_DIGEST     responseAuths[MAX_SESSION_NUM];
	    
	    pAssert(s_sessionNum > 0);
	    
	    // Iterate over each session in the command session area, and create
	    // corresponding sessions for response.
	    for(i = 0; i < s_sessionNum; i++)
		{
		    BuildSingleResponseAuth(i,
					    commandIndex,
					    resParmSize,
					    resParmBuffer,
					    &responseAuths[i]);
		    // Make sure that continueSession is SET on any Password session.
		    // This makes it marginally easier for the management software
		    // to keep track of the closed sessions.
		    if(   s_attributes[i].continueSession == CLEAR
			  && s_sessionHandles[i] == TPM_RS_PW)
			{
			    s_attributes[i].continueSession = SET;
			}
		}
	    
	    // Assemble Response Sessions.
	    *resSessionSize = 0;
	    buffer = resParmBuffer + resParmSize;
	    for(i = 0; i < s_sessionNum; i++)
		{
		    *resSessionSize += TPM2B_NONCE_Marshal(&responseNonces[i],
							   &buffer, NULL);
		    *resSessionSize += TPMA_SESSION_Marshal(&s_attributes[i],
							    &buffer, NULL);
		    *resSessionSize += TPM2B_DIGEST_Marshal(&responseAuths[i],
							    &buffer, NULL);
		}
	    
	    // Update internal sessions after completing response buffer computation.
	    UpdateInternalSession();
	}
    else
	{
	    // Process command with no session.
	    *resSessionSize = 0;
	}
    
    return;
}

/* 6.4.5.12	SessionRemoveAssociationToHandle() */

/* This function deals with the case where an entity associated with an authorization is deleted
   during command processing. The primary use of this is to support UndefineSpaceSpecial(). */

void
SessionRemoveAssociationToHandle(
				 TPM_HANDLE       handle
				 )
{
    UINT32               i;
    
    for(i = 0; i < MAX_SESSION_NUM; i++)
	{
	    if(s_associatedHandles[i] == handle)
		{
		    s_associatedHandles[i] = TPM_RH_NULL;
		}
	}
}
