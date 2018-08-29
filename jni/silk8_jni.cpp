#include <jni.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

extern "C"{
	#include"Decoder.h"
	#include"Encoder.h"
}
/* Define codec specific settings */
#define MAX_BYTES_ENC_PER_FRAME     250 // Equals peak bitrate of 100 kbps 
#define MAX_BYTES_DEC_PER_FRAME     1024

#define MAX_INPUT_FRAMES        5
#define MAX_LBRR_DELAY          2
#define MAX_FRAME_LENGTH        480

#define	MAX_FRAME			160

#include <android/log.h> 

#define LOG_TAG "silk" // text for log tag 

#include "SKP_Silk_SDK_API.h"
#include "SKP_Silk_SigProc_FIX.h"

#undef DEBUG_SILK8

// the header length of the RTP frame (must skip when en/decoding)
#define	RTP_HDR_SIZE	12

static int codec_open = 0;

static JavaVM *gJavaVM;
const char *kInterfacePath = "org/sipdroid/pjlib/silk8";

/* encoder parameters */

    SKP_int32 encSizeBytes;
    void      *psEnc;

    /* default settings */
    SKP_int   fs_kHz = 8;
    SKP_int   targetRate_bps = 20000;
    SKP_int   packetSize_ms = 20;
    SKP_int   frameSizeReadFromFile_ms = 20;
    SKP_int   packetLoss_perc = 0, smplsSinceLastPacket;
    SKP_int   INBandFec_enabled = 0, DTX_enabled = 0, quiet = 0;
    SKP_SILK_SDK_EncControlStruct encControl; // Struct for input to encoder
        

/* decoder parameters */

    jbyte payloadToDec[    MAX_BYTES_DEC_PER_FRAME * MAX_INPUT_FRAMES * ( MAX_LBRR_DELAY + 1 ) ];
    jshort out[ ( MAX_FRAME_LENGTH << 1 ) * MAX_INPUT_FRAMES ], *outPtr;
    SKP_int32 decSizeBytes;
    void      *psDec;
    SKP_SILK_SDK_DecControlStruct DecControl;

char* jstringTostring(JNIEnv* env, jstring jstr) {
	char* rtn = NULL;
	jclass clsstring = (env)->FindClass("java/lang/String");
	jstring strencode = (env)->NewStringUTF("utf-8");
	jmethodID mid = (env)->GetMethodID(clsstring, "getBytes",
			"(Ljava/lang/String;)[B");
	jbyteArray barr = (jbyteArray)(env)->CallObjectMethod(jstr, mid,
			strencode);
	jsize alen = (env)->GetArrayLength(barr);
	jbyte* ba = (env)->GetByteArrayElements(barr, JNI_FALSE);
	if (alen > 0) {
		rtn = (char*) malloc(alen + 1);
		memcpy(rtn, ba, alen);
		rtn[alen] = 0;
	}
	(env)->ReleaseByteArrayElements(barr, ba, 0);
	return rtn;
}
	
	
extern "C"
JNIEXPORT jint JNICALL Java_me_hxert_volice_volice_util_JNI_open
  (JNIEnv *env, jobject obj, jint compression) {
	int ret;

	if (codec_open++ != 0)
		return (jint)0;

	/* Set the samplingrate that is requested for the output */
    DecControl.API_sampleRate = 8000;
		
    /* Create decoder */
    ret = SKP_Silk_SDK_Get_Decoder_Size( &decSizeBytes );
    if( ret ) {
		__android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, 
            "\n!!!!!!!! SKP_Silk_SDK_Get_Decoder_Size returned %d", ret );		
    }
#ifdef DEBUG_SILK8
    __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, 
            "### INIT Decoder decSizeBytes = %d\n", decSizeBytes); 		
#endif	
    psDec = malloc( decSizeBytes );

    /* Reset decoder */
    ret = SKP_Silk_SDK_InitDecoder( psDec );
    if( ret ) {
		__android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, 
            "\n!!!!!!!! SKP_Silk_InitDecoder returned %d", ret );	
    }


    /* Create Encoder */
    ret = SKP_Silk_SDK_Get_Encoder_Size( &encSizeBytes );
    if( ret ) {
		__android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, 
            "\n!!!!!!!! SKP_Silk_SDK_Get_Encoder_Size returned %d", ret );	
    }
#ifdef DEBUG_SILK8
    __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, 
            "### INIT Encoder encSizeBytes = %d\n", encSizeBytes); 		
#endif		
    psEnc = malloc( encSizeBytes );
    
    /* Reset Encoder */
    ret = SKP_Silk_SDK_InitEncoder( psEnc, &encControl );
    if( ret ) {
		__android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, 
            "\n!!!!!!!! SKP_Silk_SDK_InitEncoder returned %d", ret );	
	}
    
    /* Set Encoder parameters */
    encControl.API_sampleRate       = fs_kHz * 1000;
	encControl.maxInternalSampleRate = 8000;
    encControl.packetSize           = packetSize_ms * fs_kHz;
    encControl.packetLossPercentage = packetLoss_perc;
    encControl.useInBandFEC         = INBandFec_enabled;
    encControl.useDTX               = DTX_enabled;
    encControl.complexity           = compression;
    encControl.bitRate              = targetRate_bps;		
	
	return (jint)0;
}

void Print_Decode_Error_Msg(int errcode) {
	switch (errcode) {
		case SKP_SILK_DEC_INVALID_SAMPLING_FREQUENCY:
			__android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, 
				"!!!!!!!!!!! Decode_Error_Message: %d\nOutput sampling frequency lower than internal decoded sampling frequency\n", errcode);
			break;
		case SKP_SILK_DEC_PAYLOAD_TOO_LARGE:
			__android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, 
				"!!!!!!!!!!! Decode_Error_Message: %d\nPayload size exceeded the maximum allowed 1024 bytes\n", errcode); 
			break;
		case SKP_SILK_DEC_PAYLOAD_ERROR:
			__android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, 
				"!!!!!!!!!!! Decode_Error_Message: %d\nPayload has bit errors\n", errcode); 
			break;			
	}
}

void Print_Encode_Error_Msg(int errcode) {
	switch (errcode) {
		case SKP_SILK_ENC_INPUT_INVALID_NO_OF_SAMPLES:
			__android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, 
				"!!!!!!!!!!! Decode_Error_Message: %d\nInput length is not a multiplum of 10 ms, or length is longer than the packet length\n", errcode);
			break;
		case SKP_SILK_ENC_FS_NOT_SUPPORTED:
			__android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, 
				"!!!!!!!!!!! Decode_Error_Message: %d\nSampling frequency not 8000, 12000, 16000 or 24000 Hertz \n", errcode); 
			break;
		case SKP_SILK_ENC_PACKET_SIZE_NOT_SUPPORTED:
			__android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, 
				"!!!!!!!!!!! Decode_Error_Message: %d\nPacket size not 20, 40, 60, 80 or 100 ms\n", errcode); 
			break;			
		case SKP_SILK_ENC_PAYLOAD_BUF_TOO_SHORT:
			__android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, 
				"!!!!!!!!!!! Decode_Error_Message: %d\nAllocated payload buffer too short \n", errcode);
			break;
		case SKP_SILK_ENC_INVALID_LOSS_RATE:
			__android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, 
				"!!!!!!!!!!! Decode_Error_Message: %d\nLoss rate not between 0 and 100 percent\n", errcode); 
			break;
		case SKP_SILK_ENC_INVALID_COMPLEXITY_SETTING:
			__android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, 
				"!!!!!!!!!!! Decode_Error_Message: %d\nComplexity setting not valid, use 0, 1 or 2\n", errcode); 
			break;		
		case SKP_SILK_ENC_INVALID_INBAND_FEC_SETTING:
			__android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, 
				"!!!!!!!!!!! Decode_Error_Message: %d\nInband FEC setting not valid, use 0 or 1\n", errcode);
			break;
		case SKP_SILK_ENC_INVALID_DTX_SETTING:
			__android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, 
				"!!!!!!!!!!! Decode_Error_Message: %d\nDTX setting not valid, use 0 or 1\n", errcode); 
			break;
		case SKP_SILK_ENC_INTERNAL_ERROR:
			__android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, 
				"!!!!!!!!!!! Decode_Error_Message: %d\nInternal encoder error\n", errcode); 
			break;				
	}
}

extern "C"
JNIEXPORT jint JNICALL Java_me_hxert_volice_volice_util_JNI_encode
    (JNIEnv *env, jobject obj, jstring injs, jstring outjs) {
	__android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, 
		"\ndecode: %d",0);		
	char* in;
	char* out;
	char* argv[3];
	int result = -1;

	in = jstringTostring(env, injs);
	__android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, 
	"\nINJS: ",injs);
	__android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, 
	"\nOUTJS: ",outjs);

	
	out = jstringTostring(env, outjs);
	
		__android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, 
	"\nIN: ",in);
	__android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, 
	"\nOUT: ",out);
	
	__android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, 
		"\ndecode: %d",1);	
	argv[0] = "";
	argv[1] = in;
	argv[2] = out;
	
	__android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, 
		"\nin: %s",argv[1]);
		
	__android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, 
		"\nout: %s",argv[2]);

	result = encode(3, argv);
	free(in);
	free(out);
	return result;
}

extern "C"
JNIEXPORT jint JNICALL Java_me_hxert_volice_volice_util_JNI_decode
    (JNIEnv *env, jobject obj, jstring injs, jstring outjs, jint offset) {
		__android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, 
            "\ndecode: %d",0);		
		char* in;
		char* out;
		char* argv[3];
		int result = -1;

		in = jstringTostring(env, injs);
		out = jstringTostring(env, outjs);
		__android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, 
            "\ndecode: %d",1);	
		argv[0] = "";
		argv[1] = in;
		argv[2] = out;

		__android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, 
		"\nin: ",argv[1]);
		
		__android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, 
		"\nout: ",argv[2]);
		
		result = decode(3, argv, (int) offset);
		free(in);
		free(out);
		return result;
}

extern "C"
JNIEXPORT void JNICALL Java_me_hxert_volice_volice_util_JNI_close
    (JNIEnv *env, jobject obj) {

	if (--codec_open != 0)
		return;
    /* Free decoder */
    free( psDec );
    /* Free Encoder */
    free( psEnc );
}

extern "C"
JNIEXPORT jstring JNICALL Java_me_hxert_volice_volice_util_JNI_version
	(JNIEnv *env, jobject obj) {
		
		return (env)->NewStringUTF("silk-v3 On Jul 24, 2018");
	}