#include"librtmp/rtmp.h"
#include<stdio.h>
#include<unistd.h>    //for sleep(ms毫秒)/usleep(μs微秒)

//使用宏转换大小端
#define HTON16(x)  ((x>>8&0xff)|(x<<8&0xff00))
#define HTON24(x)  ((x>>16&0xff)|(x<<16&0xff0000)|(x&0xff00))
#define HTON32(x)  ((x>>24&0xff)|(x>>8&0xff00)|(x<<8&0xff0000)|(x<<24&0xff000000))
#define HTONTIME(x) ((x>>16&0xff)|(x<<16&0xff0000)|(x&0xff00)|(x&0xff000000))

enum bool{false,true};
void push_stream();
FILE* open_flv(const char* flv_name);
RTMP* connect_rtmp_server(char* rtmpaddr);
void send_data(RTMP*rtmp, FILE* fp);

RTMPPacket* initialize_packet();
int read_data(RTMPPacket* packet,FILE* fp);

int read_u8(char *u8,FILE* fp);
int read_u24(int *u24,FILE* fp);
int read_time_stamp(int *ts,FILE* fp);
int read_u32(int *u32,FILE* fp);

int main()
{
    push_stream();
    return 0;
}
void push_stream()
{
    char *flv = "test.flv";
    char *rtmpaddr = "rtmp://localhost/live/room";

    FILE*fp = NULL;
    fp = open_flv(flv);     //打开flv文件

    RTMP*rtmp = NULL;
    rtmp = connect_rtmp_server(rtmpaddr);   // 链接流媒体服务器

    send_data(rtmp,fp);     // 推流
}

FILE *open_flv(const char *flv_name)
{
    FILE*fp = NULL;
    fp = fopen(flv_name,"rb");
    if(!fp)
    {
        printf("Failed to open flv: %s", flv_name);
        return NULL;
    }
    fseek(fp, 9, SEEK_SET); //跳过 9 字节的 FLV Header
    fseek(fp, 4, SEEK_CUR); //跳过 4 字节的PreTagSize,只需要跳过前面第一个pretagsize
    //fseek(fp, 13, SEEK_SET)
    return fp;
}

RTMP *connect_rtmp_server(char *rtmpaddr)  //没释放资源
{
    RTMP *rtmp = NULL;

    //1. 创建RTMP对象,并进行初始化
    rtmp = RTMP_Alloc();
    if(!rtmp){
        printf("NO Memory, Failed to alloc RTMP object!\n");
        return NULL;
    }
    RTMP_Init(rtmp);

    //2.设置RTMP服务地址，以及设置连接超时间
    RTMP_SetupURL(rtmp, rtmpaddr);
    rtmp->Link.timeout = 10;

    //3. 设置是推流还是拉流
    //如果设置了该开关，就是推流(push)，如果未设置就是拉流（play)
    RTMP_EnableWrite(rtmp);

    //4. 建立连接,失败返回0
   if(!RTMP_Connect(rtmp,NULL))
   {
       printf("Failed to Connect RTMP Server!\n");
       if(rtmp)
       {
           RTMP_Close(rtmp);
           RTMP_Free(rtmp);
       }
       return NULL;
   }

    //5. 创建流
    RTMP_ConnectStream(rtmp,0);     //可以设置到某个点开始，默认0开始

    return rtmp;
}



void send_data(RTMP *rtmp, FILE *fp)
{
    //1. 创建RTMPPacket对象,并进行初始化
    RTMPPacket* packet = NULL;      //rtmppacket 是用于在rtmp传输的数据包，所有flv数据都需要先存为rtmppacket再通过rtmp传输
    packet = initialize_packet();   //分配空间,初始化
    packet->m_nInfoField2 = rtmp->m_stream_id;  //设置流ID，每个包都属于某一个流
    unsigned pre_ts = 0;

    while(1)
    {
        //2.从flv文件中读取数据,存为packet
        if(!read_data(packet,fp)){
            printf("over!\n");
            break;
        }

        //3. 传输数据之前，判断RTMP连接是否正常
        if(!RTMP_IsConnected(rtmp))
        {
            printf("Disconnect!\n");
            break;
        }

        //延时,每次隔一段时间发送,类似ffmpeg推流加-re的功能，防止数据一下子全部发送过去，接收端缓冲区溢出导致数据丢失
        unsigned int diff = packet->m_nTimeStamp - pre_ts;
        usleep(diff*1000);//usleep精度往往不够，真正推流时，应该使用多线程：接收端已有数据就发送，发送端每次隔一段时间发送

        //4. 发送数据
        RTMP_SendPacket(rtmp, packet, 0);

        pre_ts = packet->m_nTimeStamp;
    }
    //5.释放packet内存
//    RTMPPacket_Free(packet);

}

RTMPPacket* initialize_packet()
{
    RTMPPacket* packet = NULL;
    packet = (RTMPPacket*)malloc(sizeof(RTMPPacket));
    if(!packet)
    {
        printf("No Memory, Failed to alloc RTMPPacket!\n");
        return NULL;
    }
    RTMPPacket_Alloc(packet,64*1024);    //packet实际上只是个外壳,还要分配真正存放数据的buffer空间
    RTMPPacket_Reset(packet);            //使用前，内部重置

    //设置参数
    packet->m_hasAbsTimestamp = 0;       //时间戳是绝对值还是相对值,0是相对时间戳
    packet->m_nChannel = 0x4;           //块流ID，Audio 和 Video通道

    return packet;
}

int read_data(RTMPPacket *packet, FILE *fp)
{
    /*
     * tag header
     * 第一个字节 Tag Type, 0x08 音频，0x09 视频， 0x12 script
     * 2-4, Tag body 的长度， PreTagSize - Tag Header size
     * 5-7, 时间戳，单位是毫秒; script 它的时间戳是0
     * 第8个字节，扩展时间戳。真正时间戳结格 [扩展，时间戳] 一共是4字节。
     * 9-11, streamID, 0
     */

    /*
     * flv
     * flv header(9), tagpresize, tag(header+data), tagpresize
     */
    char tag_type;
    int tag_data_size;
    int time_stamp;
    int streamID;
    int pre_tag_size;

    if(!read_u8(&tag_type,fp))
        return false;

    if(!read_u24(&tag_data_size,fp))
        return false;

    if(!read_time_stamp(&time_stamp,fp))
        return false;

    if(!read_u24(&streamID,fp))
        return false;

     printf("tag header, tag_type: %u, tag_data_size: %d, time_stamp:%d \n", tag_type, tag_data_size, time_stamp);

    //头部解析完，开始读取数据
    int size = fread(packet->m_body,1,tag_data_size,fp);
    if(size != tag_data_size)
    {
        printf("Failed to read tag body from flv, (datasize=%d:tds=%d)\n",size,tag_data_size);
        return false;
    }

    //设置rtmppacket属性:RTMP Message Header 与flv的Tag Header,一一对应
    packet->m_headerType = RTMP_PACKET_SIZE_LARGE;      //对应rtmp格式中的fmt（0~3）,取0时，11个字节全部包含  只有设置了fmt数值，才能知道message header（chunk header）的大小
    packet->m_packetType = tag_type;
    packet->m_nBodySize = tag_data_size;
    packet->m_nTimeStamp = time_stamp;

    //跳过下一个pre_tag_size
    if(!read_u32(&pre_tag_size,fp))
        return false;

    return true;
}

int read_u8(char *u8, FILE *fp)
{
   if(fread(u8,1,1,fp) != 1)         //从fp读取1个字节给u8，u8赋值给packetType（RTMP参数,大小1个字节）
   {
       printf("Failed to read_u8!\n");
       return false;
   }
   return true;
}

int read_u24(int *u24, FILE *fp)
{
    int tmp;
    if(fread(&tmp, 1, 3, fp) != 3)      //从fp读取3个字节给u24，u24赋值给某个参数
    {
        printf("Failed to read_u24!\n");
        return false;
    }
    //由于FLV是大端模式，超过一个字节时，需要将FLV读取赋值给u24的信息进行转换
    //转换为小端，才能被操作系统正确识别，不会造成数据混乱
    //转换：3个字节abc对应低中高位，转为小端，低位变高位，高位变低位：cba
    *u24 = ((tmp >> 16) & 0xFF) | ((tmp << 16) & 0xFF0000) | (tmp & 0xFF00);
    //tmp右移16位，剩下大端低位1个字节，和0xFF（16进制2个F对应二进制8个1）进行与运算，确保只取8个位作为小端低位（大端高位）
    //tmp左移16位，0xFF0000即左端8个1，右端16个0，同理将大端高位换成小端高位
    return true;
}

int read_time_stamp(int *ts, FILE *fp)
{
    int tmp;
    if(fread(&tmp, 1, 4, fp) !=4)
    {
         printf("Failed to read_ts!\n");
         return false;
    }
    *ts = ((tmp >> 16) & 0xFF) | ((tmp << 16) & 0xFF0000) | (tmp & 0xFF00) | (tmp & 0xFF000000);
    return true;
}

int read_u32(int *u32, FILE *fp)
{
    int tmp;
    if(fread(&tmp, 1, 4, fp) != 4)
    {
        printf("Failed to read_u32!\n");
        return false;
    }
    *u32 = ((tmp >> 24) & 0xFF) | ((tmp >> 8) & 0xFF00) | \
            ((tmp << 8) & 0xFF0000) | ((tmp << 24) & 0xFF000000);
    return true;
}