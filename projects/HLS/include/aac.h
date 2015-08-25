/*
@author:chenzhengqiang
@start date:2015/8/25
@modified date:
@desc:aac media file related,obtain the adts header, and read a frame from aac file and so on
*/

#ifndef _CZQ_AAC_H_
#define _CZQ_AAC_H_
#include "ts.h"
#include<cstdio>

static const int ADTS_HEADER_LENGTH = 7;

typedef struct
{
	unsigned int syncword; 
       unsigned int id;
	unsigned int layer;    
	unsigned int protection_absent;
	unsigned int profile; 
	unsigned int sf_index;
	unsigned int private_bit;
	unsigned int channel_configuration;
	unsigned int original;          
	unsigned int home;            

    
	unsigned int copyright_identification_bit;  
	unsigned int copyright_identification_start;
	unsigned int aac_frame_length;               // adts header length + aac frame's raw length
	unsigned int adts_buffer_fullness;           
	/*no_raw_data_blocks_in_frame ��ʾADTS֡����number_of_raw_data_blocks_in_frame + 1��AACԭʼ֡.
	����˵number_of_raw_data_blocks_in_frame == 0 
	��ʾ˵ADTS֡����һ��AAC���ݿ鲢����˵û�С�(һ��AACԭʼ֡����һ��ʱ����1024���������������)
    */
	unsigned int no_raw_data_blocks_in_frame;
} ADTS_HEADER;

int obtain_aac_adts_header(FILE *faac_handler,ADTS_HEADER & adts_header);                
int read_aac_frame( FILE *faac_handler,unsigned char * aac_frame, unsigned int & frame_length );                         
#endif