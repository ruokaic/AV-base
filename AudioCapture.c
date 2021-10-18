#include<stdio.h>

#include"libavutil/avutil.h"
#include"libavdevice/avdevice.h"
#include"libavformat/avformat.h"
#include"libavcodec/codec.h"
#include"libswresample/swresample.h"   // 重采样

#include<string.h>   //for memcpy
//录音流程：创建上下文->设置参数->打开设备->初始化packet包->从设备读取录音数据到packet包->把packet包数据写入pcm文件->释放内存
//重采样流程：创建上下文->设置输入输出参数->设置输入输出缓冲区->数据拷贝到输入缓冲区->重采样到输出缓冲区->从输出缓冲区提取数据进行其他操作（写入文件或编码）->释放内存
int main()
{
    avdevice_register_all();

    AVFormatContext * fmt_ctx = NULL;    //包含一切媒体相关的上下文结构，初始值为空，函数调用成功之后为其赋值
    char devicename[] = "hw:0,0";
    AVInputFormat *informat = av_find_input_format("alsa");
    AVDictionary* option = NULL;


    int ret = avformat_open_input(&fmt_ctx,devicename,informat,&option);


    char errors[1024];
    if(ret < 0)           //判断设备是否打开成功
    {
        av_strerror(ret,errors,1024);
        qDebug() << "Failed to open audio device, [" << ret << "]" << errors;
    }

    ///////////////////////////////////////////////////
        SwrContext *swr_ctx = NULL;                      //创建重采样上下文
        swr_ctx = swr_alloc_set_opts(NULL,
                                     AV_CH_LAYOUT_STEREO,AV_SAMPLE_FMT_S32,44100,       //output
                                     AV_CH_LAYOUT_STEREO,AV_SAMPLE_FMT_S16,44100,       //input
                                     0,NULL);
        if(NULL == swr_ctx)
        {
            av_log(NULL,AV_LOG_INFO,"option setting failed");
        }
        if(swr_init(swr_ctx) < 0)
        {
            av_log(NULL,AV_LOG_INFO,"initial failed");
        }

        u_int8_t **buff_out = NULL;
        int buff_out_size = 0;
        u_int8_t **buff_in = NULL;
        int buff_in_size = 0;
        //构造缓冲区
        av_samples_alloc_array_and_samples(&buff_out,&buff_out_size,2,512,AV_SAMPLE_FMT_S32,0);
        av_samples_alloc_array_and_samples(&buff_in,&buff_in_size,2,512,AV_SAMPLE_FMT_S16,0);
    ////////////////////////////////////////////////////////////
av_samples_get_buffer_size()
    AVPacket packet;    //AVPacket *packet also ok but frequently new and free is slow
    av_init_packet(&packet);
    int count = 0;

    FILE *fout;
    const char *out = "out1.pcm";  //char out[] = "out.pcm"    save in XXX.Debug!!!!!!!!!
    if((fout = fopen(out,"wb")) == NULL)
    {
        fprintf(stdout,"can't open the file\n");
        exit(EXIT_FAILURE);
    }

    while(av_read_frame(fmt_ctx,&packet) == 0 && count < 500)
    {
        av_log(NULL,AV_LOG_INFO,"size is %d(%p),count = %d\n",packet.size,packet.data,count);
        count ++;

        ///////////////////////////////////////////////////
        memcpy((void*)buff_in[0],(void*)packet.data,packet.size);

        swr_convert(swr_ctx,buff_out,512,(const u_int8_t **)buff_in,512);
        ///////////////////////////////////////////////////

        //fwrite(packet.data,packet.size,1,fout);
        fwrite(buff_out[0],1,buff_out_size,fout);
        fflush(fout);                    //fflush会强迫将缓冲区内的数据写回参数stream 指定的文件中
        av_packet_unref(&packet);
    }

 ///////////////////////////

    if(buff_out)          //释放重采样缓冲区
    {
        av_freep(buff_out[0]);
    }
    av_freep(buff_out);
    if(buff_in)
    {
        av_freep(buff_in[0]);
    }
    av_freep(buff_in);
    swr_free(&swr_ctx);  //释放重采样上下文
////////////////////////////
    fclose(fout);
    avformat_close_input(&fmt_ctx);   //关闭输入流并且释放fmt上下文

    return 0;
}
