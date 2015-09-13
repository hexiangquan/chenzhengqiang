/*
@author:chenzhengqiang
@version:1.0
@start date:2015/8/26
@modified date:
@desc:providing the api that encapsulate the h264 file and aac file into ts file frame by frame
*/

#include "errors.h"
#include "aac.h"
#include "h264.h"
#include "ts_muxer.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>


static const int H264_FRAME_RATE = 30;

int write_ts_pat_2_file(FILE *fts_handler,unsigned char * buf)
{
	write_ts_pat(buf);
	size_t writen_bytes = fwrite((char *)buf,sizeof(unsigned char),TS_PACKET_SIZE, fts_handler );
       if( writen_bytes < TS_PACKET_SIZE )
       {
           if( feof(fts_handler))
           return FILE_EOF;
           return SYSTEM_ERROR;
       }
       return OK;
}

int write_ts_pmt_2_file(FILE *fts_handler,unsigned char * buf)
{
	write_ts_pmt(buf);
	size_t writen_bytes = fwrite( (char *)buf,sizeof(unsigned char),TS_PACKET_SIZE, fts_handler);
       if( writen_bytes < TS_PACKET_SIZE )
       {
           if( feof(fts_handler))
           return FILE_EOF;
           return SYSTEM_ERROR;
       }
       return OK;
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



/*
@returns:void
@desc:encapsulate pes with aac frame buffer and the display timestamp
*/
void aac_frame_2_pes( unsigned char *aac_frame, unsigned int frame_length, unsigned long aac_pts,TsPes  & aac_pes )
{
       
       memcpy(aac_pes.Es,aac_frame,frame_length );
	unsigned int aacpes_pos = 0;
	aacpes_pos += frame_length ;

	aac_pes.packet_start_code_prefix = 0x000001;
	aac_pes.stream_id = TS_AAC_STREAM_ID;                                //E0~EF��ʾ����Ƶ��,C0~DF����Ƶ,H264-- E0
	aac_pes.PES_packet_length = 0 ; // frame_length + 8 ;             //һ֡���ݵĳ��� ������ PES��ͷ ,8����Ӧ�εĳ���
	aac_pes.Pes_Packet_Length_Beyond = frame_length;                  //= OneFrameLen_aac;     //���������һ֡  
	if (frame_length > 0xFFFF)                                          //���һ֡���ݵĴ�С��������
	{
		aac_pes.PES_packet_length = 0x00;
		aac_pes.Pes_Packet_Length_Beyond = frame_length;  
		aacpes_pos += 16;
	}
	else
	{
		aac_pes.PES_packet_length = 0x00;
		aac_pes.Pes_Packet_Length_Beyond = frame_length;  
		aacpes_pos += 14;
	}
	aac_pes.marker_bit = 0x02;
	aac_pes.PES_scrambling_control = 0x00;                               //��ѡ�ֶ� ���ڣ�������
	aac_pes.PES_priority = 0x00;
	aac_pes.data_alignment_indicator = 0x00;
	aac_pes.copyright = 0x00;
	aac_pes.original_or_copy = 0x00;
	aac_pes.PTS_DTS_flags = 0x02;                                        //10'��PTS�ֶδ���,DTS������
	aac_pes.ESCR_flag = 0x00;
	aac_pes.ES_rate_flag = 0x00;
	aac_pes.DSM_trick_mode_flag = 0x00;
	aac_pes.additional_copy_info_flag = 0x00;
	aac_pes.PES_CRC_flag = 0x00;
	aac_pes.PES_extension_flag = 0x00;
	aac_pes.PES_header_data_length = 0x05;                               //��������� ������PTS��ռ���ֽ���

	aac_pes.tsptsdts.pts_32_30  = 0;
	aac_pes.tsptsdts.pts_29_15 = 0;
	aac_pes.tsptsdts.pts_14_0 = 0;

	aac_pes.tsptsdts.reserved_1 = 0x03;                                 //��д pts��Ϣ
	
	
	//if aac frame's pts greater than 30 bit,then use the higest 3 bit
	if( aac_pts > 0x7FFFFFFF )
	{
		aac_pes.tsptsdts.pts_32_30 = (aac_pts >> 30) & 0x07;                 
		aac_pes.tsptsdts.marker_bit1 = 0x01;
	}
	else 
	{
		aac_pes.tsptsdts.marker_bit1 = 0;
	}
	// if greater than 15bit,then use more bit to save the pts
	if( aac_pts > 0x7FFF )
	{
		aac_pes.tsptsdts.pts_29_15 = (aac_pts >> 15) & 0x007FFF ;
		aac_pes.tsptsdts.marker_bit2 = 0x01;
	}
	else
	{
		aac_pes.tsptsdts.marker_bit2 = 0;
	}
    
	//use the last 15 bit
	aac_pes.tsptsdts.pts_14_0 = aac_pts & 0x007FFF;
	aac_pes.tsptsdts.marker_bit3 = 0x01;
}


/*
@desc:
 encapsulate the h264 frame to pes packet
*/
int h264_frame_2_pes(unsigned char *h264_frame,unsigned int frame_length,unsigned long h264_pts,TsPes & h264_pes)
{
	unsigned int h264pes_pos = 0;
	h264pes_pos += frame_length;

       memcpy(h264_pes.Es,h264_frame,frame_length);
	h264_pes.packet_start_code_prefix = 0x000001;
	h264_pes.stream_id = TS_H264_STREAM_ID;                               //E0~EF��ʾ����Ƶ��,C0~DF����Ƶ,H264-- E0
	h264_pes.PES_packet_length = 0 ;                                      //һ֡���ݵĳ��� ������ PES��ͷ ,���8 �� ����Ӧ�ĳ���,��0 �����Զ�����
	h264_pes.Pes_Packet_Length_Beyond = frame_length;

	if ( frame_length> 0xFFFF)                                          //���һ֡���ݵĴ�С��������
	{
		h264_pes.PES_packet_length = 0x00;
		h264_pes.Pes_Packet_Length_Beyond = frame_length;
		h264pes_pos += 16;
	}
	else
	{
		h264_pes.PES_packet_length = 0x00;
		h264_pes.Pes_Packet_Length_Beyond = frame_length;
		h264pes_pos += 14;
	}
	h264_pes.marker_bit = 0x02;
	h264_pes.PES_scrambling_control = 0x00;                               //��ѡ�ֶ� ���ڣ�������
	h264_pes.PES_priority = 0x00;
	h264_pes.data_alignment_indicator = 0x00;
	h264_pes.copyright = 0x00;
	h264_pes.original_or_copy = 0x00;
	h264_pes.PTS_DTS_flags = 0x02;                                         //10'��PTS�ֶδ���,DTS������
	h264_pes.ESCR_flag = 0x00;
	h264_pes.ES_rate_flag = 0x00;
	h264_pes.DSM_trick_mode_flag = 0x00;
	h264_pes.additional_copy_info_flag = 0x00;
	h264_pes.PES_CRC_flag = 0x00;
	h264_pes.PES_extension_flag = 0x00;
	h264_pes.PES_header_data_length = 0x05;                                //��������� ������PTS��ռ���ֽ���

	//�� 0 
	h264_pes.tsptsdts.pts_32_30  = 0;
	h264_pes.tsptsdts.pts_29_15 = 0;
	h264_pes.tsptsdts.pts_14_0 = 0;

	h264_pes.tsptsdts.reserved_1 = 0x0003;                                 //��д pts��Ϣ
	// Videopts����30bit��ʹ�������λ 
	if(h264_pts > 0x7FFFFFFF)
	{
		h264_pes.tsptsdts.pts_32_30 = (h264_pts >> 30) & 0x07;                 
		h264_pes.tsptsdts.marker_bit1 = 0x01;
	}
	else 
	{
		h264_pes.tsptsdts.marker_bit1 = 0;
	}
	// Videopts����15bit��ʹ�ø����λ���洢
	if(h264_pts > 0x7FFF)
	{
		h264_pes.tsptsdts.pts_29_15 = (h264_pts >> 15) & 0x007FFF ;
		h264_pes.tsptsdts.marker_bit2 = 0x01;
	}
	else
	{
		h264_pes.tsptsdts.marker_bit2 = 0;
	}
	//ʹ�����15λ
	h264_pes.tsptsdts.pts_14_0 = h264_pts & 0x007FFF;
	h264_pes.tsptsdts.marker_bit3 = 0x01;

	return h264pes_pos;
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
		write_ts_packet_header(TSbuf,Video_Audio_PID,0x01,0x03);                          //PID = TS_H264_PID,��Ч���ص�Ԫ��ʼָʾ��_play_init = 0x01, ada_field_C,0x03,���е����ֶκ���Ч���� ��
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
		write_ts_pat_2_file(fts_handler,ts_pes->Es);                                                         //����PAT
		write_ts_pmt_2_file(fts_handler,ts_pes->Es);                                                         //����PMT
	}
	//��ʼ�����һ����,��Ƭ���ĸ�������Ҳ�������� 
	write_ts_packet_header(TSbuf,Video_Audio_PID,0x01,0x03);                              //PID = TS_H264_PID,��Ч���ص�Ԫ��ʼָʾ��_play_init = 0x01, ada_field_C,0x03,���е����ֶκ���Ч���� ��
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
			write_ts_pat_2_file(fts_handler,ts_pes->Es);                                                         //����PAT
		write_ts_pmt_2_file(fts_handler,ts_pes->Es);                                                            //����PMT
		}
		if(ts_pes->Pes_Packet_Length_Beyond >= 184)
		{
			//�����м��   
			write_ts_packet_header(TSbuf,Video_Audio_PID,0x00,0x01);     //PID = TS_H264_PID,������Ч���ص�Ԫ��ʼָʾ��_play_init = 0x00, ada_field_C,0x01,������Ч���أ�    
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
					write_ts_pat_2_file(fts_handler,ts_pes->Es);                                                         //����PAT
		                    write_ts_pmt_2_file(fts_handler,ts_pes->Es);                                                           //����PMT
				}

				write_ts_packet_header(TSbuf,Video_Audio_PID,0x00,0x03);   //PID = TS_H264_PID,������Ч���ص�Ԫ��ʼָʾ��_play_init = 0x00, ada_field_C,0x03,���е����ֶκ���Ч���أ�
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
					write_ts_pat_2_file(fts_handler,ts_pes->Es);                                                         //����PAT
		                    write_ts_pmt_2_file(fts_handler,ts_pes->Es);                                                       //����PMT
				}

				write_ts_packet_header(TSbuf,Video_Audio_PID,0x00,0x03);  //PID = TS_H264_PID,������Ч���ص�Ԫ��ʼָʾ��_play_init = 0x00, ada_field_C,0x03,���е����ֶκ���Ч���أ�
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
                           //write the ts's adaptation fields
			       write_adaptive_head_fields(&ts_adaptation_field_head,h264_pts);
				write_adaptive_tail_fields(&ts_adaptation_field_tail); 
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
				//write the ts's adaptation fields
				write_adaptive_tail_fields(&ts_adaptation_field_head); //��д����Ӧ�α�־  ,����ע�� ��Ƶ���Ͳ�Ҫ��pcr ���Զ���֡β�������
				write_adaptive_tail_fields(&ts_adaptation_field_tail); //��д����Ӧ�α�־֡β
				pes_2_ts(fts_handler,&aac_pes,TS_AAC_PID ,&ts_adaptation_field_head ,&ts_adaptation_field_tail,h264_pts,aac_pts);
				aac_pts += 1024*1000* 90/aac_frame_samplerate;
			}
            
		}
	}
	return OK;
}

