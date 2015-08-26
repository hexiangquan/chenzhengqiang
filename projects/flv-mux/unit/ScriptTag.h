#ifndef _CZQ_SCRIPT_TAG_H_
#define _CZQ_SCRIPT_TAG_H_
#include "Flv.h"

#define  MAX_SCRIPT_TAG_BUF_SIZE  1024 * 100
#define  SCRIPT_TAG_HEADER_LENGTH  11

//����tag��header��tag��data
typedef struct Tag_Script_Tag                           
{
	unsigned char Type ;                       //��Ƶ��0x08������Ƶ��0x09����script data��0x12����������
	unsigned int  DataSize;                    //������tagheader �ĳ���
	unsigned int  Timestamp;                   //��5-7�ֽ�ΪUI24���͵�ֵ����ʾ��Tag��ʱ�������λΪms������һ��Tag��ʱ�������0��
	unsigned char TimestampExtended;           //��8���ֽ�Ϊʱ�������չ�ֽڣ���24λ��ֵ����ʱ�����ֽ���Ϊ���λ��ʱ�����չΪ32λֵ��
	unsigned int  StreamID;                    //��9-11�ֽ�ΪUI24���͵�ֵ����ʾstream id������0��
	//Type of the ScriptDataValue.
	//The following types are defined:
	//0 = Number
	//1 = Boolean
	//2 = String
	//3 = Object
	//4 = MovieClip (reserved, not supported)
	//5 = Null
	//6 = Undefined
	//7 = Reference
	//8 = ECMA array
	//9 = Object end marker
	//10 = Strict array
	//11 = Date
	//12 = Long string
	unsigned char Type_1;
	unsigned int  StringLength;                //��2-3���ֽ�ΪUI16����ֵ����ʾ�ַ����ĳ��ȣ�һ������0x000A����onMetaData������
	unsigned char Type_2;
	unsigned int  ECMAArrayLength;             //4:һ���ж��������ݣ����ж��ٸ����ƣ����ߣ���������������Ϣ
	double duration;                           //�ļ�����ʱ��         
	double width;							   //�ļ�    ���         
	double height;							   //�ļ�    �߶�         
	double videodatarate;					   //��Ƶ��������         
	double framerate;						   //֡����               
	double videocodecid;					   //��Ƶ�������id       
	double audiosamplerate;					   //��Ƶ������           
	double audiodatarate;					   //��Ƶ��������         
	double audiosamplesize;					   //��Ƶ������С         
	int    stereo;							   //�Ƿ���������         
	double audiocodecid;					   //��Ƶ�������id       
	double filesize;						   //�ļ���С             
	double lasttimetamp;					   //�ļ����ʱ��         
	double lastkeyframetimetamp;               //�ļ����ؼ�֡ʱ��� 
	double filepositions[1000];                //ÿһ��I֡�������ļ��е�λ��
	double times[1000];                        //ʱ��
	unsigned char * Data;                      //��Ϣʣ�µ����ݣ���ʱû�н���
}Script_Tag ;

double char2double(unsigned char * buf,unsigned int size);
void   double2char(unsigned char * buf,double val);
int WriteStruct_Script_Tag(unsigned char * buf,double duration,double width,double height,double framerate,double audiosamplerate,int stereo,double filesize,
						   unsigned int filepositions_times_length ,double * filepositions,double *times);
#endif

