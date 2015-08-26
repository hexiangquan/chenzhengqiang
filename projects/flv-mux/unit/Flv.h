#ifndef _CZQ_FLV_H_
#define _CZQ_FLV_H_

#include "FileIo.h"

#define FILE_HEADER_LENGTH    9

//�ļ�ͷ�ṹ��9���ֽ�
typedef struct Tag_File_Header
{
	unsigned char Signature_1;                 //always 'F' (0x46)
	unsigned char Signature_2;                 //always 'L' (0x4C)
	unsigned char Signature_3;                 //always 'V' (0x56)
	unsigned char version  ;                   //�汾�� ������0x01
	unsigned char TypeFlagsReserved_1;         //��5���ֽڵ�ǰ5λ����������Ϊ0��
	unsigned char TypeFlagsAudio;              //��5���ֽڵĵ�6λ��ʾ�Ƿ������ƵTag��
	unsigned char TypeFlagsReserved_2;         //��5���ֽڵĵ�7λ����������Ϊ0��
	unsigned char TypeFlagsVideo;              //��5���ֽڵĵ�8λ��ʾ�Ƿ������ƵTag��
	unsigned int  DataOffset;                  //��6-9���ֽ�ΪUI32���͵�ֵ����ʾ��File Header��ʼ��File Body��ʼ���ֽ������汾1����Ϊ9

}File_Header;

int WriteStruct_File_Header(unsigned char * Buf , unsigned int length);
#endif