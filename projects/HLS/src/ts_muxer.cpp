/*
@author:chenzhengqiang
@version:1.0
@start date:2015/8/26
@modified date:
@desc:providing the api that encapsulate the h264 file and aac file into ts file frame by frame
*/

#include "errors.h"
#include "ts_muxer.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>


static const int H264_FRAME_RATE = 30;

int write_ts_pat(FILE *fts_handler,unsigned char * buf)
{
	WriteStruct_Pat(buf);
	return fwrite((char *)buf,sizeof(unsigned char),TS_PACKET_SIZE, fts_handler );
}

int write_ts_pmt(FILE *fts_handler,unsigned char * buf)
{
	WriteStruct_Pmt(buf);
	return fwrite( (char *)buf,sizeof(unsigned char),TS_PACKET_SIZE, fts_handler);
}

int write_adaptive_head_fields(Ts_Adaptation_field  * ts_adaptation_field,unsigned int Videopts)
{
	//��д����Ӧ��
	ts_adaptation_field->discontinuty_indicator = 0;
	ts_adaptation_field->random_access_indicator = 0;
	ts_adaptation_field->elementary_stream_priority_indicator = 0;
	ts_adaptation_field->PCR_flag = 1;                                          //ֻ�õ����
	ts_adaptation_field->OPCR_flag = 0;
	ts_adaptation_field->splicing_point_flag = 0;
	ts_adaptation_field->transport_private_data_flag = 0;
	ts_adaptation_field->adaptation_field_extension_flag = 0;

	//��Ҫ�Լ���
	ts_adaptation_field->pcr  = Videopts * 300;
	ts_adaptation_field->adaptation_field_length = 7;                          //ռ��7λ

	ts_adaptation_field->opcr = 0;
	ts_adaptation_field->splice_countdown = 0;
	ts_adaptation_field->private_data_len = 0;
	return 1;
}

int write_adaptive_tail_fields(Ts_Adaptation_field  * ts_adaptation_field)
{
	//��д����Ӧ��
	ts_adaptation_field->discontinuty_indicator = 0;
	ts_adaptation_field->random_access_indicator = 0;
	ts_adaptation_field->elementary_stream_priority_indicator = 0;
	ts_adaptation_field->PCR_flag = 0;                                          //ֻ�õ����
	ts_adaptation_field->OPCR_flag = 0;
	ts_adaptation_field->splicing_point_flag = 0;
	ts_adaptation_field->transport_private_data_flag = 0;
	ts_adaptation_field->adaptation_field_extension_flag = 0;

	//��Ҫ�Լ���
	ts_adaptation_field->pcr  = 0;
	ts_adaptation_field->adaptation_field_length = 1;                          //ռ��1λ��־���õ�λ

	ts_adaptation_field->opcr = 0;
	ts_adaptation_field->splice_countdown = 0;
	ts_adaptation_field->private_data_len = 0;                    
	return 1;
}

int create_adaptive_ts(Ts_Adaptation_field * ts_adaptation_field,unsigned char * buf,unsigned int AdaptiveLength)
{
	unsigned int CurrentAdaptiveLength = 1;                                 //��ǰ�Ѿ��õ�����Ӧ�γ���  
	unsigned char Adaptiveflags = 0;                                        //����Ӧ�εı�־
	unsigned int adaptive_pos = 0;

	//��д����Ӧ�ֶ�
	if (ts_adaptation_field->adaptation_field_length > 0)
	{
		adaptive_pos += 1;                                                  //����Ӧ�ε�һЩ��־��ռ�õ�1���ֽ�
		CurrentAdaptiveLength += 1;

		if (ts_adaptation_field->discontinuty_indicator)
		{
			Adaptiveflags |= 0x80;
		}
		if (ts_adaptation_field->random_access_indicator)
		{
			Adaptiveflags |= 0x40;
		}
		if (ts_adaptation_field->elementary_stream_priority_indicator)
		{
			Adaptiveflags |= 0x20;
		}
		if (ts_adaptation_field->PCR_flag)
		{
			unsigned long long pcr_base;
			unsigned int pcr_ext;

			pcr_base = (ts_adaptation_field->pcr / 300);
			pcr_ext = (ts_adaptation_field->pcr % 300);

			Adaptiveflags |= 0x10;

			buf[adaptive_pos + 0] = (pcr_base >> 25) & 0xff;
			buf[adaptive_pos + 1] = (pcr_base >> 17) & 0xff;
			buf[adaptive_pos + 2] = (pcr_base >> 9) & 0xff;
			buf[adaptive_pos + 3] = (pcr_base >> 1) & 0xff;
			buf[adaptive_pos + 4] = pcr_base << 7 | pcr_ext >> 8 | 0x7e;
			buf[adaptive_pos + 5] = (pcr_ext) & 0xff;
			adaptive_pos += 6;

			CurrentAdaptiveLength += 6;
		}
		if (ts_adaptation_field->OPCR_flag)
		{
			unsigned long long opcr_base;
			unsigned int opcr_ext;

			opcr_base = (ts_adaptation_field->opcr / 300);
			opcr_ext = (ts_adaptation_field->opcr % 300);

			Adaptiveflags |= 0x08;

			buf[adaptive_pos + 0] = (opcr_base >> 25) & 0xff;
			buf[adaptive_pos + 1] = (opcr_base >> 17) & 0xff;
			buf[adaptive_pos + 2] = (opcr_base >> 9) & 0xff;
			buf[adaptive_pos + 3] = (opcr_base >> 1) & 0xff;
			buf[adaptive_pos + 4] = ((opcr_base << 7) & 0x80) | ((opcr_ext >> 8) & 0x01);
			buf[adaptive_pos + 5] = (opcr_ext) & 0xff;
			adaptive_pos += 6;
			CurrentAdaptiveLength += 6;
		}
		if (ts_adaptation_field->splicing_point_flag)
		{
			buf[adaptive_pos] = ts_adaptation_field->splice_countdown;

			Adaptiveflags |= 0x04;

			adaptive_pos += 1;
			CurrentAdaptiveLength += 1;
		}
		if (ts_adaptation_field->private_data_len > 0)
		{
			Adaptiveflags |= 0x02;
			if ((unsigned int)(1+ ts_adaptation_field->private_data_len) > AdaptiveLength - CurrentAdaptiveLength)
			{
				printf("private_data_len error !\n");
				return getchar();
			}
			else
			{
				buf[adaptive_pos] = ts_adaptation_field->private_data_len;
				adaptive_pos += 1;
				memcpy (buf + adaptive_pos, ts_adaptation_field->private_data, ts_adaptation_field->private_data_len);
				adaptive_pos += ts_adaptation_field->private_data_len;

				CurrentAdaptiveLength += (1 + ts_adaptation_field->private_data_len) ;
			}
		}
		if (ts_adaptation_field->adaptation_field_extension_flag)
		{
			Adaptiveflags |= 0x01;
			buf[adaptive_pos + 1] = 1;
			buf[adaptive_pos + 2] = 0;
			CurrentAdaptiveLength += 2;
		}
		buf[0] = Adaptiveflags;                        //����־�����ڴ�
	}
	return 1;
}

int pes_2_ts(FILE *fts_handler,TsPes * ts_pes,unsigned int Video_Audio_PID ,Ts_Adaptation_field * ts_adaptation_field_head ,Ts_Adaptation_field * ts_adaptation_field_tail,
		   unsigned long  Videopts,unsigned long Adudiopts)
{
       
       (void)ts_adaptation_field_tail;
       unsigned int ts_pos = 0;
	unsigned int FirstPacketLoadLength = 0 ;                                   //��Ƭ���ĵ�һ�����ĸ��س���
	unsigned int NeafPacketCount = 0;                                          //��Ƭ���ĸ���
	unsigned int AdaptiveLength = 0;                                           //Ҫ��д0XFF�ĳ���
	unsigned char * NeafBuf = NULL;                                            //��Ƭ�� �ܸ��ص�ָ��
	unsigned char TSbuf[TS_PACKET_SIZE];

	memset(TSbuf,0,TS_PACKET_SIZE); 
	FirstPacketLoadLength = 188 - 4 - 1 - ts_adaptation_field_head->adaptation_field_length - 14; //�����Ƭ���ĵ�һ�����ĸ��س���
	NeafPacketCount += 1;                                                                   //��һ����Ƭ��  

	//һ���������
	if (ts_pes->Pes_Packet_Length_Beyond < FirstPacketLoadLength)                           //������ sps ��pps ��sei��
	{
		memset(TSbuf,0xFF,TS_PACKET_SIZE);
		WriteStruct_Packetheader(TSbuf,Video_Audio_PID,0x01,0x03);                          //PID = TS_H264_PID,��Ч���ص�Ԫ��ʼָʾ��_play_init = 0x01, ada_field_C,0x03,���е����ֶκ���Ч���� ��
		ts_pos += 4;
		TSbuf[ts_pos + 0] = 184 - ts_pes->Pes_Packet_Length_Beyond - 9 - 5 - 1 ;
		TSbuf[ts_pos + 1] = 0x00;
		ts_pos += 2; 
		memset(TSbuf + ts_pos,0xFF,(184 - ts_pes->Pes_Packet_Length_Beyond - 9 - 5 - 2));
		ts_pos += (184 - ts_pes->Pes_Packet_Length_Beyond - 9 - 5 - 2);

		TSbuf[ts_pos + 0] = (ts_pes->packet_start_code_prefix >> 16) & 0xFF;
		TSbuf[ts_pos + 1] = (ts_pes->packet_start_code_prefix >> 8) & 0xFF; 
		TSbuf[ts_pos + 2] = ts_pes->packet_start_code_prefix & 0xFF;
		TSbuf[ts_pos + 3] = ts_pes->stream_id;
		TSbuf[ts_pos + 4] = ((ts_pes->PES_packet_length) >> 8) & 0xFF;
		TSbuf[ts_pos + 5] = (ts_pes->PES_packet_length) & 0xFF;
		TSbuf[ts_pos + 6] = ts_pes->marker_bit << 6 | ts_pes->PES_scrambling_control << 4 | ts_pes->PES_priority << 3 |
			ts_pes->data_alignment_indicator << 2 | ts_pes->copyright << 1 |ts_pes->original_or_copy;
		TSbuf[ts_pos + 7] = ts_pes->PTS_DTS_flags << 6 |ts_pes->ESCR_flag << 5 | ts_pes->ES_rate_flag << 4 |
			ts_pes->DSM_trick_mode_flag << 3 | ts_pes->additional_copy_info_flag << 2 | ts_pes->PES_CRC_flag << 1 | ts_pes->PES_extension_flag;
		TSbuf[ts_pos + 8] = ts_pes->PES_header_data_length;
		ts_pos += 9;

		if (ts_pes->stream_id == TS_H264_STREAM_ID)
		{
			TSbuf[ts_pos + 0] = (((0x3 << 4) | ((Videopts>> 29) & 0x0E) | 0x01) & 0xff);
			TSbuf[ts_pos + 1]= (((((Videopts >> 14) & 0xfffe) | 0x01) >> 8) & 0xff);
			TSbuf[ts_pos + 2]= ((((Videopts >> 14) & 0xfffe) | 0x01) & 0xff);
			TSbuf[ts_pos + 3]= (((((Videopts << 1) & 0xfffe) | 0x01) >> 8) & 0xff);
			TSbuf[ts_pos + 4]= ((((Videopts << 1) & 0xfffe) | 0x01) & 0xff);
			ts_pos += 5;

		}
		else if (ts_pes->stream_id == TS_AAC_STREAM_ID)
		{
			TSbuf[ts_pos + 0] = (((0x3 << 4) | ((Adudiopts>> 29) & 0x0E) | 0x01) & 0xff);
			TSbuf[ts_pos + 1]= (((((Adudiopts >> 14) & 0xfffe) | 0x01) >> 8) & 0xff);
			TSbuf[ts_pos + 2]= ((((Adudiopts >> 14) & 0xfffe) | 0x01) & 0xff);
			TSbuf[ts_pos + 3]= (((((Adudiopts << 1) & 0xfffe) | 0x01) >> 8) & 0xff);
			TSbuf[ts_pos + 4]= ((((Adudiopts << 1) & 0xfffe) | 0x01) & 0xff);
			ts_pos += 5;
		}
		else
		{
			printf("ts_pes->stream_id  error 0x%x \n",ts_pes->stream_id);
			return getchar();
		}
		memcpy(TSbuf + ts_pos,ts_pes->Es,ts_pes->Pes_Packet_Length_Beyond);  

		//����д���ļ�
		fwrite(TSbuf,188,1,fts_handler);                               //��һ������д���ļ�
		write_packet_no ++;                                                      //�Ѿ�д���ļ��İ�����++
		return write_packet_no;
	}
	
	NeafPacketCount += (ts_pes->Pes_Packet_Length_Beyond - FirstPacketLoadLength)/ 184;     
	NeafPacketCount += 1;                                                                   //���һ����Ƭ��
	AdaptiveLength = 188 - 4 - 1 - ((ts_pes->Pes_Packet_Length_Beyond - FirstPacketLoadLength)% 184)  ;  //Ҫ��д0XFF�ĳ���
	if ((write_packet_no % 40) == 0)                                                         //ÿ40������һ�� pat,һ��pmt
	{
		write_ts_pat(fts_handler,ts_pes->Es);                                                         //����PAT
		write_ts_pmt(fts_handler,ts_pes->Es);                                                         //����PMT
	}
	//��ʼ�����һ����,��Ƭ���ĸ�������Ҳ�������� 
	WriteStruct_Packetheader(TSbuf,Video_Audio_PID,0x01,0x03);                              //PID = TS_H264_PID,��Ч���ص�Ԫ��ʼָʾ��_play_init = 0x01, ada_field_C,0x03,���е����ֶκ���Ч���� ��
	ts_pos += 4;
	TSbuf[ts_pos] = ts_adaptation_field_head->adaptation_field_length;                      //����Ӧ�ֶεĳ��ȣ��Լ���д��
	ts_pos += 1;                                                       

	create_adaptive_ts(ts_adaptation_field_head,TSbuf + ts_pos,(188 - 4 - 1 - 14));          //��д����Ӧ�ֶ�
	ts_pos += ts_adaptation_field_head->adaptation_field_length;                            //��д����Ӧ������Ҫ�ĳ���

	TSbuf[ts_pos + 0] = (ts_pes->packet_start_code_prefix >> 16) & 0xFF;
	TSbuf[ts_pos + 1] = (ts_pes->packet_start_code_prefix >> 8) & 0xFF; 
	TSbuf[ts_pos + 2] = ts_pes->packet_start_code_prefix & 0xFF;
	TSbuf[ts_pos + 3] = ts_pes->stream_id;
	TSbuf[ts_pos + 4] = ((ts_pes->PES_packet_length) >> 8) & 0xFF;
	TSbuf[ts_pos + 5] = (ts_pes->PES_packet_length) & 0xFF;
	TSbuf[ts_pos + 6] = ts_pes->marker_bit << 6 | ts_pes->PES_scrambling_control << 4 | ts_pes->PES_priority << 3 |
		ts_pes->data_alignment_indicator << 2 | ts_pes->copyright << 1 |ts_pes->original_or_copy;
	TSbuf[ts_pos + 7] = ts_pes->PTS_DTS_flags << 6 |ts_pes->ESCR_flag << 5 | ts_pes->ES_rate_flag << 4 |
		ts_pes->DSM_trick_mode_flag << 3 | ts_pes->additional_copy_info_flag << 2 | ts_pes->PES_CRC_flag << 1 | ts_pes->PES_extension_flag;
	TSbuf[ts_pos + 8] = ts_pes->PES_header_data_length;
	ts_pos += 9;

	if (ts_pes->stream_id == TS_H264_STREAM_ID)
	{
		TSbuf[ts_pos + 0] = (((0x3 << 4) | ((Videopts>> 29) & 0x0E) | 0x01) & 0xff);
		TSbuf[ts_pos + 1]= (((((Videopts >> 14) & 0xfffe) | 0x01) >> 8) & 0xff);
		TSbuf[ts_pos + 2]= ((((Videopts >> 14) & 0xfffe) | 0x01) & 0xff);
		TSbuf[ts_pos + 3]= (((((Videopts << 1) & 0xfffe) | 0x01) >> 8) & 0xff);
		TSbuf[ts_pos + 4]= ((((Videopts << 1) & 0xfffe) | 0x01) & 0xff);
		ts_pos += 5;

	}
	else if (ts_pes->stream_id == TS_AAC_STREAM_ID)
	{
		TSbuf[ts_pos + 0] = (((0x3 << 4) | ((Adudiopts>> 29) & 0x0E) | 0x01) & 0xff);
		TSbuf[ts_pos + 1]= (((((Adudiopts >> 14) & 0xfffe) | 0x01) >> 8) & 0xff);
		TSbuf[ts_pos + 2]= ((((Adudiopts >> 14) & 0xfffe) | 0x01) & 0xff);
		TSbuf[ts_pos + 3]= (((((Adudiopts << 1) & 0xfffe) | 0x01) >> 8) & 0xff);
		TSbuf[ts_pos + 4]= ((((Adudiopts << 1) & 0xfffe) | 0x01) & 0xff);
		ts_pos += 5;
	}
	else
	{
		printf("ts_pes->stream_id  error 0x%x \n",ts_pes->stream_id);
		return getchar();
	}

	NeafBuf = ts_pes->Es ;
	memcpy(TSbuf + ts_pos,NeafBuf,FirstPacketLoadLength);  

	NeafBuf += FirstPacketLoadLength;
	ts_pes->Pes_Packet_Length_Beyond -= FirstPacketLoadLength;
	//����д���ļ�
	fwrite(TSbuf,188,1,fts_handler);                               //��һ������д���ļ�
	write_packet_no ++;                                                      //�Ѿ�д���ļ��İ�����++

	while(ts_pes->Pes_Packet_Length_Beyond)
	{
		ts_pos = 0;
		memset(TSbuf,0,TS_PACKET_SIZE); 

		if ((write_packet_no % 40) == 0)                                                         //ÿ40������һ�� pat,һ��pmt
		{
			write_ts_pat(fts_handler,ts_pes->Es);                                                         //����PAT
		write_ts_pmt(fts_handler,ts_pes->Es);                                                            //����PMT
		}
		if(ts_pes->Pes_Packet_Length_Beyond >= 184)
		{
			//�����м��   
			WriteStruct_Packetheader(TSbuf,Video_Audio_PID,0x00,0x01);     //PID = TS_H264_PID,������Ч���ص�Ԫ��ʼָʾ��_play_init = 0x00, ada_field_C,0x01,������Ч���أ�    
			ts_pos += 4;
            memcpy(TSbuf + ts_pos,NeafBuf,184); 
			NeafBuf += 184;
			ts_pes->Pes_Packet_Length_Beyond -= 184;
			fwrite(TSbuf,188,1,fts_handler); 
		}
		else
		{
			if(ts_pes->Pes_Packet_Length_Beyond == 183||ts_pes->Pes_Packet_Length_Beyond == 182)
			{
				if ((write_packet_no % 40) == 0)                                                         //ÿ40������һ�� pat,һ��pmt
				{
					write_ts_pat(fts_handler,ts_pes->Es);                                                         //����PAT
		                    write_ts_pmt(fts_handler,ts_pes->Es);                                                           //����PMT
				}

				WriteStruct_Packetheader(TSbuf,Video_Audio_PID,0x00,0x03);   //PID = TS_H264_PID,������Ч���ص�Ԫ��ʼָʾ��_play_init = 0x00, ada_field_C,0x03,���е����ֶκ���Ч���أ�
				ts_pos += 4;
				TSbuf[ts_pos + 0] = 0x01;
				TSbuf[ts_pos + 1] = 0x00;
				ts_pos += 2;
				memcpy(TSbuf + ts_pos,NeafBuf,182); 
				  
				NeafBuf += 182;
				ts_pes->Pes_Packet_Length_Beyond -= 182;
				fwrite(TSbuf,188,1,fts_handler); 
			}
			else
			{
				if ((write_packet_no % 40) == 0)                                                         //ÿ40������һ�� pat,һ��pmt
				{
					write_ts_pat(fts_handler,ts_pes->Es);                                                         //����PAT
		                    write_ts_pmt(fts_handler,ts_pes->Es);                                                       //����PMT
				}

				WriteStruct_Packetheader(TSbuf,Video_Audio_PID,0x00,0x03);  //PID = TS_H264_PID,������Ч���ص�Ԫ��ʼָʾ��_play_init = 0x00, ada_field_C,0x03,���е����ֶκ���Ч���أ�
				ts_pos += 4;
				TSbuf[ts_pos + 0] = 184-ts_pes->Pes_Packet_Length_Beyond-1 ;
				TSbuf[ts_pos + 1] = 0x00;
				ts_pos += 2;
				memset(TSbuf + ts_pos,0xFF,(184 - ts_pes->Pes_Packet_Length_Beyond - 2)); 
				ts_pos += (184-ts_pes->Pes_Packet_Length_Beyond-2);
				memcpy(TSbuf + ts_pos,NeafBuf,ts_pes->Pes_Packet_Length_Beyond);
				ts_pes->Pes_Packet_Length_Beyond = 0;
				fwrite(TSbuf,188,1,fts_handler);   //��һ������д���ļ�
				write_packet_no ++;  
			}
		}	
		write_packet_no ++;  
	}

	return write_packet_no ;
}



/*
@desc:
 read the h264 frame from h264_file
 read the aac  frame from aac_file
 then encapsulate the h264 frame and aac frame into ts _file
*/
int ts_mux_for_h264_aac( const char *h264_file, const char * aac_file, const char * ts_file )
{
       if( h264_file == NULL || aac_file == NULL || ts_file == NULL )
       return ARGUMENT_ERROR;
       
	unsigned long  h264_pts = 0; 
	unsigned long  aac_pts = 0; 
	unsigned int   aac_frame_samplerate = 0;   
	unsigned int   h264_frame_type =  0; 
	Ts_Adaptation_field  ts_adaptation_field_head ; 
	Ts_Adaptation_field  ts_adaptation_field_tail ;
    
       TsPes h264_pes;
       TsPes aac_pes;
       //open these media files first
	FILE *fh264_handler = fopen( h264_file,"r" );
       FILE *faac_handler = fopen( aac_file,"r" );
       FILE *fts_handler = fopen( ts_file,"w" );
       //obtain the aac file's samplerate
       aac_frame_samplerate = obtain_aac_file_samplerate( aac_file);
	unsigned int frame_length = 0;
       bool handle_h264_done = false;
       bool handle_aac_done = false;

       unsigned char h264_frame[MAX_ONE_FRAME_SIZE];
       unsigned char aac_frame[MAX_ONE_FRAME_SIZE];
       int ret;
       
	for (;;)
	{
	       if( handle_h264_done )
              break;
           
		/* write interleaved audio and video frames */
		if ( (h264_pts <= aac_pts) && !handle_h264_done)
		{
                    ret = read_h264_frame(fh264_handler,h264_frame,frame_length, h264_frame_type);
                    if( ret != OK )
                    {
                        handle_h264_done = true;
                        continue;
                    }
                    
                    h264_frame_2_pes(h264_frame,frame_length,h264_pts,h264_pes); 
			if ( h264_pes.Pes_Packet_Length_Beyond != 0 )
			{
				printf("PES_VIDEO  :  SIZE = %d\n",h264_pes.Pes_Packet_Length_Beyond);
			       write_adaptive_head_fields(&ts_adaptation_field_head,h264_pts); //��д����Ӧ�α�־֡ͷ
				write_adaptive_tail_fields(&ts_adaptation_field_tail); //��д����Ӧ�α�־֡β
					//����һ֡��Ƶ����ʱ��
				pes_2_ts(fts_handler,&h264_pes,TS_H264_PID ,&ts_adaptation_field_head ,&ts_adaptation_field_tail,h264_pts,aac_pts);
				h264_pts += 1000* 90 /H264_FRAME_RATE;   //90khz
			}
		}
		else if( !handle_aac_done )
		{
			ret = read_aac_frame( faac_handler,aac_frame,frame_length );
                    if( ret != OK )
                    {
                        handle_aac_done = true;
                        continue;
                    }
                    
                    aac_frame_2_pes( aac_frame,frame_length,aac_pts,aac_pes);
			if (aac_pes.Pes_Packet_Length_Beyond != 0)
			{
				printf("PES_AUDIO  :  SIZE = %d\n",aac_pes.Pes_Packet_Length_Beyond);
				//��д����Ӧ�α�־
				write_adaptive_tail_fields(&ts_adaptation_field_head); //��д����Ӧ�α�־  ,����ע�� ��Ƶ���Ͳ�Ҫ��pcr ���Զ���֡β�������
				write_adaptive_tail_fields(&ts_adaptation_field_tail); //��д����Ӧ�α�־֡β
				pes_2_ts(fts_handler,&aac_pes,TS_AAC_PID ,&ts_adaptation_field_head ,&ts_adaptation_field_tail,h264_pts,aac_pts);
				//����һ֡��Ƶ����ʱ��
				aac_pts += 1024*1000* 90/aac_frame_samplerate;
			}
            
		}
	}
	return OK;
}



