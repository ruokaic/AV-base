#include <stdio.h>
#include"libavutil/avutil.h"
#include"libavdevice/avdevice.h"
#include"libavformat/avformat.h"
#include"libavcodec/codec.h"
int main()
{
    avdevice_register_all();

    AVFormatContext * fmt_ctx = NULL;    //包含一切媒体相关的上下文结构，初始值为空，函数调用成功之后为其赋值
    char devicename[] = "/dev/video0";        //摄像头
    AVInputFormat *informat =  av_find_input_format("video4linux2");
    AVDictionary* option = NULL;

    av_dict_set(&option,"video_size","1280x720",0);   // 为打开视频设备设置参数
    av_dict_set(&option,"framerate","15",0);   //播放速度过快？ffplay播放设置-framerate 15

    int ret = avformat_open_input(&fmt_ctx,devicename,informat,&option);  //打开视频设备

    if(ret < 0)           //判断设备是否打开成功
    {
        fprintf(stderr,"Failed to open audio device");
        exit(EXIT_FAILURE);
    }

    AVPacket packet;    //AVPacket *packet also ok but frequently new and free is slow
    av_init_packet(&packet);


    FILE *fout;
    const char *out = "out.yuv";                     //char out[] = "out.yuv"    输出文件存放在Debug文件夹下
    if((fout = fopen(out,"wb")) == NULL)
    {
        fprintf(stderr,"can't open the file\n");
        exit(EXIT_FAILURE);
    }
    int count = 0;

    while(av_read_frame(fmt_ctx,&packet) == 0 && count < 500)   //将读取的数据存在packet中
    {
        av_log(NULL,AV_LOG_INFO,"size is %d(%p),count = %d\n",packet.size,packet.data,count);
        count ++;

        fwrite(packet.data,packet.size,1,fout);        //packet.size应该等于YUV422即Y（分辨率）*2,否则，应该把packet.size参数改为该值
        fflush(fout);                    //fflush会将缓冲区内的数据写回参数stream 指定的文件中
        av_packet_unref(&packet);
    }

    fclose(fout);
    avformat_close_input(&fmt_ctx);   //关闭输入流并且释放fmt上下文
    return 0;
}
