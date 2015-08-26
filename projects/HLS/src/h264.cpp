/*
@author:chenzhengqiang
@version:1.0
@start date:2015/8/26
@modified date:
@desc:providing the api for handling h264 file
*/

#include "errors.h"
#include "h264.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

NALU_t *AllocNALU(int buffersize)
{
	NALU_t *nal_unit;

	if ((nal_unit = (NALU_t*)calloc (1, sizeof(NALU_t))) == NULL)
	{
		printf("AllocNALU Error: Allocate Meory To NALU_t Failed ");
		getchar();
	}

	nal_unit->max_size = buffersize;									//Assign buffer size 

	if ((nal_unit->buf = (unsigned char*)calloc (buffersize, sizeof (char))) == NULL)
	{
		free (nal_unit);
		printf ("AllocNALU Error: Allocate Meory To NALU_t Buffer Failed ");
		getchar();
	}
	return nal_unit;
}

void FreeNALU(NALU_t *nal_unit)
{
	if (nal_unit)
	{
		if (nal_unit->buf)
		{
			free(nal_unit->buf);
			nal_unit->buf=NULL;
		}
		free (nal_unit);
	}
}

int FindStartCode2 (unsigned char *Buf)
{
	if(Buf[0]!=0 || Buf[1]!=0 || Buf[2] !=1)               //Check whether buf is 0x000001
	{
		return 0;
	}
	else 
	{
		return 1;
	}
}

int FindStartCode3 (unsigned char *Buf)
{
	if(Buf[0]!=0 || Buf[1]!=0 || Buf[2] !=0 || Buf[3] !=1)  //Check whether buf is 0x00000001
	{
		return 0;
	}
	else 
	{
		return 1;
	}
}

int GetAnnexbNALU ( FILE *fh264_handler, NALU_t * nalu )
{
	int pos = 0;                  //һ��nal����һ��nal �����ƶ���ָ��
	int StartCodeFound  = 0;      //�Ƿ��ҵ���һ��nal ��ǰ׺
	int rewind = 0;               //�ж� ǰ׺��ռ�ֽ��� 3�� 4
	unsigned char * Buf = NULL;
	static int info2 =0 ;
	static int info3 =0 ;

	if ((Buf = (unsigned char*)calloc (nalu->max_size , sizeof(char))) == NULL) 
	{
		printf ("GetAnnexbNALU Error: Could not allocate Buf memory\nal_unit");
	}

	nalu->startcodeprefix_len = 3;      //��ʼ��ǰ׺λ�����ֽ�

	if (3 != fread (Buf, 1, 3, fh264_handler))//���ļ���ȡ�����ֽڵ�buf
	{
		free(Buf);
		return 0;
	}
	info2 = FindStartCode2 (Buf);       //Check whether Buf is 0x000001
	if(info2 != 1) 
	{
		//If Buf is not 0x000001,then read one more byte
		if(1 != fread(Buf + 3, 1, 1, fh264_handler))
		{
			free(Buf);
			return 0;
		}
		info3 = FindStartCode3 (Buf);   //Check whether Buf is 0x00000001
		if (info3 != 1)                 //If not the return -1
		{ 
			free(Buf);
			return -1;
		}
		else 
		{
			//If Buf is 0x00000001,set the prefix length to 4 bytes
			pos = 4;
			nalu->startcodeprefix_len = 4;
		}
	} 
	else
	{
		//If Buf is 0x000001,set the prefix length to 3 bytes
		pos = 3;
		nalu->startcodeprefix_len = 3;
	}
	//Ѱ����һ���ַ�����λ�� �� Ѱ��һ��nal ��һ��0000001 ����һ��00000001
	StartCodeFound = 0;
	info2 = 0;
	info3 = 0;
	while (!StartCodeFound)
	{
		if (feof (fh264_handler))                                 //��������ļ���β
		{
			nalu->len = (pos-1) - nalu->startcodeprefix_len;  //��0 ��ʼ
			memcpy (nalu->buf, &Buf[nalu->startcodeprefix_len], nalu->len);     
			nalu->forbidden_bit = nalu->buf[0] & 0x80;      // 1 bit--10000000
			nalu->nal_reference_idc = nalu->buf[0] & 0x60;  // 2 bit--01100000
			nalu->nal_unit_type = (nalu->buf[0]) & 0x1f;    // 5 bit--00011111
			free(Buf);
			return ((info3 == 1)? 4 : 3);
		}
		Buf[pos++] = fgetc (fh264_handler);                       //Read one char to the Buffer һ���ֽ�һ���ֽڴ��ļ������
		info3 = FindStartCode3(&Buf[pos-4]);		        //Check whether Buf is 0x00000001 
		if(info3 != 1)
		{
			info2 = FindStartCode2(&Buf[pos-3]);            //Check whether Buf is 0x000001
		}
		StartCodeFound = (info2 == 1 || info3 == 1);        //����ҵ���һ��ǰ׺
	}

	rewind = (info3 == 1)? -4 : -3;

	if (0 != fseek (fh264_handler, rewind, SEEK_CUR))			    //���ļ��ڲ�ָ���ƶ��� nal ��ĩβ
	{
		free(Buf);
		printf("GetAnnexbNALU Error: Cannot fseek in the bit stream file");
	}

	nalu->len = (pos + rewind) -  nalu->startcodeprefix_len;       //���ð���nal ͷ�����ݳ���
	memcpy (nalu->buf, &Buf[nalu->startcodeprefix_len], nalu->len);//����һ��nal ���ݵ�������
	nalu->forbidden_bit = nalu->buf[0] & 0x80;                     //1 bit  ����nal ͷ
	nalu->nal_reference_idc = nalu->buf[0] & 0x60;                 // 2 bit
	nalu->nal_unit_type = (nalu->buf[0]) & 0x1f;                   // 5 bit
	free(Buf);
	return ((info3 == 1)? 4 : 3);                                               
}

int GetFrameType(NALU_t * nal)
{
	bs_t s;
	int frame_type = 0; 
	unsigned char * OneFrameBuf_H264 = NULL ;
	if ((OneFrameBuf_H264 = (unsigned char *)calloc(nal->len + 4,sizeof(unsigned char))) == NULL)
	{
		printf("Error malloc OneFrameBuf_H264\nal_unit");
		return getchar();
	}
	if (nal->startcodeprefix_len == 3)
	{
		OneFrameBuf_H264[0] = 0x00;
		OneFrameBuf_H264[1] = 0x00;
		OneFrameBuf_H264[2] = 0x01;
		memcpy(OneFrameBuf_H264 + 3,nal->buf,nal->len);
	}
	else if (nal->startcodeprefix_len == 4)
	{
		OneFrameBuf_H264[0] = 0x00;
		OneFrameBuf_H264[1] = 0x00;
		OneFrameBuf_H264[2] = 0x00;
		OneFrameBuf_H264[3] = 0x01;
		memcpy(OneFrameBuf_H264 + 4,nal->buf,nal->len);
	}
	else
	{
		printf("H264��ȡ����\nal_unit");
	}
	bs_init( &s,OneFrameBuf_H264 + nal->startcodeprefix_len + 1  ,nal->len - 1 );


	if (nal->nal_unit_type == NAL_SLICE || nal->nal_unit_type ==  NAL_SLICE_IDR )
	{
		/* i_first_mb */
		bs_read_ue( &s );
		/* picture type */
		frame_type =  bs_read_ue( &s );
		switch(frame_type)
		{
		case 0: case 5: /* P */
			nal->Frametype = FRAME_P;
			break;
		case 1: case 6: /* B */
			nal->Frametype = FRAME_B;
			break;
		case 3: case 8: /* SP */
			nal->Frametype = FRAME_P;
			break;
		case 2: case 7: /* I */
			nal->Frametype = FRAME_I;
			break;
		case 4: case 9: /* SI */
			nal->Frametype = FRAME_I;
			break;
		}
	}
	else if (nal->nal_unit_type == NAL_SEI)
	{
		nal->Frametype = NAL_SEI;
	}
	else if(nal->nal_unit_type == NAL_SPS)
	{
		nal->Frametype = NAL_SPS;
	}
	else if(nal->nal_unit_type == NAL_PPS)
	{
		nal->Frametype = NAL_PPS;
	}
	if (OneFrameBuf_H264)
	{
		free(OneFrameBuf_H264);
		OneFrameBuf_H264 = NULL;
	}
	return 1;
}


/*
@desc:
 read a frame from h264 file and then 
*/
int read_h264_frame(FILE *fh264_handler,unsigned char * h264_frame,
                             unsigned int & frame_length, unsigned int & frame_type)
{
	NALU_t * nal_unit = NULL;
	//����nal ��Դ
	nal_unit = AllocNALU(MAX_VIDEO_TAG_BUF_SIZE); 

	//��ȡһ֡����
	int startcodeprefix_size = GetAnnexbNALU( fh264_handler, nal_unit );
	if (startcodeprefix_size == 0)
	{
		return FILE_EOF;
	}

	//�ж�֡����
	GetFrameType(nal_unit);
	frame_type = nal_unit->Frametype;

	if (nal_unit->startcodeprefix_len == 3)
	{
		h264_frame[0] = 0x00;
		h264_frame[1] = 0x00;
		h264_frame[2] = 0x01;
		memcpy(h264_frame + 3,nal_unit->buf,nal_unit->len);
	}
	else if (nal_unit->startcodeprefix_len == 4)
	{
		h264_frame[0] = 0x00;
		h264_frame[1] = 0x00;
		h264_frame[2] = 0x00;
		h264_frame[3] = 0x01;
		memcpy( h264_frame + 4,nal_unit->buf,nal_unit->len);
	}
	else
	{
		return FILE_FORMAT_ERROR;
	}

	frame_length = nal_unit->startcodeprefix_len + nal_unit->len;

	FreeNALU(nal_unit);                                                   //�ͷ�nal ��Դ 
	return OK;
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
