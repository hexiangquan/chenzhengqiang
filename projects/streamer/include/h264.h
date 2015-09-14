/*
@file name:h264.h
@author:chenzhengqiang
@start date:2015/9/13
@modified date:
@desc:providing the api for parsing h264 file
*/

#ifndef _CZQ_H264_H_
#define _CZQ_H264_H_

#include "ts.h"
#include "my_bs.h"
#include <cstdio>
#include <stdint.h>
#define  MAX_VIDEO_TAG_BUF_SIZE   1024 * 1024
#define  VIDEO_TAG_HEADER_LENGTH  11

//H264һ֡���ݵĽṹ��
typedef struct Tag_NALU_t
{
	unsigned char forbidden_bit;           //! Should always be FALSE
	unsigned char nal_reference_idc;       //! NALU_PRIORITY_xxxx
	unsigned char nal_unit_type;           //! NALU_TYPE_xxxx  
	unsigned int  startcodeprefix_len;      //! ǰ׺�ֽ���
	unsigned int  len;                     //! ����nal ͷ��nal ���ȣ��ӵ�һ��00000001����һ��000000001�ĳ���
	unsigned int  max_size;                //! ����һ��nal �ĳ���
	unsigned char * buf;                   //! ����nal ͷ��nal ����
	unsigned char Frametype;               //! ֡����
	unsigned int  lost_packets;            //! Ԥ��
} NALU_t;

//nal����
enum nal_unit_type_e
{
	NAL_UNKNOWN     = 0,
	NAL_SLICE       = 1,
	NAL_SLICE_DPA   = 2,
	NAL_SLICE_DPB   = 3,
	NAL_SLICE_DPC   = 4,
	NAL_SLICE_IDR   = 5,    /* ref_idc != 0 */
	NAL_SEI         = 6,    /* ref_idc == 0 */
	NAL_SPS         = 7,
	NAL_PPS         = 8
	/* ref_idc == 0 for 6,9,10,11,12 */
};

//֡����
enum Frametype_e
{
	FRAME_I  = 15,
	FRAME_P  = 16,
	FRAME_B  = 17
};


NALU_t *allocate_nal_unit(int buffersize);
void free_nal_unit(NALU_t * n);  
int is_the_right_nalu_prefix3( unsigned char * stream_buf );  
int is_the_right_nalu_prefix4 (unsigned char * stream_buf ); 
int read_h264_nal_unit(FILE *,NALU_t *nalu);                //��дnal ���ݺ�ͷ
int get_h264_frame_type(NALU_t * n);                    //��ȡ֡����
int read_h264_nal_unit( uint8_t *stream_buffer, uint8_t buffer_size, NALU_t * nalu );
int read_h264_frame(FILE *,unsigned char * h264_frame, unsigned int & frame_length, unsigned int & frame_type);
int read_h264_frame(unsigned char * input_stream,unsigned int stream_size, unsigned char * h264_frame,
                                       unsigned int & frame_length, unsigned int & frame_type);
#endif