#include <stdio.h>
#include"libavcodec/codec.h"
#include"libavdevice/avdevice.h"
#include"libavutil/avutil.h"
#include"libavformat/avformat.h"

#include<string.h>   //for memcpy
//音频编码流程：先判断是否需要重采样 如fdkaac只支持16位输入，若设备输入为32位则需在编码前重采样
//创建编码器->创建编码器上下文->设置编码输出参数->打开编码器->初始化frame和packet->将音频数据存入frame->送frame数据给编码器->将编码后数据存入packet中->从packet取出数据进行其他操作->释放内存
int main()
{

    //通过录音设备接收音频数据,并使用编码器编码后输出
    //打开音频设备
    avdevice_register_all();

    AVFormatContext * fmt_ctx = NULL;    //包含一切媒体相关的上下文结构，初始值为空，函数调用成功之后为其赋值
    char devicename[] = "hw:0,0";
    AVInputFormat *informat = av_find_input_format("alsa");
    AVDictionary* option = NULL;


    int ret = avformat_open_input(&fmt_ctx,devicename,informat,&option);      //打开视频设备

    if(ret < 0)
    {
        av_log(NULL,AV_LOG_ERROR,"can not open device\n");
        //exit(EXIT_FAILURE);
    }

    //创建编码器AVCodec *codec = avcodec_find_encoder(AV_CODEC_ID_AAC);
    AVCodec *codec = avcodec_find_encoder_by_name("libfdk_aac");        //不知为何打不开，一直返回NULL,重装了所有库后解决

    //创建编码上下文并设置编码输出参数(格式)
    AVCodecContext *codec_ctx = avcodec_alloc_context3(codec);
    codec_ctx->sample_fmt = AV_SAMPLE_FMT_S16;
    codec_ctx->sample_rate = 44100;
    codec_ctx->channel_layout = AV_CH_LAYOUT_STEREO;
    //codec_ctx->bit_rate = 0;
    //codec_ctx->profile = FF_PROFILE_AAC_HE_V2;

    //打开编码器
    if(avcodec_open2(codec_ctx,codec,NULL) < 0)
    {
        fprintf(stderr,"failed to open AVCodec\n");
        exit(EXIT_FAILURE);
    }

    //初始化原始数据包,存储未经编码的原始数据
    AVFrame *frame = av_frame_alloc();
    if(!frame)
    {
        fprintf(stderr,"frame malloc failed\n");
        exit(EXIT_FAILURE);                                     //内存分配失败，退出程序
    }

    frame->nb_samples = 512;
    frame->format = AV_SAMPLE_FMT_S16;
    frame->channel_layout = AV_CH_LAYOUT_STEREO;                //frame->channels = 2;

    av_frame_get_buffer(frame,0);

    if(!frame->buf[0])
    {
        fprintf(stderr,"buf[0] malloc failed\n");
        exit(EXIT_FAILURE);                                     //内存分配失败，退出程序
    }
    //初始化packet数据包,储存编码后数据
    AVPacket *new_packet = av_packet_alloc();
    if(!frame)
    {
        fprintf(stderr,"packet malloc failed\n");
        exit(EXIT_FAILURE);                                     //内存分配失败，退出程序
    }


    //从音频设备读取音频数据并使用编码器编码后输出
    AVPacket packet;
    av_init_packet(&packet);
    int count = 0;

    FILE *fout;
    const char *out = "out.aac";  //char out[] = "out.aac"    save in XXX.Debug!!!!!!!!!
    if((fout = fopen(out,"wb+")) == NULL)
    {
        fprintf(stderr,"can't open the file\n");
        exit(EXIT_FAILURE);
    }

    while(av_read_frame(fmt_ctx,&packet) == 0 && count < 500)      //从音频设备读取音频数据
    {
        av_log(NULL,AV_LOG_INFO,"size is %d(%p),count = %d\n",packet.size,packet.data,count);
        count ++;

        //fdk编码器只支持16位pcm输入由于本机设备录音采样大小为16k，故不需要重采样，可使用fdkaac编码器直接编码
        //将待编码数据传入frame中
        memcpy((void*)frame->data[0],(void*)packet.data,packet.size);

        //开始编码
        int ret = avcodec_send_frame(codec_ctx,frame);     //送frame数据给编码器
        while(ret >= 0)
        {
            ret = avcodec_receive_packet(codec_ctx,new_packet);//将编码内容存入packet中
            if(ret < 0)
            {
                if(ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
                    break;
                fprintf(stderr,"encoding filed\n");
                exit(EXIT_FAILURE);
            }
            fwrite(new_packet->data,new_packet->size,1,fout);
            fflush(fout);                    //fflush会强迫将缓冲区内的数据写回参数stream 指定的文件中
        }
        av_packet_unref(&packet);
    }

    fclose(fout);
    avformat_close_input(&fmt_ctx);   //关闭输入流并且释放fmt上下文

    return 0;
}
