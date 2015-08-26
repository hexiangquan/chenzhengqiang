#ifndef _CZQ_AUDIO_TAG_H_
#define _CZQ_AUDIO_TAG_H_
#include "Flv.h"

#define AUDIO_TAG_HEADER_LENGTH    11
#define ADTS_HEADER_LENGTH         7
#define MAX_AUDIO_TAG_BUF_SIZE     1024 * 100

extern unsigned int decode_audio_done;

//ADTS ͷ��������õ���Ϣ �����ʡ���������֡����
//adtsͷ
typedef struct
{
	unsigned int syncword;  //12 bslbf ͬ����The bit string ��1111 1111 1111����˵��һ��ADTS֡�Ŀ�ʼ
	unsigned int id;        //1 bslbf   MPEG ��ʾ��, ����Ϊ1
	unsigned int layer;     //2 uimsbf Indicates which layer is used. Set to ��00��
	unsigned int protection_absent;  //1 bslbf  ��ʾ�Ƿ�����У��
	unsigned int profile;            //2 uimsbf  ��ʾʹ���ĸ������AAC����01 Low Complexity(LC)--- AACLC
	unsigned int sf_index;           //4 uimsbf  ��ʾʹ�õĲ������±�
	unsigned int private_bit;        //1 bslbf 
	unsigned int channel_configuration;  //3 uimsbf  ��ʾ������
	unsigned int original;               //1 bslbf 
	unsigned int home;                   //1 bslbf 
	/*�����Ϊ�ı�Ĳ�����ÿһ֡����ͬ*/
	unsigned int copyright_identification_bit;   //1 bslbf 
	unsigned int copyright_identification_start; //1 bslbf
	unsigned int aac_frame_length;               // 13 bslbf  һ��ADTS֡�ĳ��Ȱ���ADTSͷ��raw data block
	unsigned int adts_buffer_fullness;           //11 bslbf     0x7FF ˵�������ʿɱ������
	/*no_raw_data_blocks_in_frame ��ʾADTS֡����number_of_raw_data_blocks_in_frame + 1��AACԭʼ֡.
	����˵number_of_raw_data_blocks_in_frame == 0 
	��ʾ˵ADTS֡����һ��AAC���ݿ鲢����˵û�С�(һ��AACԭʼ֡����һ��ʱ����1024���������������)
    */
	unsigned int no_raw_data_blocks_in_frame;    //2 uimsfb
} ADTS_HEADER;


typedef struct Tag_Audio_ASC
{
	unsigned char audioObjectType;              //5;��������ͣ�AAC-LC = 0x02
	unsigned char samplingFrequencyIndex;       //4;������ 44100 = 0x04
	unsigned char channelConfiguration;         //4;���� = 2
	unsigned char framelengthFlag;              //1;��־λ��λ�ڱ���IMDCT���ڳ��� = 0
	unsigned char dependsOnCoreCoder;           //1;��־λ�������Ƿ�������corecoder = 0
	unsigned char extensionFlag;                //1;ѡ����AAC-LC = 0
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

int   Detach_Head_Aac(ADTS_HEADER * adtsheader);                                 //��ȡADTSͷ��Ϣ
void  Create_AudioSpecificConfig(unsigned char * buf,
							   unsigned char profile/*��ʾʹ���ĸ������AAC����01 Low Complexity(LC)--- AACLC*/,
							   unsigned char SoundRate/*������*/,
							   unsigned char SoundType/*����*/ );
int WriteStruct_Aac_Tag(unsigned char * Buf,unsigned int  Timestamp,unsigned char AACPacketType/*AAC����ͷ��*/);
#endif