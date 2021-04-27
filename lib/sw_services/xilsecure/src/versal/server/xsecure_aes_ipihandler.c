/******************************************************************************
* Copyright (c) 2021 Xilinx, Inc.  All rights reserved.
* SPDX-License-Identifier: MIT
******************************************************************************/

/*****************************************************************************/
/**
*
* @file xsecure_aes_ipihandler.c
* @addtogroup xsecure_apis XilSecure Versal AES handler APIs
* @{
* @cond xsecure_internal
* This file contains the xilsecure AES IPI handlers implementation.
*
* <pre>
* MODIFICATION HISTORY:
*
* Ver   Who  Date        Changes
* ----- ---- -------- -------------------------------------------------------
* 1.00  kal   03/04/2021 Initial release
*
* </pre>
*
* @note
* @endcond
*
******************************************************************************/

/***************************** Include Files *********************************/
#include "xplmi_dma.h"
#include "xsecure_aes.h"
#include "xsecure_aes_ipihandler.h"
#include "xsecure_defs.h"
#include "xil_util.h"

/************************** Constant Definitions *****************************/

static XSecure_Aes Secure_Aes;
static XPmcDma_Config *Config;
static XPmcDma PmcDmaInstance;

#define XSECURE_AES_DEC_KEY_SRC_MASK	0x000000FFU
#define XSECURE_AES_DST_KEY_SRC_MASK	0x0000FF00U
#define XSECURE_AES_KEY_SIZE_MASK	0xFFFF0000U
#define XSECURE_PMCDMA_DEVICEID		PMCDMA_0_DEVICE_ID

/************************** Function Prototypes *****************************/
static int XSecure_AesInit(void);
static int XSecure_AesOpInit(u32 SrcAddrLow, u32 SrcAddrHigh);
static int XSecure_AesAadUpdate(u32 SrcAddrLow, u32 SrcAddrHigh, u32 Size);
static int XSecure_AesEncUpdate(u32 SrcAddrLow, u32 SrcAddrHigh,
	u32 DstAddrLow, u32 DstAddrHigh);
static int XSecure_AesEncFinal(u32 DstAddrLow, u32 DstAddrHigh);
static int XSecure_AesDecUpdate(u32 SrcAddrLow, u32 SrcAddrHigh,
	u32 DstAddrLow, u32 DstAddrHigh);
static int XSecure_AesDecFinal(u32 SrcAddrLow, u32 SrcAddrHigh);
static int XSecure_AesKeyZeroize(u32 KeySrc);
static int XSecure_AesKeyWrite(u8  KeySize, u8 KeySrc,
	u32 KeyAddrLow, u32 KeyAddrHigh);
static int XSecure_AesDecryptKek(u32 KeyInfo, u32 IvAddrLow, u32 IvAddrHigh);
static int XSecure_AesSetDpaCmConfig(u8 DpaCmCfg);
static int XSecure_AesExecuteDecKat(void);
static int XSecure_AesExecuteDecCmKat(void);

/*****************************************************************************/
/**
 * @brief       This function calls respective IPI handler based on the API_ID
 *
 * @param 	Cmd is pointer to the command structure
 *
 * @return	- XST_SUCCESS - If the handler execution is successful
 * 		- ErrorCode - If there is a failure
 *
 ******************************************************************************/
int XSecure_AesIpiHandler(XPlmi_Cmd *Cmd)
{
	volatile int Status = XST_FAILURE;
	u32 *Pload = Cmd->Payload;

	switch (Cmd->CmdId & 0xFFU) {
	case XSECURE_API(XSECURE_API_AES_INIT):
		Status = XSecure_AesInit();
		break;
	case XSECURE_API(XSECURE_API_AES_OP_INIT):
		Status = XSecure_AesOpInit(Pload[0], Pload[1]);
		break;
	case XSECURE_API(XSECURE_API_AES_UPDATE_AAD):
		Status = XSecure_AesAadUpdate(Pload[0], Pload[1], Pload[2]);
		break;
	case XSECURE_API(XSECURE_API_AES_ENCRYPT_UPDATE):
		Status = XSecure_AesEncUpdate(Pload[0], Pload[1], Pload[2],
				Pload[3]);
		break;
	case XSECURE_API(XSECURE_API_AES_ENCRYPT_FINAL):
		Status = XSecure_AesEncFinal(Pload[0], Pload[1]);
		break;
	case XSECURE_API(XSECURE_API_AES_DECRYPT_UPDATE):
		Status = XSecure_AesDecUpdate(Pload[0], Pload[1], Pload[2],
				Pload[3]);
		break;
	case XSECURE_API(XSECURE_API_AES_DECRYPT_FINAL):
		Status = XSecure_AesDecFinal(Pload[0], Pload[1]);
		break;
	case XSECURE_API(XSECURE_API_AES_KEY_ZERO):
		Status = XSecure_AesKeyZeroize(Pload[0]);
		break;
	case XSECURE_API(XSECURE_API_AES_WRITE_KEY):
		Status = XSecure_AesKeyWrite(Pload[0], Pload[1], Pload[2],
				Pload[3]);
		break;
	case XSECURE_API(XSECURE_API_AES_KEK_DECRYPT):
		Status = XSecure_AesDecryptKek(Pload[0], Pload[1], Pload[2]);
		break;
	case XSECURE_API(XSECURE_API_AES_SET_DPA_CM):
		Status = XSecure_AesSetDpaCmConfig(Pload[0]);
		break;
	case XSECURE_API(XSECURE_API_AES_DECRYPT_KAT):
		Status = XSecure_AesExecuteDecKat();
		break;
	case XSECURE_API(XSECURE_API_AES_DECRYPT_CM_KAT):
		Status = XSecure_AesExecuteDecCmKat();
		break;
	default:
		XSecure_Printf(XSECURE_DEBUG_GENERAL, "CMD: INVALID PARAM\r\n");
		Status = XST_INVALID_PARAM;
		break;
	}

	return Status;
}

/*****************************************************************************/
/**
 * @brief       This function handler calls XSecure_AesInitialize Server API
 *
 * @return	- XST_SUCCESS - If the initialization is successful
 * 		- ErrorCode - If there is a failure
 *
 ******************************************************************************/
static int XSecure_AesInit(void)
{
	volatile int Status = XST_FAILURE;

	/* Initialize PMC DMA driver */
	Config = XPmcDma_LookupConfig(XSECURE_PMCDMA_DEVICEID);
	if (NULL == Config) {
		goto END;
	}

	Status = XPmcDma_CfgInitialize(&PmcDmaInstance, Config,
					Config->BaseAddress);
	if (Status != XST_SUCCESS) {
		goto END;
	}

	/* Initialize the Aes driver so that it's ready to use */
	Status = XSecure_AesInitialize(&Secure_Aes, &PmcDmaInstance);

END:
	return Status;
}

/*****************************************************************************/
/**
 * @brief       This function handler calls XSecure_AesEncryptInit or
 * 		XSecure_AesDecryptInit server API based on the Operation type
 *
 * @param	SrcAddrLow	- Lower 32 bit address of the XSecure_AesInitOps
 * 				structure.
 * 		SrcAddrHigh	- Higher 32 bit address of the XSecure_AesInitOps
 * 				structure.
 *
 * @return	- XST_SUCCESS - If the initialization is successful
 * 		- ErrorCode - If there is a failure
 *
 ******************************************************************************/
static int XSecure_AesOpInit(u32 SrcAddrLow, u32 SrcAddrHigh)
{
	volatile int Status = XST_FAILURE;
	u64 Addr = ((u64)SrcAddrHigh << 32U) | (u64)SrcAddrLow;
	XSecure_AesInitOps AesParams;

	Status = XPlmi_DmaXfr(Addr, (UINTPTR)&AesParams, sizeof(AesParams),
			XPLMI_PMCDMA_0);
	if (Status != XST_SUCCESS) {
		goto END;
	}

	if (AesParams.OperationId == XSECURE_ENCRYPT) {
		Status = XSecure_AesEncryptInit(&Secure_Aes, AesParams.KeySrc,
				AesParams.KeySize, AesParams.IvAddr);
	}
	else {
		Status = XSecure_AesDecryptInit(&Secure_Aes, AesParams.KeySrc,
				AesParams.KeySize, AesParams.IvAddr);
	}

END:
	return Status;
}

/*****************************************************************************/
/**
 * @brief       This function handler calls XSecure_AesUpdateAad server API
 *
 * @param	SrcAddrLow	- Lower 32 bit address of the AAD data
 * 		SrcAddrHigh	- Higher 32 bit address of the AAD data
 *		Size		- AAD Size
 *
 * @return	- XST_SUCCESS - If the encrypt update is successful
 * 		- ErrorCode - If there is a failure
 *
 ******************************************************************************/
static int XSecure_AesAadUpdate(u32 SrcAddrLow, u32 SrcAddrHigh, u32 Size)
{
	volatile int Status = XST_FAILURE;
	u64 Addr = ((u64)SrcAddrHigh << 32U) | (u64)SrcAddrLow;

	Status = XSecure_AesUpdateAad(&Secure_Aes, Addr, Size);

	return Status;
}

/*****************************************************************************/
/**
 * @brief       This function handler calls XSecure_AesEncryptUpdate server API
 *
 * @param	SrcAddrLow	- Lower 32 bit address of the
 * 				XSecure_AesInParams structure.
 * 		SrcAddrHigh	- Higher 32 bit address of the
 * 				XSecure_AesInParams structure.
 * 		DstAddrLow	- Lower 32 bit address of the Output buffer
 * 				where encrypted data to be stored
 * 		DstAddrHigh	- Higher 32 bit address of the output buffer
 * 				where encrypted data to be stored
 *
 * @return	- XST_SUCCESS - If the encrypt update is successful
 * 		- ErrorCode - If there is a failure
 *
 ******************************************************************************/
static int XSecure_AesEncUpdate(u32 SrcAddrLow, u32 SrcAddrHigh,
				u32 DstAddrLow, u32 DstAddrHigh)
{
	volatile int Status = XST_FAILURE;
	u64 Addr = ((u64)SrcAddrHigh << 32U) | (u64)SrcAddrLow;
	u64 DstAddr = ((u64)DstAddrHigh << 32U) | (u64)DstAddrLow;
	XSecure_AesInParams InParams;

	Status = XPlmi_DmaXfr(Addr, (UINTPTR)&InParams, sizeof(InParams),
			XPLMI_PMCDMA_0);
	if (Status != XST_SUCCESS) {
		goto END;
	}

	Status = XSecure_AesEncryptUpdate(&Secure_Aes, InParams.InDataAddr,
				DstAddr, InParams.Size, InParams.IsLast);
END:
	return Status;
}

/*****************************************************************************/
/**
 * @brief       This function handler calls XSecure_AesEncryptFinal server API
 *
 * @param	DstAddrLow	- Lower 32 bit address of the GCM-TAG
 * 				to be stored.
 * 		DstAddrHigh	- Higher 32 bit address of the GCM-TAG
 * 				to be stored.
 *
 * @return	- XST_SUCCESS - If the encrypt final is successful
 * 		- ErrorCode - If there is a failure
 *
 ******************************************************************************/
static int XSecure_AesEncFinal(u32 DstAddrLow, u32 DstAddrHigh)
{
	volatile int Status = XST_FAILURE;
	u64 Addr = ((u64)DstAddrHigh << 32U) | (u64)DstAddrLow;

	Status = XSecure_AesEncryptFinal(&Secure_Aes, Addr);

	return Status;
}

/*****************************************************************************/
/**
 * @brief       This function handler calls XSecure_AesDecryptUpdate server API
 *
 * @param	SrcAddrLow	- Lower 32 bit address of the
 * 				XSecure_AesInParams structure.
 * 		SrcAddrHigh	- Higher 32 bit address of the
 * 				XSecure_AesInParams structure.
 * 		DstAddrLow	- Lower 32 bit address of the Output buffer
 * 				where decrypted data to be stored
 * 		DstAddrHigh	- Higher 32 bit address of the output buffer
 * 				where decrypted data to be stored
 *
 * @return	- XST_SUCCESS - If the decrypt update is successful
 * 		- ErrorCode - If there is a failure
 *
 ******************************************************************************/
static int XSecure_AesDecUpdate(u32 SrcAddrLow, u32 SrcAddrHigh,
				u32 DstAddrLow, u32 DstAddrHigh)
{
	volatile int Status = XST_FAILURE;
	u64 Addr = ((u64)SrcAddrHigh << 32U) | (u64)SrcAddrLow;
	u64 DstAddr = ((u64)DstAddrHigh << 32U) | (u64)DstAddrLow;
	XSecure_AesInParams InParams;

	Status = XPlmi_DmaXfr(Addr, (UINTPTR)&InParams, sizeof(InParams),
			XPLMI_PMCDMA_0);
	if (Status != XST_SUCCESS) {
		goto END;
	}

	Status = XSecure_AesDecryptUpdate(&Secure_Aes, InParams.InDataAddr,
				DstAddr, InParams.Size, InParams.IsLast);

END:
	return Status;
}

/*****************************************************************************/
/**
 * @brief       This function handler calls XSecure_AesDecryptFinal server API
 *
 * @param	SrcAddrLow	- Lower 32 bit address of the GCM-TAG
 * 		SrcAddrHigh	- Higher 32 bit address of the GCM-TAG
 *
 * @return	- XST_SUCCESS - If the decrypt final is successful
 * 		- ErrorCode - If there is a failure
 *
 ******************************************************************************/
static int XSecure_AesDecFinal(u32 SrcAddrLow, u32 SrcAddrHigh)
{
	volatile int Status = XST_FAILURE;
	u64 Addr = ((u64)SrcAddrHigh << 32U) | (u64)SrcAddrLow;

	Status = XSecure_AesDecryptFinal(&Secure_Aes, Addr);

	return Status;
}

/*****************************************************************************/
/**
 * @brief       This function handler calls XSecure_AesKeyZero server API
 *
 * @param	KeySrc	- Key source to be zeroized
 *
 * @return	- XST_SUCCESS - If the key zeroize is successful
 * 		- ErrorCode - If there is a failure
 *
 ******************************************************************************/
static int XSecure_AesKeyZeroize(u32 KeySrc)
{
	volatile int Status = XST_FAILURE;

	Status = XSecure_AesKeyZero(&Secure_Aes, (XSecure_AesKeySrc)KeySrc);

	return Status;
}

/*****************************************************************************/
/**
 * @brief       This function handler calls XSecure_AesWriteKey server API
 *
 * @param	KeySize		- Size of the key to specify 128/256 bit key
 *		KeySrc		- KeySrc to which key has to be written
 * 		KeyAddrLow	- Lower 32 bit address of the Key
 * 		KeyAddrHigh	- Higher 32 bit address of the Key
 *
 * @return	- XST_SUCCESS - If the key write is successful
 * 		- ErrorCode - If there is a failure
 *
 ******************************************************************************/
static int XSecure_AesKeyWrite(u8  KeySize, u8 KeySrc,
			u32 KeyAddrLow, u32 KeyAddrHigh)
{
	volatile int Status = XST_FAILURE;
	u64 KeyAddr = ((u64)KeyAddrHigh << 32U) | (u64)KeyAddrLow;

	Status = XSecure_AesWriteKey(&Secure_Aes, KeySrc,
				(XSecure_AesKeySize)KeySize, KeyAddr);
	return Status;
}

/*****************************************************************************/
/**
 * @brief       This function handler calls XSecure_AesDecryptKek server API
 *
 * @param	KeyInfo		- KeyInfo contains KeySize, KeyDst and KeySrc
 * 		IvAddrLow	- Lower 32 bit address of the IV
 * 		IvAddrHigh	- Higher 32 bit address of the IV
 *
 * @return	- XST_SUCCESS - If the decryption is successful
 * 		- ErrorCode - If there is a failure
 *
 ******************************************************************************/
static int XSecure_AesDecryptKek(u32 KeyInfo, u32 IvAddrLow, u32 IvAddrHigh)
{
	volatile int Status = XST_FAILURE;
	u64 IvAddr = ((u64)IvAddrHigh << 32U) | (u64)IvAddrLow;
	XSecure_AesKeySrc DecKeySrc = KeyInfo & XSECURE_AES_DEC_KEY_SRC_MASK;
	XSecure_AesKeySrc DstKeySrc = KeyInfo & XSECURE_AES_DST_KEY_SRC_MASK;
	XSecure_AesKeySize KeySize = KeyInfo & XSECURE_AES_KEY_SIZE_MASK;

	Status = XSecure_AesKekDecrypt(&Secure_Aes, DecKeySrc, DstKeySrc,
				IvAddr, KeySize);
	return Status;
}

/*****************************************************************************/
/**
 * @brief       This function handler calls XSecure_AesSetDpaCm server API
 *
 * @param	DpaCmCfg	- User DpaCmCfg configuration
 *
 * @return	- XST_SUCCESS - If the Set DpaCm is successful
 * 		- ErrorCode - If there is a failure
 *
 ******************************************************************************/
static int XSecure_AesSetDpaCmConfig(u8 DpaCmCfg)
{
	volatile int Status = XST_FAILURE;

	Status = XSecure_AesSetDpaCm(&Secure_Aes, DpaCmCfg);

	return Status;
}

/*****************************************************************************/
/**
 * @brief       This function handler calls XSecure_AesDecryptKat server API
 *
 * @return	- XST_SUCCESS - If the KAT is successful
 * 		- ErrorCode - If there is a failure
 *
 ******************************************************************************/
static int XSecure_AesExecuteDecKat(void)
{
	volatile int Status = XST_FAILURE;

	Status = XSecure_AesDecryptKat(&Secure_Aes);

	return Status;
}

/*****************************************************************************/
/**
 * @brief       This function handler calls XSecure_AesExecuteDecCmKat
 * 		server API
 *
 * @return	- XST_SUCCESS - If the KAT is successful
 * 		- XST_FAILURE - If there is a failure
 *
 ******************************************************************************/
static int XSecure_AesExecuteDecCmKat(void)
{
	volatile int Status = XST_FAILURE;

	Status = XSecure_AesDecryptCmKat(&Secure_Aes);

	return Status;
}