#include "Mux.h"

#define FRAMETATE 60

int main()
{
	printf("--------�������п�ʼ----------\n");
	pVideo_H264_File = OpenFile(INPUTH264FILENAME,"rb");
	pAudio_Aac_File = OpenFile(INPUTAACFILENAME,"rb");
	pVideo_Audio_Ts_File = OpenFile(OUTPUTTSFILENAME,"wb");

	//////////////////////////////////////////////////////////////////////////
	WriteBuf2File(FRAMETATE);
	//////////////////////////////////////////////////////////////////////////

	if (pVideo_H264_File)
	{
		CloseFile(pVideo_H264_File);
		pVideo_H264_File = NULL;
	}
	if (pAudio_Aac_File)
	{
		CloseFile(pAudio_Aac_File);
		pAudio_Aac_File = NULL;
	}
	if (pVideo_Audio_Ts_File)
	{
		CloseFile(pVideo_Audio_Ts_File);
		pVideo_Audio_Ts_File = NULL;
	}
	printf("--------�������н���----------\n");
	printf("-------�밴������˳�---------\n");
	return getchar();
}