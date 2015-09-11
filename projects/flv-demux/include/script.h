/*
@author:internet
@modified author:chenzhengqiang
@start date:2015/9/9
@modified date:
*/

#ifndef _CZQ_SCRIPT_H_
#define _CZQ_SCRIPT_H_
#include "flv.h"

static const int ONE_SCRIPT_FRAME_SIZE = 1024 * 1024;
static const int MAX_ECMAARAY_NAME_LENGH = 100;

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
	unsigned int  ECMAArrayLength;             //һ���ж��������ݣ����ж��ٸ����ƣ����ߣ���������������Ϣ
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
	double filepositions[1000];                //ÿһ֡�������ļ��е�λ��
	double times[1000];                        //ʱ��
	unsigned char Data[ONE_SCRIPT_FRAME_SIZE];                      //��Ϣʣ�µ����ݣ���ʱû�н���
}FLV_SCRIPT_TAG ;

double char2double(unsigned char * buf,unsigned int size);
void   double2char(unsigned char * buf,double val);
int read_flv_script_tag( unsigned char * flv_script_buffer, unsigned int length, FLV_SCRIPT_TAG & script_tag );
#endif