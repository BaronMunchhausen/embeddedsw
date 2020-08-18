/******************************************************************************
* Copyright (c) 2014 - 2020 Xilinx, Inc.  All rights reserved.
* SPDX-License-Identifier: MIT
******************************************************************************/

/*****************************************************************************/
/**
*
* @file rsa_auth_app.c
* 	This file contains the implementation of the SW app used to
* 	validate any user application. It makes use of librsa to do the same.
*
* @note
*
* None.
*
* <pre>
* MODIFICATION HISTORY:
*
* Ver   Who Date     Changes
* ----- --- -------- -----------------------------------------------
* 1.0   hk  27/01/14 First release
*
*</pre>
*
******************************************************************************/

/***************************** Include Files *********************************/

#include "xparameters.h"
#include "xil_types.h"
#include "xil_assert.h"
#include "xil_io.h"
#include "xstatus.h"
#include "xil_printf.h"
#include "xilrsa.h"
#include "rsa_auth_app.h"
#include "xil_cache.h"

/************************** Constant Definitions *****************************/

/**************************** Type Definitions *******************************/

/***************** Macros (Inline Functions) Definitions *********************/

/************************** Function Prototypes ******************************/

int AuthenticatePartition(u8 *Buffer, u32 Size, u8 *CertStart);
void SetPpk(u8 *CertStart);
int RecreatePaddingAndCheck(u8 *signature, u8 *hash);

/************************** Variable Definitions *****************************/

static u8 *PpkModular;
static u8 *PpkModularEx;
static u32 PpkExp;


/*****************************************************************************/
/**
*
* Main function to call the AuthenticaApp function.
*
* @param	None
*
* @return	XST_SUCCESS if authentication was successful.
*           XST_FAILURE if authentication failed
*
* @note		None
*
******************************************************************************/
int main(void)
{
	int Status = XST_FAILURE;

	Xil_DCacheFlush();

	xil_printf("RSA authentication of application started \n\r");

	Status = AuthenticateApp();
	if (Status != XST_SUCCESS) {
		xil_printf("RSA authentication of SW application failed\n\r");
		goto END;
	}

	xil_printf("Successfully authenticated SW application \n\r");
END:
	return Status;
}

/*****************************************************************************/
/**
*
* This function authenticates the SW application given by the user
*
* @param	None
*
* @return	XST_SUCCESS if authentication was successful.
*           XST_FAILURE if authentication failed
*
* @note		None
*
******************************************************************************/
int AuthenticateApp(void)
{
	/*
	 * Set the Ppk
	 */
	SetPpk((u8 *)CERTIFICATE_START_ADDR);

	/*
	 * Authenticate partition containing the application.
	 */
	return AuthenticatePartition((u8 *)APPLICATION_START_ADDR,
			PARTITION_SIZE, (u8 *)CERTIFICATE_START_ADDR);
}

/*****************************************************************************/
/**
*
* This function is used to set ppk pointer to ppk in OCM
*
* @param	CertStart Pointer to buffer which holds the starting address of
*                     authentication certificate provided by user
*
* @return   None
*
* @note		None
*
******************************************************************************/

void SetPpk(u8 *CertStart)
{
	u8 *PpkPtr;

	/*
	 * Set PpkPtr to PPK start address provided by user
	 */
	PpkPtr = (u8 *)(CertStart);

	/*
	 * Increment the pointer by authentication Header size
	 */
	PpkPtr += RSA_HEADER_SIZE;

	/*
	 * Increment the pointer by Magic word size
	 */
	PpkPtr += RSA_MAGIC_WORD_SIZE;

	/*
	 * Set pointer to PPK
	 */
	PpkModular = (u8 *)PpkPtr;
	PpkPtr += RSA_PPK_MODULAR_SIZE;
	PpkModularEx = (u8 *)PpkPtr;
	PpkPtr += RSA_PPK_MODULAR_EXT_SIZE;
	PpkExp = *((u32 *)PpkPtr);

	return;
}

/*****************************************************************************/
/**
*
* This function authenticates the partition signature
*
* @param	Buffer    Pointer which holds the address of the partition
*                     data which needs to be authenticated
*           Size      size of the partition
*           CertStart Pointer to buffer which holds the starting address of
*                     authentication certificate provided by user
*
* @return   XST_SUCCESS if Authentication passed
*		    XST_FAILURE if Authentication failed
*
* @note		None
*
******************************************************************************/
int AuthenticatePartition(u8 *Buffer, u32 Size, u8 *CertStart)
{
	int Status = XST_FAILURE;
	u8 DecryptSignature[RSA_PARTITION_SIGNATURE_SIZE];
	u8 HashSignature[HASHLEN];
	u8 *SpkModular;
	u8 *SpkModularEx;
	u32 SpkExp;
	u8 *SignaturePtr;

	/*
	 * Point to Authentication Certificate
	 */
	SignaturePtr = (u8 *)(CertStart);

	/*
	 * Increment the pointer by authentication Header size
	 */
	SignaturePtr += RSA_HEADER_SIZE;

	/*
	 * Increment the pointer by Magic word size
	 */
	SignaturePtr += RSA_MAGIC_WORD_SIZE;

	/*
	 * Increment the pointer beyond the PPK
	 */
	SignaturePtr += RSA_PPK_MODULAR_SIZE;
	SignaturePtr += RSA_PPK_MODULAR_EXT_SIZE;
	SignaturePtr += RSA_PPK_EXPO_SIZE;

	/*
	 * Calculate Hash Signature
	 */
	sha_256((u8 *)SignaturePtr, (RSA_SPK_MODULAR_EXT_SIZE +
				RSA_SPK_EXPO_SIZE + RSA_SPK_MODULAR_SIZE),
				HashSignature);

   	/*
   	 * Extract SPK signature
   	 */
	SpkModular = (u8 *)SignaturePtr;
	SignaturePtr += RSA_SPK_MODULAR_SIZE;
	SpkModularEx = (u8 *)SignaturePtr;
	SignaturePtr += RSA_SPK_MODULAR_EXT_SIZE;
	SpkExp = *((u32 *)SignaturePtr);
	SignaturePtr += RSA_SPK_EXPO_SIZE;

	/*
	 * Decrypt SPK Signature
	 */
	rsa2048_pubexp((RSA_NUMBER)DecryptSignature,
			(RSA_NUMBER)SignaturePtr,
			(u32)PpkExp,
			(RSA_NUMBER)PpkModular,
			(RSA_NUMBER)PpkModularEx);

	Status = RecreatePaddingAndCheck(DecryptSignature, HashSignature);
	if (Status != XST_SUCCESS) {
		goto END;
	}
	SignaturePtr += RSA_SPK_SIGNATURE_SIZE;

	/*
	 * Decrypt Partition Signature
	 */
	rsa2048_pubexp((RSA_NUMBER)DecryptSignature,
			(RSA_NUMBER)SignaturePtr,
			(u32)SpkExp,
			(RSA_NUMBER)SpkModular,
			(RSA_NUMBER)SpkModularEx);

	/*
	 * Partition Authentication
	 * Calculate Hash Signature
	 */
	sha_256((u8 *)Buffer, (Size - RSA_PARTITION_SIGNATURE_SIZE),
			HashSignature);

	Status = RecreatePaddingAndCheck(DecryptSignature, HashSignature);
END:
	return Status;
}

/*****************************************************************************/
/**
*
* This function recreates and checks the signature.
*
* @param	signature Partition signature
* @param	hash      Partition hash value which includes boot header,
*                     partition data
*
* @return   XST_SUCCESS signature verification passed
*		    XST_FAILURE signature verification failed
*
* @note		None
*
******************************************************************************/
int RecreatePaddingAndCheck(u8 *signature, u8 *hash)
{
	int Status = XST_FAILURE;
	u8 T_padding[] = {0x30, 0x31, 0x30, 0x0D, 0x06, 0x09, 0x60, 0x86, 0x48,
		0x01, 0x65, 0x03, 0x04, 0x02, 0x01, 0x05, 0x00, 0x04, 0x20 };
    u8 * pad_ptr = signature + RSA_PARTITION_SIGNATURE_SIZE;
    u32 padlen;
    u32 ii;

	padlen = RSA_PARTITION_SIGNATURE_SIZE - RSA_BYTE_PAD_LENGTH -
				RSA_T_PAD_LENGTH - HASHLEN;
    /*
    * Re-Create PKCS#1v1.5 Padding
    * MSB  ----------------------------------------------------LSB
    * 0x0 || 0x1 || 0xFF(for 202 bytes) || 0x0 || T_padding || SHA256 Hash
    */
    if (*--pad_ptr != 0x00U) {
	goto END;
    }

	if (*--pad_ptr != 0x01U) {
		goto END;
	}

    for (ii = 0U; ii < padlen; ii++) {
	if (*--pad_ptr != 0xFFU) {
		goto END;
        }
    }

    if (*--pad_ptr != 0x00U) {
	goto END;
    }

    for (ii = 0U; ii < sizeof(T_padding); ii++) {
	if (*--pad_ptr != T_padding[ii]) {
		goto END;
        }
    }

    for (ii = 0U; ii < HASHLEN; ii++) {
	if (*--pad_ptr != hash[ii]) {
		goto END;
	}
    }
	Status= (u32)XST_SUCCESS;
END:
	return Status;
}
