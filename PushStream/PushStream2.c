/* H264推流主要流程
 * 1、设置输入输出，生成对应输入输出的AVFormatContext
 * 2、根据输入流AVStream来构造输出流（AVFormatContext中的AVStream）
 * 3、将输入的AVCodecContext的视频参数拷贝到输出的AVCodecContext中
 * 4、用avio_open打开输出
 * 5、写入（推流）：avformat_write_header写文件头，av_interleaved_write_frame写包，av_write_trailer写文件尾
 * 注意：需为H264裸流添加时间戳等参数并转换其时间基准，需设置推流延迟
 */
#include <stdio.h>
#include"libavformat/avformat.h"
#include"libavutil/mathematics.h"   //for timestamp and time base
#include"libavutil/time.h"          //Get the current time
#include"libavcodec/avcodec.h"
int main()
{
    AVFormatContext *ifmt_ctx = NULL;
    AVFormatContext *ofmt_ctx = NULL;

    AVOutputFormat *ofmt = NULL;

    AVPacket pkt;
    const char *in_filename,*out_filename;

    int ret;
    int video_index = -1;
    int frame_index = 0;
    int64_t start_time = 0;

    in_filename = "test.h264";
    //in_filename = "test.flv";
    out_filename = "rtmp://localhost/live/room";

    avformat_network_init();

    //输入
    if((ret = avformat_open_input(&ifmt_ctx,in_filename,0,0)) < 0)
    {
        fprintf(stderr,"Failed to open input file.\n");
        exit(EXIT_FAILURE);
    }
    if((ret = avformat_find_stream_info(ifmt_ctx,0)) < 0)
    {
        fprintf(stderr,"Failed to find input stream information.\n");
        exit(EXIT_FAILURE);
    }
    for(int i = 0; i<ifmt_ctx->nb_streams; i++)
    {
        if(ifmt_ctx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO)
        {
            video_index = i;
            break;
        }
    }
    if(video_index < 0)
    {
        fprintf(stderr, "%s: Failed to find a video stream.\n", in_filename);
        exit(EXIT_FAILURE);
    }
    av_dump_format(ifmt_ctx,0,in_filename,0);

    //输出
    avformat_alloc_output_context2(&ofmt_ctx,NULL,"flv",out_filename);
    if(!ofmt_ctx)
    {
        fprintf(stderr, "Failed to create output context.\n");
        exit(EXIT_FAILURE);
    }

    ofmt = ofmt_ctx->oformat;   //The output container format.

    //根据输入流构造输出流
    for(int i = 0; i < ifmt_ctx->nb_streams; i++)
    {
        AVStream *in_stream = ifmt_ctx->streams[i];
        AVStream *out_stream = avformat_new_stream(ofmt_ctx,in_stream->codec->codec);
        if(!out_stream)
        {
            fprintf(stderr, "Failed to allow output stream.\n");
            exit(EXIT_FAILURE);
        }

       //复制AVCodecContext的设置，即复制视频参数
        ret = avcodec_copy_context(out_stream->codec,in_stream->codec);
        //ret = avcodec_parameters_from_context(out_stream->codecpar,enc_ctx);
        if(ret < 0)
        {
            fprintf(stderr, "Failed to copy context from input to output.\n");
            exit(EXIT_FAILURE);
        }

        /*
         * avformat_write_header写入封装容器的头信息时，会检查codec_tag：
         * 若AVStream->codecpar->codec_tag有值，则会校验AVStream->codecpar->codec_tag
         * 是否在封装格式支持的codec_tag列表中，若不在，就会打印错误信息；
         * 若AVStream->codecpar->codec_tag为0，则会根据AVCodecID从封装格式的codec_tag列表中，
         * 找一个匹配的codec_tag。
         */
        out_stream->codec->codec_tag = 0;
        if(ofmt->flags & AVFMT_GLOBALHEADER)
            out_stream->codec->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }

    av_dump_format(ofmt_ctx,0,out_filename,1);
    //打开输出
    if(!(ofmt->flags & AVFMT_NOFILE))
    {
        ret = avio_open(&ofmt_ctx->pb,out_filename,AVIO_FLAG_WRITE);
        if(ret < 0)
        {
            fprintf(stderr, "Could not open output URL '%s'.\n",out_filename);
            exit(EXIT_FAILURE);
        }
    }

    ret = avformat_write_header(ofmt_ctx,NULL);
    if(ret < 0)
    {
        fprintf(stderr, "Failed to write header\n");
        exit(EXIT_FAILURE);
    }
    start_time = av_gettime();
    while(1)
    {
        ret = av_read_frame(ifmt_ctx,&pkt);
        if(ret < 0)
            break;

        //处理没有时间戳的情况，如H264
        if(pkt.pts == AV_NOPTS_VALUE)
        {
            //write PTS
            AVRational time_base1 = ifmt_ctx->streams[video_index]->time_base;
            //duration between 2 frames(us)
            int64_t calc_duration = (double)AV_TIME_BASE/av_q2d(ifmt_ctx->streams[video_index]->r_frame_rate);
            //Parameters
                        pkt.pts=(double)(frame_index*calc_duration)/(double)(av_q2d(time_base1)*AV_TIME_BASE);
                        pkt.dts=pkt.pts;
                        pkt.duration=(double)calc_duration/(double)(av_q2d(time_base1)*AV_TIME_BASE);
        }
        //设置推流延时，避免服务器缓冲区数据溢出
        if(pkt.stream_index == video_index)
        {
            AVRational time_base=ifmt_ctx->streams[video_index]->time_base;
            AVRational time_base_q={1,AV_TIME_BASE};
            int64_t pts_time = av_rescale_q(pkt.dts, time_base, time_base_q);
            int64_t now_time = av_gettime() - start_time;
            if (pts_time > now_time)
                av_usleep(pts_time - now_time);
        }

        AVStream *in_stream2 ,*out_stream2;
        in_stream2 = ifmt_ctx->streams[pkt.stream_index];
        out_stream2 = ofmt_ctx->streams[pkt.stream_index];
        /* copy packet */
        //转换PTS/DTS（Convert PTS/DTS）
        pkt.pts = av_rescale_q_rnd(pkt.pts, in_stream2->time_base,
                                   out_stream2->time_base,
                                   (enum AVRounding)(AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX));
        pkt.dts = av_rescale_q_rnd(pkt.dts, in_stream2->time_base,
                                   out_stream2->time_base,
                                   (enum AVRounding)(AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX));
        pkt.duration = av_rescale_q(pkt.duration, in_stream2->time_base, out_stream2->time_base);
        pkt.pos = -1;

        if(pkt.stream_index == video_index)
        {
            fprintf(stderr,"send %8d video frames to output URL\n",frame_index);
            frame_index++;
        }

        ret = av_interleaved_write_frame(ofmt_ctx,&pkt);    //写入一个AVPacket到输出文件

        if(ret < 0)
        {
            fprintf(stderr, "Error muxing packet\n");
            break;
        }

        av_packet_unref(&pkt);
    }
    //写文件尾
    av_write_trailer(ofmt_ctx);

    avformat_close_input(&ifmt_ctx);
    if(ofmt_ctx && !(ofmt->flags & AVFMT_NOFILE))
        avio_close(ofmt_ctx->pb);
    avformat_free_context(ofmt_ctx);
    if(ret < 0 && ret != AVERROR_EOF)
    {
        fprintf(stderr, "Error occurred.\n");
        exit(EXIT_FAILURE);
    }
    return 0;
}
