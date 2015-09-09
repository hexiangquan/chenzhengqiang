/*
@author:internet
@modified author:chenzhengqiang
@start date:2015/9/9
*/


#ifndef _CZQ_AAC_H_
#define _CZQ_AAC_H_

#include "flv.h"
#define ONE_AUDIO_FRAME_SIZE 1024*100

typedef struct Tag_Audio_ASC
{
	unsigned char audioObjectType;              //��������ͣ�AAC-LC = 0x02
	unsigned char samplingFrequencyIndex;       //������ 44100 = 0x04
	unsigned char channelConfiguration;         //���� = 2
	unsigned char framelengthFlag;              //��־λ��λ�ڱ���IMDCT���ڳ��� = 0
	unsigned char dependsOnCoreCoder;           //��־λ�������Ƿ�������corecoder = 0
	unsigned char extensionFlag;                //ѡ����AAC-LC = 0

}Audio_ASC;

//����tag��header��tag��data
typedef struct Tag_Audio_Tag                           
{
	unsigned char Type ;                       //��Ƶ��0x08������Ƶ��0x09����script data��0x12����������
	unsigned int  DataSize;                    //������tagheader �ĳ���
	unsigned int  Timestamp;                   //��5-7�ֽ�ΪUI24���͵�ֵ����ʾ��Tag��ʱ�������λΪms������һ��Tag��ʱ�������0��
	unsigned char TimestampExtended;           //��8���ֽ�Ϊʱ�������չ�ֽڣ���24λ��ֵ����ʱ�����ֽ���Ϊ���λ��ʱ�����չΪ32λֵ��
	unsigned int  StreamID;                    //��9-11�ֽ�ΪUI24���͵�ֵ����ʾstream id������0��
	//0 = Linear PCM, platform endian
	//1 = ADPCM
	//2 = MP3
	//3 = Linear PCM, little endian
	//4 = Nellymoser 16-kHz mono
	//5 = Nellymoser 8-kHz mono
	//6 = Nellymoser
	//7 = G.711 A-law logarithmic PCM
	//8 = G.711 mu-law logarithmic PCM
	//9 = reserved
	//10 = AAC
	//11 = Speex
	//14 = MP3 8-Khz
	//15 = Device-specific sound
	//Format of SoundData
	//Formats 7, 8, 14, and 15 are
	//reserved for internal use
	//AAC is supported in Flash
	//Player 9,0,115,0 and higher.
	//Speex is supported in Flash
	//Player 10 and highe
	unsigned char SoundFormat ;                //��������
	//0 = 5.5-kHz
	//1 = 11-kHz
	//2 = 22-kHz
	//3 = 44-kHz
	//Sampling rate For AAC: always 3
	unsigned char SoundRate ;                  //������
	//0 = snd8Bit
	//1 = snd16Bit
	//Size of each sample. This
	//parameter only pertains to
	//uncompressed formats.
	//Compressed formats always
	//decode to 16 bits internally.
	//0 = snd8Bit
	//1 = snd16Bit
	unsigned char SoundSize;                   //����
	//0 = sndMono
	//1 = sndStereo
	//Mono or stereo sound
	//For Nellymoser: always 0
	//For AAC: always 1
	unsigned char SoundType;                   //����
	//SoundData UI8[size of sound data] 
	//if SoundFormat == 10
	//AACAUDIODATA
	//else  Sound data��varies by format

	//0: AAC sequence header
	//1: AAC raw
	unsigned char AACPacketType;               //AAC����ͷ��
	//if AACPacketType == 0        AudioSpecificConfig
	//else if AACPacketType == 1   Raw AAC frame data
	unsigned char * Data; 
	Audio_ASC * audioasc;
}Audio_Tag;

int AllocStruct_Aac_Tag(Audio_Tag ** audiotag);
int FreeStruct_Aac_Tag(Audio_Tag * audiotag);
int ReadStruct_Aac_Tag(unsigned char * Buf , unsigned int length ,Audio_Tag * tag);
#endif
