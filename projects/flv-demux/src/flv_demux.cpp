/*
@author:internet
@improved by:chenzhenqiang
@start date:2015/9/9
*/

#include "common.h"
#include "flv_demux.h"

File_Header * m_fileheadr  = NULL;
Audio_Tag   * m_audio_tag  = NULL;
Video_Tag   * m_video_tag  = NULL;
Script_Tag  * m_script_tag = NULL;
static const int NO = 0;
int AllocStruct()
{
	//��չ�²�
	AllocStruct_File_Header(&m_fileheadr);
	AllocStruct_Aac_Tag(&m_audio_tag);
	AllocStruct_H264_Tag(&m_video_tag);
	AllocStruct_Script_Tag(&m_script_tag);
	return 1;
}

int FreeStruct()
{
	//��չ�²�
	FreeStruct_File_Header(m_fileheadr);
	FreeStruct_Aac_Tag(m_audio_tag);
	FreeStruct_H264_Tag(m_video_tag);
	FreeStruct_Script_Tag(m_script_tag);
	return 1;
}

int And_Head_H264( FILE *f264_handler,unsigned char * buf, unsigned int size,unsigned char *spsbuffer,unsigned int spslength,unsigned char * ppsbuffer,unsigned int  ppslength ,unsigned int IsVideo_I_Frame)
{
	int write_size = 0;
	unsigned char * buffer = NULL;
	unsigned char strcode[4];                //ǰ׺��־
	unsigned int Sei_length_1 = 0;           //sei֡�ĳ���
	unsigned int Sei_length_2 = 0;
	buffer = (unsigned char * )calloc(size + 1024,sizeof(char));

	//////////////////////////////////////////////////////////////////////////
	if (buf[4] == 0x06)  //SEI
	{
		Sei_length_1 = 
			buf[2]  << 8  |
			buf[3];
		memcpy(buffer,buf,size);
		buffer[4 + Sei_length_1] =    0x00;
		buffer[4 + Sei_length_1 +1] = 0x00;
		buffer[4 + Sei_length_1 +2] = 0x00;
		buffer[4 + Sei_length_1 +3] = 0x01;
	}
	else
	{
		memcpy(buffer,buf,size);
	}
	if (IsVideo_I_Frame == 1) //����ǹؼ�֡
	{
		strcode[0] = 0x00;
		strcode[1] = 0x00;
		strcode[2] = 0x00;
		strcode[3] = 0x01;
		write_size = fwrite((char *)strcode,1,4,f264_handler);
		write_size = fwrite((char *)spsbuffer,1,spslength, f264_handler );
		write_size = fwrite( (char *)strcode,1,4,f264_handler );
		write_size = fwrite((char *)ppsbuffer,1,ppslength, f264_handler);
	}
	buffer[0] = 0x00;
	buffer[1] = 0x00;
	buffer[2] = 0x00;
	buffer[3] = 0x01;
	//////////////////////////////////////////////////////////////////////////
	write_size = fwrite((char *)buffer,1,size,f264_handler );
	if (buffer)
	{
		free(buffer);
		buffer == NULL;
	}
	return  write_size;
}

int And_Head_Aac( FILE *faac_handler,unsigned char * buf, unsigned int size,unsigned int IsAacFrame,unsigned char audioObjectType,unsigned char samplerate,unsigned char channelcount)
{
	int write_size = 0;
	if (IsAacFrame)
	{
		//////////////////////////////////////////////////////////////////////////
		//ADTS ͷ��������õ���Ϣ �����ʡ���������֡����
		//adtsͷ
		//typedef struct
		//{
		//	unsigned int syncword;  //12 bslbf ͬ����The bit string ��1111 1111 1111����˵��һ��ADTS֡�Ŀ�ʼ
		//	unsigned int id;        //1 bslbf   MPEG ��ʾ��, ����Ϊ1
		//	unsigned int layer;     //2 uimsbf Indicates which layer is used. Set to ��00��
		//	unsigned int protection_absent;  //1 bslbf  ��ʾ�Ƿ�����У��


		//	unsigned int profile;            //2 uimsbf  ��ʾʹ���ĸ������AAC����01 Low Complexity(LC)--- AACLC
		//	unsigned int sf_index;           //4 uimsbf  ��ʾʹ�õĲ������±�
		//	unsigned int private_bit;        //1 bslbf 
		//	unsigned int channel_configuration;  //3 uimsbf  ��ʾ������
		//	unsigned int original;               //1 bslbf 
		//	unsigned int home;                   //1 bslbf 
		//	/*�����Ϊ�ı�Ĳ�����ÿһ֡����ͬ*/
		//	unsigned int copyright_identification_bit;   //1 bslbf 
		//	unsigned int copyright_identification_start; //1 bslbf

		//	unsigned int aac_frame_length;               // 13 bslbf  һ��ADTS֡�ĳ��Ȱ���ADTSͷ��raw data block
		//	unsigned int adts_buffer_fullness;           //11 bslbf     0x7FF ˵�������ʿɱ������
		//
		//	/*no_raw_data_blocks_in_frame ��ʾADTS֡����number_of_raw_data_blocks_in_frame + 1��AACԭʼ֡.
		//	����˵number_of_raw_data_blocks_in_frame == 0 
		//	��ʾ˵ADTS֡����һ��AAC���ݿ鲢����˵û�С�(һ��AACԭʼ֡����һ��ʱ����1024���������������)
		//    */
		//	unsigned int no_raw_data_blocks_in_frame;    //2 uimsfb
		//} ADTS_HEADER;

		//?0: 96000 Hz
		//?1: 88200 Hz
		//?2: 64000 Hz
		//?3: 48000 Hz
		//?4: 44100 Hz
		//?5: 32000 Hz
		//?6: 24000 Hz
		//?7: 22050 Hz
		//?8: 16000 Hz
		//?9: 12000 Hz
		//?10: 11025 Hz
		//?11: 8000 Hz
		//?12: 7350 Hz
		//?13: Reserved
		//?14: Reserved
		//?15: frequency is written explictly

		//?0: Defined in AOT Specifc Config
		//?1: 1 channel: front-center
		//?2: 2 channels: front-left, front-right
		//?3: 3 channels: front-center, front-left, front-right
		//?4: 4 channels: front-center, front-left, front-right, back-center
		//?5: 5 channels: front-center, front-left, front-right, back-left, back-right
		//?6: 6 channels: front-center, front-left, front-right, back-left, back-right, LFE-channel
		//?7: 8 channels: front-center, front-left, front-right, side-left, side-right, back-left, back-right, LFE-channel
		//?8-15: Reserved
		unsigned char  adts_headerbuf[ADTS_HEADER_LENGTH] ;
		unsigned int  aac_frame_length = size + ADTS_HEADER_LENGTH;
		adts_headerbuf[0] = 0xFF;
		adts_headerbuf[1] = 0xF1;
		//����Ҫ��0x01 vlc ���������ܲ���
		//adts_headerbuf[2] = (audioObjectType << 6) /*��01 Low Complexity(LC)--- AACLC*/ | (samplerate << 2)  /* ������ 44100���±� */ | (channelcount >> 7) /*���� = 2*/;
		adts_headerbuf[2] = (0x01 << 6) /*��01 Low Complexity(LC)--- AACLC*/ | (samplerate << 2)  /* ������ 44100���±� */ | (channelcount >> 7) /*���� = 2*/;
		adts_headerbuf[3] = (channelcount << 6) |  0x00 | 0x00 | 0x00 |0x00 | ((aac_frame_length &  0x1800) >> 11);
		adts_headerbuf[4] = (aac_frame_length & 0x7F8) >> 3;
		adts_headerbuf[5] = (aac_frame_length & 0x7) << 5  |  0x1F;
		adts_headerbuf[6] = 0xFC  | 0x00;
		write_size = fwrite((char*)adts_headerbuf, sizeof(unsigned char) ,ADTS_HEADER_LENGTH, faac_handler ); 
	}
	write_size = fwrite((char *)buf,sizeof(unsigned char),size, faac_handler );
	return write_size;
}

int ReadStruct( FILE * fflv_handler, FILE *f264_handler, FILE * faac_handler )
{
	unsigned char * temporary_buf = NULL;
	unsigned int    temporary_bufsize = 0;
	unsigned int    tag_data_size = 0 ;           //Tag Data���ֵĴ�С     
	unsigned char   tag_type;                     //tag����
	unsigned int    audiowritedatasize = 0;
	unsigned int    videowritedatasize = 0;
	unsigned int    scriptwritedatasize= 0;
	unsigned int    IsAacFrame = 0;
	unsigned char   spsbuffer[MAX_FRAME_HEAD_SIZE];
	unsigned char   ppsbuffer[MAX_FRAME_HEAD_SIZE];
	unsigned int    spslength = 0;
       unsigned int    ppslength = 0;
	unsigned char   audioObjectType;              //��������ͣ�AAC-LC = 0x02
	unsigned char   samplingFrequencyIndex;       //������ 44100 = 0x04
	unsigned char   channelConfiguration;         //����


	if ((temporary_buf = (unsigned char *)calloc(ONE_FRAME_SIZE,sizeof(char))) == NULL)
	{
		printf ("Error: Allocate Meory To temporary_buf Buffer Failed ");
		return getchar();
	}
	
//////////////////////////////////////////////////////////////////////////
	//��ȡFILEHEADER
	temporary_bufsize = fread(temporary_buf,sizeof(unsigned char),9, fflv_handler );
	ReadStruct_File_Header(temporary_buf , 9 ,m_fileheadr);
	//��ȡ����Tag
       while ( feof ( fflv_handler ) == NO )                         //���δ���ļ���β
	{
		temporary_bufsize = fread(temporary_buf,sizeof(unsigned char),4, fflv_handler );     //PreviousTagSize
		temporary_bufsize = fread( temporary_buf, sizeof(unsigned char),11, fflv_handler );    //Tag Header
		if (temporary_bufsize == 0)
		{
			if (feof( fflv_handler ) )   //���ļ���β
			{
				break;
			}
		}
		tag_type = temporary_buf[0];
		tag_data_size = 
			temporary_buf[1]  << 16 |
			temporary_buf[2]  << 8  |
			temporary_buf[3];
		temporary_bufsize = fread( temporary_buf + 11,sizeof(unsigned char),tag_data_size, fflv_handler );
		switch (tag_type)
		{
		    case 0x08:     //��Ƶ��0x08������Ƶ��0x09����script data��0x12������������
			printf("audio  : tag  size  include tag header : %d\n",(tag_data_size +11));
			audiowritedatasize = ReadStruct_Aac_Tag(temporary_buf ,tag_data_size + 11 ,m_audio_tag);
			if ( m_audio_tag->SoundFormat == 0x0A )       //�����AAC
			{
				IsAacFrame = 1;
				if(m_audio_tag->AACPacketType != 0x00)   //�������AudioSpecificConfig
				{
					And_Head_Aac(faac_handler,m_audio_tag->Data,audiowritedatasize,IsAacFrame,audioObjectType,samplingFrequencyIndex,channelConfiguration);
				}
				else
				{
					audioObjectType = m_audio_tag->audioasc->audioObjectType;
					samplingFrequencyIndex = m_audio_tag->audioasc->samplingFrequencyIndex;
					channelConfiguration = m_audio_tag->audioasc->channelConfiguration;
				}
			}
			else                                         //Sound data��varies by format
			{
				IsAacFrame = 0;
				And_Head_Aac( faac_handler, m_audio_tag->Data,audiowritedatasize,IsAacFrame,audioObjectType,samplingFrequencyIndex,channelConfiguration);
			}
			break;
		    case 0x09:
			printf("video  : tag  size  include tag header : %d\n",(tag_data_size +11));
			videowritedatasize = ReadStruct_H264_Tag(temporary_buf ,tag_data_size + 11 ,m_video_tag);
			if (m_video_tag->CodecID == 0x07 )  //AVCVIDEOPACKET
			{
				if (m_video_tag->AVCPacketType == 0x00)       //�����AudioSpecificConfig
				{
					//��ȡsps ��pps ��buffer
					spslength = m_video_tag->video_avcc->sequenceParameterSetLength;
					ppslength = m_video_tag->video_avcc->pictureParameterSetLength;
					memcpy(spsbuffer,m_video_tag->video_avcc->sequenceParameterSetNALUnit,spslength);
					memcpy(ppsbuffer,m_video_tag->video_avcc->pictureParameterSetNALUnit,ppslength);
				}
				else if(m_video_tag->AVCPacketType != 0x02)   //�������empty
				{
					And_Head_H264(f264_handler,m_video_tag->Data,videowritedatasize,spsbuffer,spslength,ppsbuffer,ppslength,m_video_tag->FrameType);
				}
			}
			else
			{
				And_Head_H264(f264_handler,m_video_tag->Data,videowritedatasize,spsbuffer,spslength,ppsbuffer,ppslength,m_video_tag->FrameType);
			}
			break;
		    case 0x12:
			printf("script : tag  size  include tag header : %d\n",(tag_data_size +11));
			scriptwritedatasize = ReadStruct_Script_Tag(temporary_buf ,tag_data_size + 11 ,m_script_tag);
			break;
		    default:
			printf("others : tag  reserved\n");
		}
	}
	printf("duration                        : %lf\n",m_script_tag->duration);
	printf("width                           : %lf\n",m_script_tag->width);
	printf("height                          : %lf\n",m_script_tag->height);
	printf("ideodatarate                   : %lf\n",m_script_tag->videodatarate);
	printf("framerate                       : %lf\n",m_script_tag->framerate);
	printf("videocodecid                    : %lf\n",m_script_tag->videodatarate);
	printf("audiosamplerate                 : %lf\n",m_script_tag->audiosamplerate);
	printf("audiodatarate                   : %lf\n",m_script_tag->audiodatarate);
	printf("audiosamplesize                 : %lf\n",m_script_tag->audiosamplesize);
	printf("stereo                          : %d\n",m_script_tag->stereo);
	printf("audiocodecid                    : %lf\n",m_script_tag->audiocodecid);
	printf("filesize                        : %lf\n",m_script_tag->filesize);
	printf("lasttimetamp                    : %lf\n",m_script_tag->lasttimetamp);
	printf("lastkeyframetimetamp            : %lf\n",m_script_tag->lastkeyframetimetamp);
	if (temporary_buf) 
	{
		free(temporary_buf);
		temporary_buf = NULL;
	}
	return 1;
}

void flv_demux_2_h264_aac( const char *flv_file, const char *h264_file, const char * aac_file )
{
    	printf("++++++++++FLV DEMUX START++++++++++\n");
       FILE *fflv_handler = fopen( flv_file, "rb" );
       FILE *f264_handler = fopen( h264_file, "wb" );
       FILE *faac_handler = fopen( aac_file, "wb" );

       if( fflv_handler == NULL || f264_handler == NULL || faac_handler == NULL )
       {
            printf("FILE OPEN FAILLED:%s",strerror(errno));
            printf("++++++++++FLV DEMUX DONE++++++++++\n");
            return;
       }

      AllocStruct();
      ReadStruct( fflv_handler, f264_handler, faac_handler );
      FreeStruct();
      fclose( fflv_handler );
      fclose( f264_handler );
      fclose( faac_handler );
      printf("++++++++++FLV DEMUX DONE++++++++++\n");
}