#include <stdio.h>
#include"SDL2/SDL.h"
#define BLOCK_SIZE 1024000  //1M
static Uint8 buffer[BLOCK_SIZE];    //能否不用静态全局?
static int buf_len = 0;
static Uint8 *buf_pos = NULL;

void Read_Audio_Data(void *udata,Uint8 *stream,int len);
/*见源码SDL_AudioCallback定义,udata是用户传入的参数，stream是SDL内置播放音频的缓冲区，len是内置缓冲区大小*/

int main()
{
    if(SDL_Init(SDL_INIT_AUDIO))
    {
        SDL_Log("failed to initial!");
        exit(EXIT_FAILURE);
    }

    FILE*fp = NULL;
    char *path = "test1.pcm";
    fp = fopen(path,"r");
    if(!fp)
    {
        SDL_Log("failed to open the file!");
        SDL_Quit();
        exit(EXIT_FAILURE);
    }

    SDL_AudioSpec spec; //SDL_AudioSpec是包含音频输出格式的结构体,同时它也包含当音频设备需要更多数据时调用的回调函数
    spec.freq = 44100;
    spec.channels = 2;
    spec.format = AUDIO_S16SYS;
    spec.callback = Read_Audio_Data;    //回调函数,由声卡来调用该函数
    spec.userdata = NULL;               //传入回调函数的参数
    /*
    AUDIO_U16SYS：Unsigned 16-bit samples
    AUDIO_S16SYS：Signed 16-bit samples
    AUDIO_S32SYS：32-bit integer samples
    AUDIO_F32SYS：32-bit floating point samples
    */

    //打开音频设备
    if(SDL_OpenAudio(&spec,NULL))
    {
        SDL_Log("failed to open the device!");
        fclose(fp);
        SDL_Quit();
        exit(EXIT_FAILURE);
    }

    //播放音频
    SDL_PauseAudio(0);  //0为播放，1是暂停
    do      //声卡内部也有一个缓冲区，这其实是缓冲区到缓冲区的操作
    {
        buf_len = fread(buffer,1,BLOCK_SIZE,fp);
        SDL_Log("read buffer\n");
        buf_pos = buffer;
        while(buf_pos < buffer + buf_len)   //等待回调函数将buffer数据消耗完
            SDL_Delay(1);
    }while(buf_len != 0);

    SDL_CloseAudio();   //可能会在声卡在播放完缓冲区内的数据前关闭,导致末尾音频没有播放
    fclose(fp);
    SDL_Quit();
    return 0;
}
void Read_Audio_Data(void *udata,Uint8 *stream,int len) //len是由声卡给出的，表示此时声卡可接收的数据大小（缓冲区大小）
{
    if(buf_len == 0)
        return;
    SDL_memset(stream,0,len);   //必须先将stream中数据置为0
    len = (len < buf_len)? len : buf_len;   //根据声卡给出的缓冲区大小，如果声卡索求的大小超过外部缓冲区大小，那么最多只能给buf_len大小数据
    SDL_Log("len = %d\n",len);
    SDL_MixAudio(stream,buf_pos,len,SDL_MIX_MAXVOLUME); //将外部缓冲区数据拷贝到stream指向的内置缓冲区地址

    buf_pos += len;
    //buf_len -= len;     //更新数据长度？会影响主线程的buf_pos < buffer + buf_len，导致音频无法完整播放，注释掉可行
}
