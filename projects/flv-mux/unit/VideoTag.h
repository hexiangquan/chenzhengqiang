#ifndef _CZQ_VIDEO_TAG_H_
#define _CZQ_VIDEO_TAG_H_
#include "Flv.h"
#include "Mybs.h"

#define  MAX_VIDEO_TAG_BUF_SIZE   1024 * 1024
#define  VIDEO_TAG_HEADER_LENGTH  11

extern unsigned int I_Frame_Num ; 
extern unsigned int decode_video_done;

typedef struct Tag_Video_AvcC
{
	unsigned char configurationVersion;  //8��= 0x01
	unsigned char AVCProfileIndication;  //sps��sps�ĵ�2�ֽ�,��ν��AVCProfileIndication
	unsigned char profile_compatibility; //sps��sps�ĵ�3�ֽ�,��ν��profile_compatibility
	unsigned char AVCLevelIndication;    //sps��sps�ĵ�4�ֽ�,��ν��AVCLevelIndication
	unsigned char reserved_1;            //��111111��b;
	unsigned char lengthSizeMinusOne;    //NALUnitLength �ĳ��� -1 һ��Ϊ0x03
	unsigned char reserved_2;            //��111��b;
	unsigned char numOfSequenceParameterSets;  //һ�㶼��һ��
	unsigned int sequenceParameterSetLength;   //sps����
	unsigned char * sequenceParameterSetNALUnit; //sps����
	unsigned char numOfPictureParameterSets;   //һ�㶼��һ��
	unsigned int  pictureParameterSetLength;   //pps����
	unsigned char * pictureParameterSetNALUnit;//pps����
	unsigned char reserved_3;
	unsigned char chroma_format;
	unsigned char reserved_4;
	unsigned char bit_depth_luma_minus8;
	unsigned char reserved_5;
	unsigned char bit_depth_chroma_minus8;
	unsigned char numOfSequenceParameterSetExt;
	unsigned int sequenceParameterSetExtLength;
	unsigned char * sequenceParameterSetExtNALUnit;
}Video_AvcC;


//����tag��header��tag��data
typedef struct Tag_Video_Tag                           
{
	unsigned char Type ;                       //��Ƶ��0x08������Ƶ��0x09����script data��0x12����������
	unsigned int  DataSize;                    //������tagheader �ĳ���
	unsigned int  Timestamp;                   //��5-7�ֽ�ΪUI24���͵�ֵ����ʾ��Tag��ʱ�������λΪms������һ��Tag��ʱ�������0��
	unsigned char TimestampExtended;           //��8���ֽ�Ϊʱ�������չ�ֽڣ���24λ��ֵ����ʱ�����ֽ���Ϊ���λ��ʱ�����չΪ32λֵ��
	unsigned int  StreamID;                    //��9-11�ֽ�ΪUI24���͵�ֵ����ʾstream id������0��
	//1: keyframe (for AVC, a seekable frame)
	//2: inter frame (for AVC, a nonseekable frame)
	//3: disposable inter frame (H.263 only)
	//4: generated keyframe (reserved for server use only)
	//5: video info/command frame
	unsigned char FrameType;                   //֡����
	//1: JPEG (currently unused)
	//2: Sorenson H.263
	//3: Screen video
	//4: On2 VP6
	//5: On2 VP6 with alpha channel
	//6: Screen video version 2
	//7: AVC
	unsigned char CodecID ;                    //CodecID
	//VideoData
	//	If CodecID == 2
	//  H263VIDEOPACKET
	//  If CodecID == 3
	//	SCREENVIDEOPACKET
	//	If CodecID == 4
	//	VP6FLVVIDEOPACKET
	//	If CodecID == 5
	//	VP6FLVALPHAVIDEOPACKET
	//	If CodecID == 6
	//	SCREENV2VIDEOPACKET
	//	if CodecID == 7
	//	AVCVIDEOPACKET
	//	Video frame payload or UI8
	//	(see note following table)
	//AVC sequence header
	//1: AVC NALU
	//2: AVC end of sequence (lower level NALU sequence ender is not required or supported)
	unsigned char AVCPacketType;               //packettype
	//if AVCPacketType == 1
	//Composition time offset
	//else
	//0
	unsigned int CompositionTime;              //AVCʱ��ȫ0��������,����tag��ʱ�䣬��Ϊ0
	//if AVCPacketType == 0
	//AVCDecoderConfigurationRecord
	//else if AVCPacketType == 1
	//One or more NALUs (can be individual
	//slices per FLV packets; that is, full frames
	//are not strictly required)
	//else if AVCPacketType == 2
	//Empty
	Video_AvcC  * video_avcc;
	unsigned char * Data;                     
}Video_Tag;

//H264һ֡���ݵĽṹ��
typedef struct Tag_NALU_t
{
	unsigned char forbidden_bit;           //! Should always be FALSE
	unsigned char nal_reference_idc;       //! NALU_PRIORITY_xxxx
	unsigned char nal_unit_type;           //! NALU_TYPE_xxxx  
	unsigned int  startcodeprefix_len;      //! ǰ׺�ֽ���
	unsigned int  len;                     //! ����nal ͷ��nal ���ȣ��ӵ�һ��00000001����һ��000000001�ĳ���
	unsigned int  max_size;                //! ����һ��nal �ĳ���
	unsigned char * buf;                   //! ����nal ͷ��nal ����
	unsigned char Frametype;               //! ֡����
	unsigned int  lost_packets;            //! Ԥ��
} NALU_t;

//nal����
enum nal_unit_type_e
{
	NAL_UNKNOWN     = 0,
	NAL_SLICE       = 1,
	NAL_SLICE_DPA   = 2,
	NAL_SLICE_DPB   = 3,
	NAL_SLICE_DPC   = 4,
	NAL_SLICE_IDR   = 5,    /* ref_idc != 0 */
	NAL_SEI         = 6,    /* ref_idc == 0 */
	NAL_SPS         = 7,
	NAL_PPS         = 8
	/* ref_idc == 0 for 6,9,10,11,12 */
};

//֡����
enum Frametype_e
{
	FRAME_I  = 15,
	FRAME_P  = 16,
	FRAME_B  = 17
};


unsigned int Create_AVCDecoderConfigurationRecord(unsigned char * buf,unsigned char * spsbuf,unsigned int spslength,unsigned char * ppsbuf,unsigned int ppslength);
NALU_t *AllocNALU(int buffersize);   //����nal ��Դ
void FreeNALU(NALU_t * n);           //�ͷ�nal ��Դ 
int FindStartCode2 (unsigned char *Buf);         //�ж�nal ǰ׺�Ƿ�Ϊ3���ֽ�
int FindStartCode3 (unsigned char *Buf);         //�ж�nal ǰ׺�Ƿ�Ϊ4���ֽ�
int GetAnnexbNALU (NALU_t *nalu);                //��дnal ���ݺ�ͷ
int GetFrameType(NALU_t * n);                    //��ȡ֡����
int TraverseH264File();                          //����H264�ļ����ڲ���I֡��Ŀ
int WriteStruct_H264_Tag(unsigned char * Buf,unsigned int  Timestamp,unsigned char AACPacketType/*AAC����ͷ��*/,unsigned int * video_frame_type);
#endif
