/*
@this is an example of using speex library to denoise
*/

#define NN 882
 
int main()
{
    short in[NN];
    int i;
    SpeexPreprocessState *st;
    int count=0;
    float f;
 
    FILE *fp_src_wav, *fp_dst_wav;
    RIFF_HEADER riff;
    FMT_BLOCK fmt;
    FACT_BLOCK fact;
    DATA_BLOCK data;
    int samples, len_to_read, actual_len;//
    BYTE *src_all, *dst_all;
    //BYTE *src_left, *src_right;
    int frame_num;
 
    //�����ļ�·����
    char src_wav_name[]=".\\input_output\\sc2_one_ch.wav";
    char dst_wav_name[]=".\\input_output\\output.wav";
 
    st = speex_preprocess_state_init(NN, 44100);
    i=1;
    speex_preprocess_ctl(st, SPEEX_PREPROCESS_SET_DENOISE, &i);
 
    fp_src_wav=fopen(src_wav_name, "rb");
    if( !fp_src_wav )
    {
        printf("failed to open src wav file, exit...\n");
        exit(1);
    }
 
    fp_dst_wav=fopen(dst_wav_name, "wb");
    if(!fp_dst_wav)
    {
        printf("failed to open dst wav file, exit...\n");
        exit(1);
    }
 
    read_file_header(&fmt, &riff, &fact, &data, fp_src_wav);
    write_file_header(&fmt, &riff, &fact, &data, fp_dst_wav);
 
    if(fmt.wavFormat.wBitsPerSample==8)
    {
        printf("8 bits sample can't be dealed! exit...\n");
        fclose(fp_src_wav);
        fclose(fp_dst_wav);
        exit(1);
    }
 
    samples=NN;
    len_to_read=samples*fmt.wavFormat.wChannels*(fmt.wavFormat.wBitsPerSample/8);
    //�����ڴ�ռ�
    src_all=(BYTE*)malloc(sizeof(BYTE)*len_to_read);
    dst_all=(BYTE*)malloc(sizeof(BYTE)*len_to_read);
 
    //���ζ�ȡwav���ݡ��������
    frame_num=-1;
    for(;;)
    {
        //���ļ���ȡ����
        actual_len=fread(src_all, 1, len_to_read, fp_src_wav);
        if(!actual_len)
        {
            //�����Ѿ���ȡ���
            break;
        }
        frame_num++;
        //�������
        if(fmt.wavFormat.wChannels==1)
        {
            speex_preprocess_run(st, (short*)src_all);
        }
        else
        {
        }
        fwrite(src_all, 1, actual_len, fp_dst_wav);
    }//end for(;;)
 
    //�ͷſռ�
    free(src_all);
    free(dst_all);
 
    //�ر��ļ�
    fclose(fp_src_wav);
    fclose(fp_dst_wav);
 
    speex_preprocess_state_destroy(st);
    return 0;
}

