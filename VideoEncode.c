//yuv格式编码输出h264格式，压缩了100倍

#include <stdio.h>
#include"libavdevice/avdevice.h"
#include"libavutil/avutil.h"
#include"libavformat/avformat.h"
#include"libavcodec/avcodec.h"
#include<string.h>
#define WIDTH 1280
#define HEIGHT 720
////ibx264 的输入必须是yuv420p,需要将yuyv422 转成yuv420p
static void open_device(AVFormatContext **pfmt_ctx);

static void open_encoder(int width,int height,AVCodecContext** enc_ctx);

static void open_file(FILE **pfout, const char* filename);

static void set_frame(AVFrame *frame,int width,int height);

static void yuyv422_to_yuv420p(unsigned char *out, const unsigned char *in, unsigned int width, unsigned int height);

static void encode(FILE *fout,AVPacket *new_packet,AVFrame *frame,AVCodecContext* enc_ctx);

int main()
{
    //打开视频采集设备
    avdevice_register_all();
    AVFormatContext * fmt_ctx = NULL;    //包含一切媒体相关的上下文结构，初始值为空，函数调用成功之后为其赋值
    open_device(&fmt_ctx);
    //打开编码器
    AVCodecContext* enc_ctx = NULL;
    open_encoder(WIDTH,HEIGHT,&enc_ctx);

    //打开文件
    FILE *fout;
    FILE *fout2;
    open_file(&fout,"out.h264");
    open_file(&fout2,"out.yuv");

    //创建packet存储摄像头采集的数据
    AVPacket packet;    //AVPacket *packet also ok but frequently new and free is slow
    av_init_packet(&packet);

    //创建原始数据包  frame存储未经编码的原始数据，new_packet存储已编码的数据
    AVFrame *frame = NULL;
    frame = av_frame_alloc();
    if(!frame)
    {
        printf("No memory for frame!\n");
        exit(EXIT_FAILURE);
    }
    set_frame(frame,WIDTH,HEIGHT);

    AVPacket *new_packet = NULL;
    new_packet = av_packet_alloc();
    if(!new_packet)
    {
        printf("No memory for new_packet!\n");
        exit(EXIT_FAILURE);
    }


    //将采集内容写入文件
    int count = 0;
    int base = 0;
    while(av_read_frame(fmt_ctx,&packet) == 0 && count < 100)   //将读取的数据存在packet中，YUV以packet（相对于planner）格式存在packet中
    {
        printf("size is %d(%p),count = %d\n",packet.size,packet.data,count);
        count ++;

        //将待编码数据(摄像头采集的数据)由yuyv422 转成yuv420p后存入frame中
        unsigned char *yuv420_buf = (unsigned char *)malloc(WIDTH*HEIGHT*1.5*sizeof(unsigned char));   //Y:WIDTH*HEIGHT U:WIDTH*HEIGHT*1/4 V:WIDTH*HEIGHT*1/4

        yuyv422_to_yuv420p(yuv420_buf, packet.data, WIDTH, HEIGHT);     //yuyv422->yuv420:yuyvyuyvyuyv...->yyyy...uu..vv..
        memcpy(frame->data[0], yuv420_buf, 921600);                     //Y存data[0]
        for(int i = 0; i < 921600 / 4; i++)
                {
                    frame->data[1][i] = yuv420_buf[921600 + i];                 //U存data[1]
                    frame->data[2][i] = yuv420_buf[921600 + 921600 / 4 + i];    //V存data[2]
                }
        //YUV分量必须分别存在data[0]、[1]、[2]中，不然编码会发生错误

        fwrite(yuv420_buf,921600*1.5,1,fout2);     //输出未编码yuv文件用于对比 :ffplay -s 1280x720 out.yuv
        //fwrite(packet.data,packet.size,1,fout2);     //ffplay -s 1280x7200 -pix_fmt yuyv422 out1.yuv

        frame->pts = base++;                    // pts默认是一个随机值，需要给pts赋连续的值给编码器编码使用，否则视频质量会很差
        encode(fout,new_packet,frame,enc_ctx);  //编码并输出到out.h264 :ffplay out.h264


        fflush(fout);                    //fflush会将缓冲区内的数据写回参数stream 指定的文件中
        av_packet_unref(&packet);
    }
    encode(fout,new_packet,NULL,enc_ctx);
    //编码最后要给编码器传一个NULL，告诉编码器没有新数据需要编码，编码器会将缓冲区内的数据全部编码后结束。否则会有编码数据不全,视频丢帧的问题。

    fclose(fout);
    avformat_close_input(&fmt_ctx);   //关闭输入流并且释放fmt上下文
    return 0;
}


static void open_device(AVFormatContext **pfmt_ctx)
{

    char devicename[] = "/dev/video0";        //摄像头
    AVInputFormat *informat =  av_find_input_format("video4linux2");
    AVDictionary* option = NULL;

    av_dict_set(&option,"video_size","1280x720",0);   // 为打开视频设备设置参数
    av_dict_set(&option,"framerate","15",0);   //播放速度过快？ffplay播放设置-framerate 15

    int ret = avformat_open_input(pfmt_ctx,devicename,informat,&option);  //打开视频设备

    if(ret  < 0 )           //判断设备是否打开成功
    {
        printf("Failed to open audio device");
        exit(EXIT_FAILURE);
    }
}

static void open_encoder(int width,int height,AVCodecContext** enc_ctx)
{
    AVCodec *codec = NULL;
    codec = avcodec_find_encoder_by_name("libx264");  //libx264
    //codec = avcodec_find_encoder(AV_CODEC_ID_H264);
    if(!codec)
    {
        printf("Codec libx264 not found\n");
        exit(EXIT_FAILURE);
    }

    *enc_ctx = avcodec_alloc_context3(codec);
    if(!(*enc_ctx))
    {
        printf("codec context allocate failed\n");
        exit(EXIT_FAILURE);
    }

    (*enc_ctx)->profile = FF_PROFILE_H264_HIGH;    //SPS主要参数
    (*enc_ctx)->level = 50;     //mean 5.0

    //设置分辨率
    (*enc_ctx)->width = width;
    (*enc_ctx)->height = height;

    //设置gop长度,GOP越长，当I帧丢失时等候时间越长,此处设置成帧率大小
    (*enc_ctx)->gop_size = 250;
    (*enc_ctx)->keyint_min = 15;        //即每15帧一个I帧,可选设置

    // 设置B帧
    (*enc_ctx)->max_b_frames = 3;       //可选设置
    (*enc_ctx)->has_b_frames = 1;       //可选设置

    // 参考帧的数量
    (*enc_ctx)->refs = 3;               //可选设置 参考帧越大，处理得越慢，还原度越高

    // 设置输入YUV格式
    (*enc_ctx)->pix_fmt = AV_PIX_FMT_YUV420P;    //ibx264 的输入必须是yuv420p

    // 设置码率
    (*enc_ctx)->bit_rate = 1600000;  //1600kbps = (1280*720*15*12)/100  YUV码流=分辨率*12bit*帧率,通过h264压缩100倍左右

    // 设置帧率
    (*enc_ctx)->time_base = (AVRational){1, 15}; // 帧与帧之间的间隔
    (*enc_ctx)->framerate = (AVRational){15, 1}; // 帧率，每秒15帧

    int ret = 0;
    //打开编码器
    ret = avcodec_open2((*enc_ctx), codec, NULL);

    if (ret < 0)
    {
        printf("Can not open codec: %s",av_err2str(ret));
        exit(EXIT_FAILURE);
    }

}

static void open_file(FILE **pfout,const char* filename)
{
    const char *out = filename;                     //char out[] = "out.yuv"    输出文件存放在Debug文件夹下
    if(!(*pfout = fopen(out,"wb")))
    {
        printf("can't open the file\n");
        exit(EXIT_FAILURE);
    }

}

static void set_frame(AVFrame *frame,int width,int height)
{
    frame->width = width;
    frame->height = height;
    frame->format = AV_PIX_FMT_YUV420P;
    int ret = av_frame_get_buffer(frame,32);   //创建缓冲区,视频必须按32位对齐
    if(ret < 0)
    {
        printf("failed to alloc buffer for frame!\n");
        exit(EXIT_FAILURE);
    }
}

static void yuyv422_to_yuv420p(unsigned char *out, const unsigned char *in, unsigned int width, unsigned int height)
{
    unsigned char *y = out;
    unsigned char *u = out + width*height;
    unsigned char *v = out + width*height + width*height/4;

    unsigned int i,j;
    unsigned int base_h;
    unsigned int is_y = 1, is_u = 1;
    unsigned int y_index = 0, u_index = 0, v_index = 0;

    unsigned long yuv422_length = 2 * width * height;

    //序列为YU YV YU YV，一个yuv422帧的长度 width * height * 2 个字节
    //丢弃偶数行 u v
    for(i=0; i<yuv422_length; i+=2)
    {
        *(y+y_index) = *(in+i);
        y_index++;
    }
    for(i=0; i<height; i+=2)
    {
        base_h = i*width*2;
        for(j=base_h+1; j<base_h+width*2; j+=2)
        {
            if(is_u)
            {
            *(u+u_index) = *(in+j);
            u_index++;
            is_u = 0;
            }
            else
            {
                *(v+v_index) = *(in+j);
                v_index++;
                is_u = 1;
            }
        }
    }
}

static void encode(FILE *fout,AVPacket *new_packet,AVFrame *frame,AVCodecContext* enc_ctx)
{
    if(frame)
        printf("send frame to encoder, pts = %lld\n",frame->pts);
    int ret = 0;
    //将原始数据传给编码器进行编码
    ret = avcodec_send_frame(enc_ctx,frame);
    if(ret < 0)
    {
        printf("failed to send frame for encoding!\n");
        exit(EXIT_FAILURE);
    }

    //从编码器获取编码后的数据，并写入文件
    while(ret >= 0)
    {
        ret = avcodec_receive_packet(enc_ctx,new_packet);    //每次receive_packet时都会加引用计数
        if(ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)     //如果编码器数据不足时会返回EAGAIN，或者到数据结尾时会返回AVERROR_EOF
        {
            return;
        }
        else if(ret < 0)
        {
            printf("failed to encode!\n");
            exit(EXIT_FAILURE);
        }
        fwrite(new_packet->data,new_packet->size,1,fout);     //packet.size应该等于YUV420即Y（分辨率）*1.5,否则，应该把packet.size参数改为该值
        av_packet_unref(new_packet);                    //减少引用计数
    }
}
