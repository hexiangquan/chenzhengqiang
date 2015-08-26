#ifndef _CZQ_FILE_IO_H_
#define _CZQ_FILE_IO_H_

#include "Information.h"

#define INPUTH264FILENAME     "./test.264"
#define INPUTAACFILENAME      "./test.aac"
#define OUTPUTFLVFILENAME     "./test.flv"


extern FILE * pVideo_H264_File;
extern FILE * pAudio_Aac_File;
extern FILE * pVideo_Audio_Flv_File;

FILE * OpenFile(char * FileName,char * OpenMode);                        //���ļ�
void   CloseFile(FILE * pFile);                                          //�ر��ļ�
int    ReadFile(FILE * pFile ,unsigned char * Buffer,int BufferSize);    //��ȡ����
int    WriteFile(FILE * pFile ,char * Buffer,int BufferSize);            //�ļ�д����
#endif
