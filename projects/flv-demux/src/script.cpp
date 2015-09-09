#include "script.h"

double char2double(unsigned char * buf,unsigned int size)
{
	double scr = 0.0;
	unsigned char buf_1[8];
	unsigned char buf_2[8];
	memcpy(buf_1,buf,size);
	//��С������
	buf_2[0] = buf_1[7];
	buf_2[1] = buf_1[6];
	buf_2[2] = buf_1[5];
	buf_2[3] = buf_1[4];
	buf_2[4] = buf_1[3];
	buf_2[5] = buf_1[2];
	buf_2[6] = buf_1[1];
	buf_2[7] = buf_1[0];
	scr = *(double *)buf_2;
	return scr;
}

void double2char(unsigned char * buf,double val)
{
	*(double *)buf = val;
}

int AllocStruct_Script_Tag(Script_Tag ** scripttag)
{
	Script_Tag * scripttag_t = * scripttag;
	if ((scripttag_t = (Script_Tag *)calloc(1,sizeof(Script_Tag))) == NULL)
	{
		printf ("Error: Allocate Meory To AllocStruct_Script_Tag Buffer Failed ");
		return getchar();
	} 
	if ((scripttag_t->Data = (unsigned char * )calloc(ONE_SCRIPT_FRAME_SIZE,sizeof(unsigned char))) == NULL)
	{
		printf ("Error: Allocate Meory To scripttag_t->Data Buffer Failed ");
		return getchar();
	}
	* scripttag = scripttag_t;
	return 1;
}

int FreeStruct_Script_Tag(Script_Tag * scripttag)
{
	if (scripttag)
	{
		if (scripttag->Data)
		{
			free(scripttag->Data);
			scripttag->Data = NULL;
		}
		free(scripttag);
		scripttag = NULL;
	}
	return 1;
}

int ReadStruct_Script_Tag(unsigned char * Buf , unsigned int length ,Script_Tag * tag)
{
	int Script_Tag_pos = 0;
	int Arry_byte_length;
	unsigned char Arry_Name[MAX_ECMAARAY_NAME_LENGH];
	unsigned char Arry_InFomation;
	unsigned char Arry_InFomation_framekey;
	unsigned int  Arry_Name_framekey_Arry_length;

	//��ȡͷ��11�ֽ�
	tag->Type = Buf[0];
	tag->DataSize = 
		Buf[1]  << 16 |
		Buf[2]  << 8  |
		Buf[3];
	tag->Timestamp = 
		Buf[4]  << 16 |
		Buf[5]  << 8  |
		Buf[6];
	tag->TimestampExtended = Buf[7];
	tag->StreamID = 
		Buf[8]  << 16 |
		Buf[9]  << 8  |
		Buf[10];
	Script_Tag_pos += 11;

	//��ȡ��һ��AMF��
	tag->Type_1 = Buf[Script_Tag_pos];
	Script_Tag_pos ++;
	if (tag->Type_1 == 0x02)
	{
		tag->StringLength = 
			Buf[Script_Tag_pos]   << 8 |
			Buf[Script_Tag_pos+1];
		Script_Tag_pos +=2;
		//������Ϣ���̶�Ϊ0x6F 0x6E 0x4D 0x65 0x74 0x64 0x44 0x61 0x74 0x61����ʾ�ַ���onMetaData

		Script_Tag_pos +=tag->StringLength;
	}
	//��ȡ�ڶ���AMF��
	tag->Type_1 = Buf[Script_Tag_pos];
	Script_Tag_pos ++;
	if (tag->Type_1 == 0x08)
	{
		tag->ECMAArrayLength =                   //��ʾ��������metadata array data ���ж���������
			Buf[Script_Tag_pos]     << 24 |
			Buf[Script_Tag_pos+1]   << 16 |
			Buf[Script_Tag_pos+2]   << 8  |
			Buf[Script_Tag_pos+3];
		Script_Tag_pos += 4;
	}

	for (int i = 0 ; i< tag->ECMAArrayLength ; i++)  //һ���ж��������ݣ����ж��ٸ����ƣ����ߣ���������������Ϣ
	{
		//�����ж����ǲ���������Script_Tag��ĩβ��־���п��ܻ���� ����ĸ��� < tag->ECMAArrayLength �����
	    if (Buf[Script_Tag_pos]  == 0x00 && Buf[Script_Tag_pos + 1]  == 0x00 && Buf[Script_Tag_pos + 2]  == 0x00 && Buf[Script_Tag_pos + 3]  == 0x09)
		{
			break;
		}

		//ǰ��2bytes��ʾ����N�������������ռ��bytes
loop:	Arry_byte_length = 
			Buf[Script_Tag_pos]   << 8  |
			Buf[Script_Tag_pos+1];
		Script_Tag_pos +=2;

		memcpy(Arry_Name,Buf + Script_Tag_pos , Arry_byte_length);  //������������
		Script_Tag_pos += Arry_byte_length;

		Arry_InFomation = Buf[Script_Tag_pos];                      //������ȥ��1bytes��ʾ��������������Ϣ
		Script_Tag_pos ++;

		/* //Arry_InFomation��ֵ˵��
		If Type == 0
		DOUBLE    //����8bytes��ʾ�����Զ�Ӧ��floatֵ
		If Type == 1
		UI8       //����1bytes��ʾboolean�������Ƿ�����ƵΪ01��ʾ�����С�����˼
		If Type == 2
		SCRIPTDATASTRING  //�����2bytes��ʾ�ַ������ȣ�Ȼ���ٸ���������ȴӺ�ߵ�bytes�ж�ȡ���ַ���
		If Type == 3
		SCRIPTDATAOBJECT[n]  //������Ϣ���������� Ȼ��ѭ������//ǰ��2bytes��ʾ����N�������������ռ��bytes�����Ĳ���
		If Type == 4
		SCRIPTDATASTRING     //��ʱ��������
		defining
		the MovieClip path
		If Type == 7
		UI16                 //��������2bytes������Ҫ������
		If Type == 8
		SCRIPTDATAVARIABLE[ECMAArrayLength]  //��ʱ��������
		If Type == 10
		SCRIPTDATAVARIABLE[n]   //���ݱ�����ռ4bytes
		If Type == 11
		SCRIPTDATADATE          //ռ10bytes����ʾ����
		If Type == 12
		SCRIPTDATALONGSTRING    //��ʱ��������
		*/

		if (strstr((char *)Arry_Name,"duration") != NULL)           
		{
			//Arry_InFomation == 0
			tag->duration= char2double(&Buf[Script_Tag_pos],8);
			Script_Tag_pos += 8;
		}
		else if (strstr((char *)Arry_Name,"width") != NULL)
		{
			//Arry_InFomation == 0;
			tag->width= char2double(&Buf[Script_Tag_pos],8);
			Script_Tag_pos += 8;
		}
		else if (strstr((char *)Arry_Name,"height") != NULL)
		{
			//Arry_InFomation == 0;
			tag->height = char2double(&Buf[Script_Tag_pos],8);
			Script_Tag_pos += 8;
		}
		else if (strstr((char *)Arry_Name,"videodatarate") != NULL)
		{
			//Arry_InFomation == 0;
			tag->videodatarate = char2double(&Buf[Script_Tag_pos],8);
			Script_Tag_pos += 8;
		}
		else if (strstr((char *)Arry_Name,"framerate") != NULL)
		{
			//Arry_InFomation == 0;
			tag->framerate = char2double(&Buf[Script_Tag_pos],8);	
			Script_Tag_pos += 8;
		}
		else if (strstr((char *)Arry_Name,"videocodecid") != NULL)
		{
			//Arry_InFomation == 0;
			tag->videocodecid = char2double(&Buf[Script_Tag_pos],8);
			Script_Tag_pos += 8;
		}
		else if (strstr((char *)Arry_Name,"audiosamplerate") != NULL)
		{
			//Arry_InFomation == 0;
			tag->audiosamplerate = char2double(&Buf[Script_Tag_pos],8);
			Script_Tag_pos += 8;
		}
		else if (strstr((char *)Arry_Name,"audiodatarate") != NULL)
		{
			//Arry_InFomation == 0;
			tag->audiodatarate = char2double(&Buf[Script_Tag_pos],8);
			Script_Tag_pos += 8;
		}
		else if (strstr((char *)Arry_Name,"audiosamplesize") != NULL)
		{
			//Arry_InFomation == 0;
			tag->audiosamplesize = char2double(&Buf[Script_Tag_pos ],8);
			Script_Tag_pos += 8;
		}
		else if (strstr((char *)Arry_Name,"stereo") != NULL)
		{
			//Arry_InFomation == 1;
			tag->stereo = Buf[Script_Tag_pos];
			Script_Tag_pos ++;
		}
		else if (strstr((char *)Arry_Name,"audiocodecid") != NULL)
		{
			//Arry_InFomation == 0;
			tag->audiocodecid = char2double(&Buf[Script_Tag_pos],8);
			Script_Tag_pos += 8;
		}
		else if (strstr((char *)Arry_Name,"filesize") != NULL)
		{
			//Arry_InFomation == 0;
			tag->filesize = char2double(&Buf[Script_Tag_pos],8);
			Script_Tag_pos += 8;
		}
		else if (strstr((char *)Arry_Name,"lasttime") != NULL)
		{
			//Arry_InFomation == 0;
			tag->lasttimetamp = char2double(&Buf[Script_Tag_pos],8);
			Script_Tag_pos += 8;
		}
		else if (strstr((char *)Arry_Name,"lastkeyframetime") != NULL)
		{
			//Arry_InFomation == 0;
			tag->lastkeyframetimetamp = char2double(&Buf[Script_Tag_pos],8);
			Script_Tag_pos += 8;
		}
		else if ((strstr((char *)Arry_Name,"keyframe") != NULL) && Arry_InFomation == 0x03)   //����ǹؼ�֡��Ϣ
		{
			//Arry_InFomation == 0x03; 
			goto loop;
		}
		else if ((strstr((char *)Arry_Name,"filepositions") != NULL)&& Arry_InFomation == 0x0A)
		{
			//Arry_InFomation == 0x0A;  ����������ڣ�keyframe�е�
			//���鳤�� 4bytes  
			Arry_Name_framekey_Arry_length = 
				Buf[Script_Tag_pos]      << 24 |
				Buf[Script_Tag_pos + 1]  << 16 |
				Buf[Script_Tag_pos + 2]  << 8  |
				Buf[Script_Tag_pos + 3];
			Script_Tag_pos += 4;
			//��ֵ��������
			for (int k = 0 ; k < Arry_Name_framekey_Arry_length ; k ++ )
			{
				Arry_InFomation_framekey =    Buf[Script_Tag_pos];               //����
				//Arry_InFomation_framekey == 0x00;
				Script_Tag_pos ++;
				tag->filepositions[i]= char2double(&Buf[Script_Tag_pos],8);      //ֵ
				Script_Tag_pos += 8;
			}
			//ע����������� ECMAArrayLength�����һ��
			i --;
		}
		else if ((strstr((char *)Arry_Name,"times") != NULL) && Arry_InFomation == 0x0A)
		{
			//Arry_InFomation == 0x0A;  ����������ڣ�keyframe�е�
			//���鳤�� 4bytes  
			Arry_Name_framekey_Arry_length = 
				Buf[Script_Tag_pos]      << 24 |
				Buf[Script_Tag_pos + 1]  << 16 |
				Buf[Script_Tag_pos + 2]  << 8  |
				Buf[Script_Tag_pos + 3];
			Script_Tag_pos += 4;
			//��ֵ��������
			for (int k = 0 ; k < Arry_Name_framekey_Arry_length ; k ++ )
			{
				Arry_InFomation_framekey = Buf[Script_Tag_pos];          //����
				//Arry_InFomation_framekey == 0x00;
				Script_Tag_pos ++;
				tag->times[i]= char2double(&Buf[Script_Tag_pos],8);      //ֵ
				Script_Tag_pos += 8;
			}
			//ע����������� ECMAArrayLength�����һ��
			i --;
		}
		else
		{
			//��ʱ������ȡ������Ҫ��bufָ������ƶ�
			switch (Arry_InFomation)
			{
			case 0x00:
				Script_Tag_pos += 8;
				break;
			case 0x01:
				Script_Tag_pos ++;
				break;
			case 0x02:
				Script_Tag_pos += 
					Buf[Script_Tag_pos]  << 8 |
					Buf[Script_Tag_pos+1];
				Script_Tag_pos +=2;
				break;
			case 0x03:
				goto loop;
				break;
			case 0x04:
				//��ʱ�������� һ�㲻������
				break;
			case 0x07:
				Script_Tag_pos += 2;
				break;
			case 0x08:
				//��ʱ�������� һ�㲻������
				break;
			case 0x0A:
				Script_Tag_pos += 4;
				break;
			case 0x0B:
				Script_Tag_pos += 10;
				break;
			case 0x0C:
				//��ʱ�������� һ�㲻������
				break;
			default:
                //�п����м���� 00 00 00 09 ���������������¶�ȡ
				printf("Arry_InFomation �������¶�ȡ\n");
				break;
			}
		}
	}
	//���data��������� ��ʲô��δ֪��
	memcpy(tag->Data,Buf + Script_Tag_pos,length - Script_Tag_pos );
	return 1;
}