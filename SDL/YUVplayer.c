#include <stdio.h>
#include <string.h>

#include <SDL2/SDL.h>

#define BLOCK_SIZE 4096000  //一次从磁盘读取一大块数据，避免频繁访问磁盘

//USEREVENT(0x8000以后)是留给用户自己定义的
#define REFRESH_EVENT  (SDL_USEREVENT + 1)
#define QUIT_EVENT  (SDL_USEREVENT + 2)

int thread_exit=0;

int refresh_video_timer(void *udata)
{
    thread_exit=0;

    while (!thread_exit)
    {
        //共享全局变量thread_exit,只读取，不写，不需要加锁
        SDL_Event event;
        event.type = REFRESH_EVENT;
        SDL_PushEvent(&event);
        SDL_Delay(40);
    }
    //push quit event
    SDL_Event event;
    event.type = QUIT_EVENT;
    SDL_PushEvent(&event);
    return 0;
}

int main(void)
{

    FILE *video_fd = NULL;

    SDL_Event event;
    SDL_Rect rect;

    Uint32 pixformat = 0;

    SDL_Window *win = NULL;
    SDL_Renderer *renderer = NULL;
    SDL_Texture *texture = NULL;

    SDL_Thread *timer_thread = NULL;

    int w_width = 640, w_height = 480;
    const int video_width = 1280, video_height = 720;

    Uint8 *video_pos = NULL;
    Uint8 *video_end = NULL;

    unsigned int remain_len = 0;
    unsigned int video_buff_len = 0;
    unsigned int blank_space_len = 0;
    Uint8 video_buf[BLOCK_SIZE];        //uint8 = unsigned char

    const char *path = "test1.yuv";

    const unsigned int yuv_frame_len = video_width * video_height * 12 / 8;     //按整数对齐,12/8 = 1.5 应避免使用小数

    //initialize SDL
    if(SDL_Init(SDL_INIT_VIDEO))
    {
        fprintf( stderr, "Could not initialize SDL - %s\n", SDL_GetError());
        return -1;
    }

    //creat window from SDL
    win = SDL_CreateWindow("YUV Player",
                           SDL_WINDOWPOS_UNDEFINED,
                           SDL_WINDOWPOS_UNDEFINED,
                           w_width, w_height,
                           SDL_WINDOW_OPENGL|SDL_WINDOW_RESIZABLE);
    if(!win)
    {
        fprintf(stderr, "Failed to create window, %s\n",SDL_GetError());
        goto __FAIL;
    }

    renderer = SDL_CreateRenderer(win, -1, 0);

    //IYUV: Y + U + V  (3 planes)
    //YV12: Y + V + U  (3 planes)
    pixformat= SDL_PIXELFORMAT_IYUV;

    //create texture for render
    texture = SDL_CreateTexture(renderer,
                                pixformat,
                                SDL_TEXTUREACCESS_STREAMING,    //变化频繁
                                video_width,
                                video_height);

    //open yuv file
    video_fd = fopen(path, "r");
    if( !video_fd )
    {
        fprintf(stderr, "Failed to open yuv file\n");
        goto __FAIL;
    }

    //read block data
    if((video_buff_len = fread(video_buf, 1, BLOCK_SIZE, video_fd)) <= 0)
    {
        fprintf(stderr, "Failed to read data from yuv file!\n");
        goto __FAIL;
    }

    //set video positon
    video_pos = video_buf;
    video_end = video_buf + video_buff_len;     //用于标记下一次读取文件数据时,文件数据读取到缓冲区的起始位置
    blank_space_len = BLOCK_SIZE - video_buff_len;  //文件读取到末尾时，才有可能值非0，即缓冲区没有存满

    timer_thread = SDL_CreateThread(refresh_video_timer,
                                    NULL,
                                    NULL);
    int CASE = 0;

    do {        //循环，每40ms触发一次纹理更新
        //Wait
        SDL_WaitEvent(&event);
        if(event.type==REFRESH_EVENT)   //每次更新一帧的纹理，缓冲区指针video_pos往后走yuv_frame_len的长度
        {
            /*经典读取数据方式:将文件中的数据一块块拷贝到缓冲区里，
            当缓冲区满了就开始读里面的数据，等缓冲区数据不够了，再从文件读取一块。*/

            //not enought data to render
            if((video_pos + yuv_frame_len) > video_end) //缓冲区可能读取到末尾了，也可能有数据残余但不够一次YUV的数据
            {
                //have remain data, but there isn't space.
                remain_len = video_end - video_pos;     //remain_len >= 0 可能刚好到末尾了，也可能有数据残余

                /*第一种情况:文件未读取到末尾的前提下,缓冲区有残余数据不够一次YUV的数据
                这时，下一次读取文件时的读取长度需要减去缓冲区剩余的数据长度*/
                if(remain_len && !blank_space_len)
                {
                    //将缓冲区后端残余数据从缓冲区起始位置重新填充
                    memcpy(video_buf, video_pos, remain_len);

                    blank_space_len = BLOCK_SIZE - remain_len;
                    video_pos = video_buf;
                    video_end = video_buf + remain_len;
                    CASE = 1;   //由于缓冲区大小固定，缓冲区指针video_pos步长固定，所以其实只会发生其中一种情况
                }

                //第二种情况:缓冲区的数据刚好读取到末尾了，重置缓冲区指针
                if(video_end == (video_buf + BLOCK_SIZE))
                {
                    video_pos = video_buf;
                    video_end = video_buf;
                    blank_space_len = BLOCK_SIZE;
                    CASE = 2;
                }

                /*
                (待定)不会有第三种情况remain_len && blank_space_len (文件到结尾，但不足一帧)
                如果下次读取的文件数据不足一帧，那么上一次缓冲区必定有残留数据跟这次的数据组
                成完整一帧，因为文件数据一定是一帧帧完整的
                所以情况一的删去 && !blank_space_len 好像没有影响？
                */

                //缓冲区数据不够，从文件读取一块数据到缓冲区
                video_buff_len = fread(video_end, 1, blank_space_len, video_fd);

                if(video_buff_len <= 0)
                {
                    fprintf(stderr, "eof, exit thread!");
                    thread_exit = 1;
                    continue;// to wait event for exiting
                }

                //reset video_end
                video_end += video_buff_len;
                blank_space_len -= video_buff_len;
                SDL_Log("not enought data: pos:%p, video_end:%p, blank_space_len:%d, case %d\n", video_pos, video_end, blank_space_len,CASE);
            }

            //更新纹理，一次更新一帧,将YUV像素数据拷贝到纹理
            SDL_UpdateTexture( texture, NULL, video_pos, video_width);  //video_width是一行像素的Y数据量

            //FIX: If window is resize
            rect.x = 0;
            rect.y = 0;
            rect.w = w_width;
            rect.h = w_height;

            SDL_RenderClear( renderer );
            SDL_RenderCopy( renderer, texture, NULL, &rect);    //纹理拷贝到渲染器（显卡驱动），rect用于确定SDL_Texture显示的位置(范围)
            SDL_RenderPresent( renderer );  //展示到屏幕

            video_pos += yuv_frame_len;

        }
        else if(event.type==SDL_WINDOWEVENT)
            //If Resize
            SDL_GetWindowSize(win, &w_width, &w_height);        
        else if(event.type==SDL_QUIT)
            thread_exit=1;        
        else if(event.type==QUIT_EVENT)
            break;

    }while (1);

    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(win);
__FAIL:

    //close file
    if(video_fd)
       fclose(video_fd);
    //关闭窗口关闭线程呢？？
    SDL_Quit();

    return 0;
}
