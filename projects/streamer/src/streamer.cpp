/***********************************************************
 * @File Name:streamer.cpp
 * @Author:chenzhengqiang
 * @company:swwy
 * @Date 2015/3/26
 * @Modified:
 * @Version:1.0
 * @Desc:the kernel of the entire streaming server
 **********************************************************/

#include "common.h"
#include "streamer.h"
#include "state_server.h"
#include "streamerutility.h"
#include "logging.h"
#include <sys/resource.h>

using std::string;
using std::cout;
using std::endl;

//specify the max open files
static const size_t MAX_OPEN_FDS=65535;
//specify the buffer size of client's request
static const size_t BUFFER_SIZE= 1024;

//specify the size of work threads pool
static const size_t WORKTHREADS_SIZE=8;

//specify the fragment size
static const size_t FRAGMENT_SIZE = 1024;
//specify the size of cached flv tag in queue
static const size_t CACHED_TAGS_LIMIT = 30;
//used to send camera's status message to notify work thread
static int NOTIFY_PIPE[2];
/*****************************WORKTHREAD  INFO  RELATED*******************************/
static const size_t FLV_HEADER_SIZE=9;
static const size_t TAG_HEADER_SIZE=11;
static const size_t PREVIOUS_TAG_SIZE=4;
static const size_t AUDIO_TAG = 8;
static const size_t VIDEO_TAG = 9;
static const size_t SCRIPT_TAG = 18;
static const size_t TAG_DATA_SIZE=65535;
static const size_t VIEWERS_LIMIT = 2000;
static const size_t CHANNELS_LIMIT = 100;

//the flv tag item
struct FLV_TAG
{
    bool  is_key_frame;
    uint8_t   *TAG;
    size_t  data_size;
    size_t  sent_bytes;
};


//the viewer's queue structure
struct VIEWER_QUEUE
{
    FLV_TAG *flv_tags[CACHED_TAGS_LIMIT];
    size_t front;
    size_t rear;
    bool empty;
};


//every signle viewer own a viewer queue
typedef struct
{
    string channel;
    char IP[INET_ADDRSTRLEN];
    ssize_t client_fd;
    bool send_first; //specify that if it's the first time to send tags to viewer
    bool register_viewer_callback_first;
    bool send_key_frame_first;	//specify that if it's the first time to send key-frame to viewer
    bool not_sent_flv_header_done;
    bool not_sent_http_response_done;
    bool not_sent_script_tag_done;
    bool not_sent_aac_tag_done;
    bool not_sent_avc_tag_done;
    struct ev_io * viewer_watcher;
    VIEWER_QUEUE viewer_queue;
}VIEWER_INFO,*VIEWER_INFO_PTR;


//the workthread's structure,attach the camera's channel to workthread's ID
//save the aac_sequence_header,avc_sequence_header for the media player's sake,
//you need to push them for the first time
//either push the flv tags to viewer through the viewer_queue
typedef std::string CHANNEL;
typedef ssize_t CLIENT_ID;
typedef ssize_t REQUEST_FD;
typedef struct _CAMERAS
{
    bool receive_first;
    bool script_tag_exists;
    char IP[INET_ADDRSTRLEN];
    struct ev_io *camera_watcher;
    struct ev_io *reply_watcher;
    // the flv header hold 9 bytes,and the first PreviousTagSize0 hold 4 bytes but always 0
    // so 9+4
    uint8_t   flv_header[FLV_HEADER_SIZE+PREVIOUS_TAG_SIZE];
    bool not_received_flv_header_done;
    size_t flv_header_sent_bytes;
    size_t flv_header_received_bytes;

    uint8_t tag_header[TAG_HEADER_SIZE];
    bool not_received_tag_header_done;
    size_t tag_header_received_bytes;

    uint8_t *tag_data;
    bool not_received_tag_data_done;
    size_t tag_data_size;
    size_t tag_data_received_bytes;

    //for flv script tag
    uint8_t  *flv_script_tag;
    size_t  script_tag_total_bytes;
    size_t  script_tag_sent_bytes;

    //for flv's aac sequence header tag
    uint8_t  *flv_aac_tag;
    size_t  aac_tag_total_bytes;
    size_t  aac_tag_sent_bytes;

    //for flv's avc sequence header tag
    uint8_t  *flv_avc_tag;
    size_t  avc_tag_total_bytes;
    size_t  avc_tag_sent_bytes;
    std::map<CLIENT_ID, VIEWER_INFO_PTR> viewer_info_pool;

}CAMERAS,*CAMERAS_PTR;



/*
 *@desc:every single thread owns a workthread_loop
 * to provide workthread's event loop
 * and the "async_watcher" is used to wake up our workthread event loop
 */
struct RESOURCE_INFO
{
     std::string IP;
     std::set<CLIENT_ID> socketfd_pool;
};

struct WORKTHREAD_INFO
{
    pthread_t thread_id;
    struct ev_loop *workthread_loop;
    struct ev_async *async_watcher;
    std::map<CHANNEL,CAMERAS_PTR> camera_info_pool;
    std::map<CHANNEL,RESOURCE_INFO> resource_info_pool;
};
static std::vector<WORKTHREAD_INFO> workthread_info_pool;



/*
#@desc:usefull aliases for c++ container's iterator
*/
typedef std::map<CHANNEL,CAMERAS_PTR>::iterator & camera_info_reference_iter;
typedef std::map<CHANNEL,CAMERAS_PTR>::iterator  camera_info_iter;

typedef std::map<CHANNEL,RESOURCE_INFO>::iterator & backing_resource_reference_iter;
typedef std::map<CHANNEL,RESOURCE_INFO>::iterator  backing_resource_iter;

typedef std::vector<WORKTHREAD_INFO>::iterator & workthread_info_reference_iter;
typedef std::vector<WORKTHREAD_INFO>::iterator workthread_info_iter;

typedef std::map<CLIENT_ID, VIEWER_INFO_PTR>::iterator & viewer_info_reference_iter;
typedef std::map<CLIENT_ID, VIEWER_INFO_PTR>::iterator  viewer_info_iter;


/*********************************END***********************************************/


struct EV_ASYNC_DATA
{
    size_t client_type;
    ssize_t client_fd;
    ssize_t request_fd;
    char *data;
    char channel[99];
};


static const size_t CAMERA = 0;
static const size_t VIEWER = 1;
static const size_t BACKER = 2;
static const size_t NOTIFY = 3;


/*********************************END*************************************/





/***************************the implementation of functions*********************/


/*
#@desc:usefull macros for logging
*/
#define LOG_START(X)  log_module(LOG_DEBUG,X,"###START###:%s",LOG_LOCATION)
#define LOG_DONE(X)   log_module(LOG_DEBUG,X,"###DONE### :%s",LOG_LOCATION)



/*
#@args:
#@returns:
#@desc:as the function name described
*/
static inline void free_workthread_info_pool(void)
{
    LOG_START("FREE_WORKTHREAD_INFO_POOL");
    std::vector<WORKTHREAD_INFO>::iterator wi_iter =  workthread_info_pool.begin();
    while( wi_iter != workthread_info_pool.end() )
    {
        if( wi_iter->workthread_loop != NULL )
        {
             free(wi_iter->workthread_loop);
             wi_iter->workthread_loop = NULL;
        }

        if( wi_iter->async_watcher != NULL )
        {
             free(wi_iter->async_watcher);
             wi_iter->async_watcher = NULL;
        }

        wi_iter->resource_info_pool.clear();
        camera_info_iter ci_iter = wi_iter->camera_info_pool.begin();
        while( ci_iter != wi_iter->camera_info_pool.end() )
        {
            viewer_info_iter vi_iter = ci_iter->second->viewer_info_pool.begin();
            while( vi_iter != ci_iter->second->viewer_info_pool.end() )
            {
                delete vi_iter->second;
                vi_iter->second = NULL;
                ++vi_iter;
            }
            ci_iter->second->viewer_info_pool.clear();
            delete ci_iter->second;
            ci_iter->second = NULL;
            ++ci_iter;
        }
        ++wi_iter;
    }
    workthread_info_pool.clear();
    LOG_DONE("FREE_WORKTHREAD_INFO_POOL");
}



/*
#@args:void
#@returns:CAMERAS_PTR
#@desc:new camera_ptr and initialize it
*/
CAMERAS_PTR new_cameras(struct ev_io * receive_request_watcher, const string & channel )
{
        assert(receive_request_watcher!=NULL);
        CAMERAS_PTR  camera_ptr = new CAMERAS;
        if( camera_ptr == NULL )
        {
            log_module(LOG_INFO,"NEW_VIEWERS","ALLOCATE MEMORY FAILED:%s",LOG_LOCATION);
            return NULL;
        }
        //CAMERAS_PTR INITIALIZATION START
        char *IP = (char *)receive_request_watcher->data;
        memcpy(camera_ptr->IP,IP,INET_ADDRSTRLEN);
        camera_ptr->camera_watcher = new struct ev_io;
        if( camera_ptr->camera_watcher == NULL )
        {
             log_module(LOG_INFO,"NEW_VIEWERS","ALLOCATE MEMORY FAILED:CAMERA_WATCHER=NULL");
             delete camera_ptr;
             camera_ptr = NULL;
             return NULL;
        }
        
        camera_ptr->reply_watcher = new struct ev_io;
        if( camera_ptr->reply_watcher == NULL )
        {
             log_module(LOG_INFO,"NEW_VIEWERS","ALLOCATE MEMORY FAILED:REPLY_WATCHER=NULL");
             delete camera_ptr->camera_watcher;
             camera_ptr->camera_watcher = NULL;
             delete camera_ptr;
             camera_ptr = NULL;
             return NULL;
        }

        char *cstr_channel = new char[channel.length()+1];
        if( cstr_channel == NULL )
        {
             log_module(LOG_INFO,"NEW_VIEWERS","ALLOCATE MEMORY FAILED:cstr_channel=new char[...]");
             delete camera_ptr->camera_watcher;
             camera_ptr->camera_watcher = NULL;
             delete camera_ptr->reply_watcher;
             camera_ptr->reply_watcher = NULL;
             delete camera_ptr;
             camera_ptr = NULL;
             return NULL;
        }

        camera_ptr->camera_watcher->data = (void *)cstr_channel;
        camera_ptr->camera_watcher->fd = receive_request_watcher->fd;
        strcpy(cstr_channel,channel.c_str());
        camera_ptr->receive_first = true;
        camera_ptr->script_tag_exists = false;
        camera_ptr->not_received_flv_header_done = true;
        camera_ptr->not_received_tag_header_done = true;
        camera_ptr->not_received_tag_data_done = true;
        camera_ptr->script_tag_sent_bytes = 0;
        camera_ptr->flv_header_received_bytes = 0;
        camera_ptr->flv_header_sent_bytes = 0;
        camera_ptr->tag_header_received_bytes = 0;
        camera_ptr->tag_data_received_bytes = 0;
        camera_ptr->avc_tag_total_bytes = 0;
        camera_ptr->aac_tag_total_bytes = 0;
        camera_ptr->avc_tag_sent_bytes = 0;
        camera_ptr->aac_tag_sent_bytes = 0;
        camera_ptr->flv_script_tag = NULL;
        camera_ptr->flv_aac_tag = NULL;
        camera_ptr->flv_avc_tag = NULL;
        camera_ptr->tag_data = NULL;
        return camera_ptr;
}



/*
#@args:void
#@returns:VIEWER_INFO_PTR
#@desc:new viewer_info_ptr and initialize it
*/
VIEWER_INFO_PTR new_viewer_info( struct ev_io * receive_request_watcher,HTTP_REQUEST_INFO & req_info )
{
       assert(receive_request_watcher!=NULL);
       VIEWER_INFO_PTR viewer_info_ptr = new VIEWER_INFO;
       if( viewer_info_ptr == NULL )
       {
            log_module(LOG_INFO,"NEW_VIEWER_INFO","ALLOCATE MEMORY FAILED:%s",LOG_LOCATION);
            return NULL;
       }

       viewer_info_ptr->viewer_watcher = new struct ev_io;
       if( viewer_info_ptr == NULL )
       {
            log_module(LOG_INFO,"NEW_VIEWER_INFO","ALLOCATE MEMORY FAILED:%s",LOG_LOCATION);
            delete viewer_info_ptr;
            viewer_info_ptr = NULL;
            return NULL;
       }
       
       viewer_info_ptr->channel = req_info.channel;
       strcpy(viewer_info_ptr->IP,static_cast<char *>(receive_request_watcher->data));
       viewer_info_ptr->send_first = true;
       viewer_info_ptr->register_viewer_callback_first = true;
       viewer_info_ptr->send_key_frame_first = true;
       viewer_info_ptr->client_fd = receive_request_watcher->fd;
       sdk_set_nonblocking(receive_request_watcher->fd);
       sdk_set_keepalive(receive_request_watcher->fd);
       sdk_set_tcpnodelay(receive_request_watcher->fd);
       sdk_set_sndbuf(receive_request_watcher->fd, 65535);
       viewer_info_ptr->not_sent_http_response_done = true;
       viewer_info_ptr->not_sent_flv_header_done = true;
       viewer_info_ptr->not_sent_avc_tag_done = true;
       viewer_info_ptr->not_sent_aac_tag_done = true;
       viewer_info_ptr->not_sent_script_tag_done = true;
       viewer_info_ptr->viewer_queue.empty = true;
       viewer_info_ptr->viewer_queue.front = CACHED_TAGS_LIMIT;
       viewer_info_ptr->viewer_queue.rear = CACHED_TAGS_LIMIT;

       return viewer_info_ptr;
}



/*
#@args:channel[in]
#@returns:true inidicates channel exists,false otherwise
#@desc:as the function name described
*/
bool camera_already_in( const string & channel )
{
    workthread_info_iter wi_iter = workthread_info_pool.begin();
    while( wi_iter != workthread_info_pool.end() )
    {
        camera_info_iter ci_iter = wi_iter->camera_info_pool.find(channel);
        if( ci_iter != wi_iter->camera_info_pool.end() )
            return true;
        ++wi_iter;
    }
    return false;
}


/*
#@args:channel[in]
#@returns:true inidicates channel exists,false otherwise
#@desc:as the function name described
*/
bool resource_already_in( const string & channel, workthread_info_reference_iter wir_iter )
{
    while( wir_iter != workthread_info_pool.end() )
    {
        backing_resource_iter br_iter = wir_iter->resource_info_pool.find(channel);
        if( br_iter != wir_iter->resource_info_pool.end() )
        return true;    
        ++wir_iter;
    }
    return false;
}



/*
#@args:
#@returns:
#@desc:as the function name described,find the channel info's iterator according to the channel
*/
static inline camera_info_iter get_channel_info_item(workthread_info_reference_iter wir_iter, const string & channel )
{
    return wir_iter->camera_info_pool.find(channel);
}



bool get_channel_info_item( const string & channel,camera_info_reference_iter cir_iter )
{
    workthread_info_iter wi_iter = workthread_info_pool.begin();
    while( wi_iter != workthread_info_pool.end() )
    {
        cir_iter = wi_iter->camera_info_pool.find(channel);
        if( cir_iter != wi_iter->camera_info_pool.end() )
            return true;
        ++wi_iter;
    }
    return false;
}


camera_info_iter get_channel_info_item(workthread_info_reference_iter wir_iter ,const CLIENT_ID &client_fd)
{
     camera_info_iter ci_iter = wir_iter->camera_info_pool.begin();
     while( ci_iter != wir_iter->camera_info_pool.end() )
     {
          if( ci_iter->second->camera_watcher->fd == client_fd )
          return ci_iter;
          ++ci_iter;
     }
     return ci_iter;
}


/*
#@args:
#@returns:
#@desc:as the function name described,find the workthread according to the thread_id
*/
workthread_info_iter get_workthread_info_item(pthread_t thread_id)
{
    workthread_info_iter wi_iter = workthread_info_pool.begin();
    while( wi_iter != workthread_info_pool.end() )
    {
        if(pthread_equal(wi_iter->thread_id, thread_id) )
            break;
        ++wi_iter;
    }
    return wi_iter;
}



/*
#@args:
#@returns:
#@desc:as the function name described,find the viewer info's iterator according to the client_id
# notice that,the client_id here is socket fd
*/

static inline viewer_info_iter get_viewer_info_item(camera_info_reference_iter cir_iter, const CLIENT_ID & client_id )
{
    return cir_iter->second->viewer_info_pool.find(client_id);
}



/*
#@args:
#@returns:
#@desc:find the viewer info's iterator according to the client_id
# notice that,the client_id here is socket fd
*/
viewer_info_iter get_viewer_info_item( const CLIENT_ID & client_id )
{
    workthread_info_iter wi_iter = workthread_info_pool.begin();
    while( wi_iter != workthread_info_pool.end() )
    {
        camera_info_iter ci_iter = wi_iter->camera_info_pool.begin();
        while( ci_iter != wi_iter->camera_info_pool.end() )
        {
            viewer_info_iter vi_iter = ci_iter->second->viewer_info_pool.find(client_id);
            if( vi_iter != ci_iter->second->viewer_info_pool.end())
            {
                return vi_iter;
            }
            ++ci_iter;
        }
        ++wi_iter;
    }
    return viewer_info_iter();
}


/*
*@args:
*@retuns:
*@desc:
*/
backing_resource_iter get_backing_resource_item( workthread_info_reference_iter wir_iter, const CLIENT_ID & client_id )
{
     
     backing_resource_iter br_iter;
     br_iter = wir_iter->resource_info_pool.begin();
     while( br_iter != wir_iter->resource_info_pool.end() )
     {
           if( br_iter->second.socketfd_pool.find(client_id) != br_iter->second.socketfd_pool.end())
           return br_iter;
           ++br_iter;
     }  
     return br_iter;
}



/*
#@args:VIEWER_QUEUE[IN],the data struct of our viewer's queue
#@returns:return a boolean type indicates if the viewer's queue is full
#@desc:if the viewer's queue is not empty and the "front" equals to "rear"
#   then we know that the queue is full
*/
static inline bool viewer_queue_full( VIEWER_QUEUE & viewer_queue )
{
    return (!viewer_queue.empty && (viewer_queue.front == viewer_queue.rear));
}


static inline bool viewer_queue_empty(const VIEWER_QUEUE & viewer_queue)
{
    return viewer_queue.empty;
}



/*
#@args:VIEWER_QUEUE[IN],FLV_TAG[OUT]
#@returns:
#@desc:pop the tag in the queue into flv_tag
notice:it's a looping queue
*/

static void viewer_queue_pop( VIEWER_QUEUE & viewer_queue )
{
    if( viewer_queue_empty( viewer_queue ))
        return;
    if(viewer_queue.front == CACHED_TAGS_LIMIT )
    {
        viewer_queue.front = 0;
    }
    
    delete [] (viewer_queue.flv_tags[viewer_queue.front])->TAG;
    (viewer_queue.flv_tags[viewer_queue.front])->TAG = NULL;

    delete viewer_queue.flv_tags[viewer_queue.front];
    viewer_queue.flv_tags[viewer_queue.front] = NULL;

    viewer_queue.front = viewer_queue.front+1;
    if( viewer_queue.front == viewer_queue.rear )
    viewer_queue.empty = true;
}


/*
#@args:VIEWER_QUEUE[IN],FLV_TAG[OUT]
#@returns:
#@desc:obtain the flv_tag in the queue's head but not pop
*/
static void viewer_queue_top( VIEWER_QUEUE & viewer_queue, FLV_TAG * &flv_tag)
{
    if( viewer_queue_empty(viewer_queue) )
        return;
    size_t top = viewer_queue.front;
    if( top == CACHED_TAGS_LIMIT )
        top = 0;
    flv_tag = viewer_queue.flv_tags[top];
}



/*
#@args:VIEWER_QUEUE[IN]
#@returns:
#@desc:as its name described,delete all the elements in viewer queue
*/
void delete_viewer_queue( VIEWER_QUEUE & viewer_queue )
{
    while( ! viewer_queue_empty(viewer_queue))
    {
         viewer_queue_pop(viewer_queue);
    }
}



/*
#@desc:as its name described,push the flv tag into the viewer's queue
*/
static void viewer_queue_push( VIEWER_QUEUE & viewer_queue, FLV_TAG & flv_tag)
{
    viewer_queue.empty = false;
    if( viewer_queue.rear == CACHED_TAGS_LIMIT )
    {
        viewer_queue.rear = 0;
    }

    viewer_queue.flv_tags[viewer_queue.rear] = new FLV_TAG;
    if( viewer_queue.flv_tags[viewer_queue.rear] == NULL )
    {
        log_module(LOG_ERROR,"VIEWER_QUEUE_PUSH","Allocate Memory Failed:%s",LOG_LOCATION);
    }

    (viewer_queue.flv_tags[viewer_queue.rear])->is_key_frame= flv_tag.is_key_frame;
    (viewer_queue.flv_tags[viewer_queue.rear])->data_size = flv_tag.data_size;
    (viewer_queue.flv_tags[viewer_queue.rear])->TAG = new uint8_t[flv_tag.data_size];
    if(  ( viewer_queue.flv_tags[viewer_queue.rear])->TAG == NULL  )
    {
        log_module(LOG_ERROR,"VIEWER_QUEUE_PUSH","Allocate Memory Failed:%s",LOG_LOCATION);
        exit(EXIT_FAILURE);
    }
    memcpy((viewer_queue.flv_tags[viewer_queue.rear])->TAG, flv_tag.TAG, flv_tag.data_size);
    (viewer_queue.flv_tags[viewer_queue.rear])->sent_bytes = 0;
    viewer_queue.rear +=1;
}



/*
#@args:the "host" stands  for the hostname
# and the "service" stands for our streaming server's name which registered in /etc/services
#@returns:return a socket descriptor for listening
#@desc:obtain the socket descriptor through function TCP_LISTEN which described in streamerutility.cpp
*/
int register_streamer_server( const char *host,const char *service)
{
    LOG_START("REGISTER_STREAMER_SERVER");
    int listen_fd;
    listen_fd = tcp_listen(host,service);
    if( listen_fd == -1 )
    {
        log_module(LOG_ERROR,"REGISTER_STREAMER_SERVER","TCP_LISTEN");
    }
    LOG_DONE("REGISTER_STREAMER_SERVER");
    return listen_fd;
}



/*
#@args:"workthreads_size" specify the size of work threads
#@returns:no returns
#@desc:create the work thread in a loop,meanwhile saving them to the pool
*/
struct WORKTHREAD_LOOP_INFO
{
    struct ev_loop    *workthread_loop;
    struct ev_async  *async_watcher;
    size_t type;
};
void init_workthread_info_pool( CONFIG & config, size_t workthreads_size )
{
    LOG_START("INIT_WORKTHREAD_INFO_POOL");
    ssize_t ret;
    //create our work threads in a loop
    log_module(LOG_DEBUG,"INIT_WORKTHREAD_INFO_POOL","CAMERA STREAM RELATED--START");
    pthread_attr_t thread_attr;
    pthread_t thread_id;
    WORKTHREAD_LOOP_INFO * workthread_loop_info = NULL;
    for( size_t index=0; index < workthreads_size; ++index )
    {
        
        pthread_attr_init(&thread_attr);
        pthread_attr_setdetachstate(&thread_attr, PTHREAD_CREATE_DETACHED);
        WORKTHREAD_INFO workthread_info;
        workthread_loop_info = new WORKTHREAD_LOOP_INFO;
        if( workthread_loop_info == NULL )
        {
            log_module(LOG_ERROR,"INIT_WORKTHREAD_INFO_POOL","MALLOC EV_LOOP_INFO FAILED");
        }
        workthread_loop_info->workthread_loop = ev_loop_new(EVBACKEND_EPOLL | EVFLAG_NOENV);
        if( workthread_loop_info->workthread_loop == NULL )
        {
            delete workthread_loop_info;
            workthread_loop_info = NULL;
            log_module(LOG_ERROR,"INIT_WORKTHREAD_INFO_POOL","MALLOC WORKTHREAD LOOP FAILED");   
        }
        
        workthread_loop_info->async_watcher = new ev_async;
        if(  workthread_loop_info->async_watcher == NULL )
        {
             free(workthread_loop_info->workthread_loop);
             workthread_loop_info->workthread_loop = NULL;
             delete workthread_loop_info;
             workthread_loop_info = NULL;
             log_module(LOG_ERROR,"INIT_WORKTHREAD_INFO_POOL","MALLOC WORKTHREAD LOOP FAILED");   
        }
        
        workthread_info.workthread_loop = workthread_loop_info->workthread_loop;
        workthread_info.async_watcher = workthread_loop_info->async_watcher;
        workthread_loop_info->type = 99;
        ret = pthread_create(&thread_id,&thread_attr,workthread_entry,(void *)workthread_loop_info);
        if(  ret != 0 )
        {
            log_module(LOG_ERROR,"INIT_WORKTHREAD_POOL","PTHREAD_CREATE FAILED:%s",LOG_LOCATION);
        }
        workthread_info.thread_id = thread_id;
        workthread_info_pool.push_back(workthread_info);
    }
    log_module(LOG_DEBUG,"INIT_WORKTHREAD_INFO_POOL","CAMERA STREAM RELATED--DONE");

    //now startup the work thread for notifying camera's status to those servers of GSLB 
    log_module(LOG_DEBUG,"INIT_WORKTHREAD_INFO_POOL","CAMERA STATUS RELATED--START");
    pthread_attr_init(&thread_attr);
    pthread_attr_setdetachstate(&thread_attr, PTHREAD_CREATE_DETACHED);
    
    ret = pthread_create(&thread_id,&thread_attr,notify_server_entry,(void *)config.notify_server_file.c_str());
    if(  ret != 0 )
    {
          log_module(LOG_ERROR,"INIT_WORKTHREAD_POOL","PTHREAD_CREATE FAILED:%s",LOG_LOCATION);
    }
    log_module(LOG_DEBUG,"INIT_WORKTHREAD_INFO_POOL","CAMERA STATUS RELATED--DONE");
    
    LOG_DONE("INIT_WORKTHREAD_INFO_POOL");
}



/*
#@desc:providing the idle callback function in workthread event loop,in which
# you can do the logging either collecting the statistics of streaming server
# and so on
*/
void workthread_idle_cb(struct ev_loop * workthread_loop, struct ev_idle *idle_watcher, int revents)
{
    (void)workthread_loop;
    (void)idle_watcher;
    (void)revents;
}



/*
#@args:viewer_queue[IN]
#@returns:
#@desc:just drop the tags in the viwer's queue,none-key frame first
#   and if there are full of key-frame in viewer's queue,then just drop the
#   tag in the queue's head
*/
void drop_frame( VIEWER_QUEUE & viewer_queue )
{
    //firstly find the none key frame in this viewer's queue, if failed
    //directly pop item from viewer's queue
    /*int pointer = viewer_queue.front;
    if( pointer == CACHED_TAGS_LIMIT )
    {
        pointer = 0;
    }

    do
    {
        //now go to find the none key frame from viewer's queue
        if( !( (viewer_queue.flv_tags[pointer])->is_key_frame ) )
        {
            delete [] ( viewer_queue.flv_tags[pointer])->TAG;
            (viewer_queue.flv_tags[pointer])->TAG = NULL;

            int cur_pos = pointer;
            int back_pos = (pointer+1) % CACHED_TAGS_LIMIT;

            while( back_pos  != viewer_queue.rear )
            {
                (viewer_queue.flv_tags[cur_pos])->TAG  = (viewer_queue.flv_tags[back_pos])->TAG;
                (viewer_queue.flv_tags[cur_pos])->is_key_frame = (viewer_queue.flv_tags[back_pos])->is_key_frame;
                (viewer_queue.flv_tags[cur_pos])->data_size = (viewer_queue.flv_tags[back_pos])->data_size;
                (viewer_queue.flv_tags[cur_pos])->sent_bytes = (viewer_queue.flv_tags[back_pos])->sent_bytes;

                cur_pos = back_pos;
                //it will cause dead loop
                //when rear equals to CACHED_TAGS_LIMIT,and back_pos is CACHED_TAGS_LIMIT-1
                //then back_pos is 0 when you do the following:back_pos = (back_pos+1) % CACHED_TAGS_LIMIT
                back_pos = (back_pos + 1) % CACHED_TAGS_LIMIT;
            }

            if( viewer_queue.rear == 0 )
            {
                viewer_queue.rear = CACHED_TAGS_LIMIT;
            }
            else
            {
                viewer_queue.rear = viewer_queue.rear-1;
            }
            return;
        }
        pointer=(pointer+1) % CACHED_TAGS_LIMIT;
    } while( pointer != viewer_queue.rear);
   
    if( pointer == viewer_queue.rear )
    {
        //knowing that the viewer's queue is full of key frame, then just drop the key-frame in the queue's head
        //log_module(LOG_DEBUG,"DROP_FRAME","drop the key-frame in the queue's head");
        viewer_queue_pop(viewer_queue);
    }*/
       viewer_queue_pop(viewer_queue);
}




/*
#@desc:receiving the stream pushed from the camera
*/
void receive_stream_cb(struct ev_loop * workthread_loop, struct  ev_io *camera_watcher, int revents )
{
    //usefull macros for deleting channel when camera has stoped to push stream
    //meanwhile delete all the viewers
    #define DELETE_CAMERA_IF(X,Y) \
    if((X) == (Y))\
    {\
        char MSG[99];\
        snprintf(MSG,99,"CAMERA %s OFF LINE",ci_iter->second->IP);\
        log_module(LOG_INFO,"RECEIVE_STREAM_CB",MSG);\
        if( !ci_iter->second->viewer_info_pool.empty() )\
        {\
            viewer_info_iter vi_iter = ci_iter->second->viewer_info_pool.begin();\
            while( vi_iter != ci_iter->second->viewer_info_pool.end())\
            {\
                close(vi_iter->second->viewer_watcher->fd);\
                if( vi_iter->second->viewer_watcher != NULL )\
                {\
                     delete vi_iter->second->viewer_watcher;\
                     vi_iter->second->viewer_watcher = NULL;\
                }\
                vi_iter->second->viewer_watcher = NULL;\
                delete_viewer_queue(vi_iter->second->viewer_queue);\
                delete vi_iter->second;\
                vi_iter->second=NULL;\
                ci_iter->second->viewer_info_pool.erase(vi_iter);\
                ++vi_iter;\
            }\
        }\
        NOTIFY_DATA notify_data;\
        strcpy(notify_data.channel,cstr_channel);\
        notify_data.flag = 98;\
        write_specify_size2(NOTIFY_PIPE[1],&notify_data,sizeof(notify_data));\
        close(camera_watcher->fd);\
        if( ci_iter->second->flv_script_tag != NULL )\
        delete [] ci_iter->second->flv_script_tag;\
        ci_iter->second->flv_script_tag = NULL;\
        if( ci_iter->second->flv_avc_tag != NULL )\
        delete [] ci_iter->second->flv_avc_tag;\
        ci_iter->second->flv_avc_tag = NULL;\
        if( ci_iter->second->flv_aac_tag != NULL )\
        delete [] ci_iter->second->flv_aac_tag;\
        ci_iter->second->flv_aac_tag = NULL;\
        delete ci_iter->second;\
        ci_iter->second = NULL;\
        wi_iter->camera_info_pool.erase(ci_iter);\
        ev_io_stop(workthread_loop,camera_watcher);\
        delete camera_watcher;\
        camera_watcher = NULL;\
        return;\
    }

    (void)workthread_loop;
    uint8_t tag_type;
    bool is_key_frame = false;
    bool tag_is_sequence_header = false;
    if( EV_ERROR & revents )
    {
        log_module(LOG_INFO,"RECEIVE_STREAM_CB","EV_ERROR %s",LOG_LOCATION);
        return;
    }

    //log_module(LOG_DEBUG,"RECEIVE_STREAM_CB","thread_id:%u",pthread_self());
    workthread_info_iter wi_iter;
    camera_info_iter ci_iter;
    //just initialize once for the capability's sake
    wi_iter = get_workthread_info_item(pthread_self());
    char * cstr_channel =(char *)camera_watcher->data;
    string channel(cstr_channel);
    ci_iter = get_channel_info_item(wi_iter,channel);
    if( ci_iter == wi_iter->camera_info_pool.end())
    return;    
    size_t received_bytes = 0;
    size_t total_bytes = 0;
    if(ci_iter->second == NULL )
    return;    
    if( ci_iter->second->receive_first )
    {
        //now parse the flv header and the first previous tag
        if( ci_iter->second->not_received_flv_header_done )
        {
            total_bytes = FLV_HEADER_SIZE+PREVIOUS_TAG_SIZE-ci_iter->second->flv_header_received_bytes;
            received_bytes = read_specify_size(camera_watcher->fd,
                    ci_iter->second->flv_header+ci_iter->second->flv_header_received_bytes,
                    total_bytes);
            ci_iter->second->flv_header_received_bytes += received_bytes;
            DELETE_CAMERA_IF(received_bytes,0);
            if( received_bytes != total_bytes )
            {
                return;
            }
            ci_iter->second->not_received_flv_header_done = false;
        }

        log_module(LOG_DEBUG,"RECEIVE_STREAM_CB","signature:%c%c%c version %d",
                ci_iter->second->flv_header[0],ci_iter->second->flv_header[1],
                ci_iter->second->flv_header[2],ci_iter->second->flv_header[3]);

        if(( ci_iter->second->flv_header[4] & 0x04 ) == 4 )
        {
            log_module(LOG_DEBUG,"RECEIVE_STREAM_CB","VIDEO are present");
        }
        if(( ci_iter->second->flv_header[4] & 0x01 ) == 1 )
        {
            log_module(LOG_DEBUG,"RECEIVE_STREAM_CB","AUDIO are present");
        }
        ci_iter->second->receive_first = false;
        log_module(LOG_DEBUG,"RECEIVE_STREAM_CB","now always receive the stream pushed from the camera unless stoped");
    }


    if( ci_iter->second->not_received_tag_header_done )
    {
        //now parse the flv tag header 
        total_bytes = TAG_HEADER_SIZE-ci_iter->second->tag_header_received_bytes;
        received_bytes = read_specify_size(camera_watcher->fd,
                ci_iter->second->tag_header+ci_iter->second->tag_header_received_bytes , total_bytes);

        ci_iter->second->tag_header_received_bytes += received_bytes;
        DELETE_CAMERA_IF(received_bytes,0);
        if( received_bytes != total_bytes )
        {
            return;
        }
       
        ci_iter->second->not_received_tag_header_done = false;
        ci_iter->second->tag_header_received_bytes = 0;
        ci_iter->second->tag_data_size = ci_iter->second->tag_header[1] * 65536+ci_iter->second->tag_header[2]*256
            +ci_iter->second->tag_header[3]+PREVIOUS_TAG_SIZE;//compute the data size
        ci_iter->second->tag_data = new uint8_t[ci_iter->second->tag_data_size];
        if(  ci_iter->second->tag_data == NULL  )
        {
            log_module(LOG_ERROR,"RECEIVE_STREAM_CB","Failed To Allocate Memory %s",LOG_LOCATION);
            return;
        }
    }

    //log_module(LOG_DEBUG,"RECEIVE_STREAM_CB","READ_SPECIFY_SIZE DONE:read the tag_header %d bytes",ret);
    //detailed information about kinds of field of flv media file,see the document pulished in adobe
    if( ci_iter->second->not_received_tag_data_done )
    {
        //now parse the flv's tag data
        total_bytes = ci_iter->second->tag_data_size-ci_iter->second->tag_data_received_bytes;
        received_bytes = read_specify_size(camera_watcher->fd,
                ci_iter->second->tag_data+ci_iter->second->tag_data_received_bytes,
                total_bytes);

        DELETE_CAMERA_IF(received_bytes,0);
        ci_iter->second->tag_data_received_bytes += received_bytes;
        if( received_bytes != total_bytes )
        {
            return;
        }
        
        ci_iter->second->not_received_tag_data_done = false;
        ci_iter->second->tag_data_received_bytes = 0;
    }

    size_t packet_size = TAG_HEADER_SIZE  + ci_iter->second->tag_data_size;
    uint8_t *packet = new uint8_t[packet_size];
    //std::vector<uint8_t> packet(packet_size);
    if( packet == NULL )
    {
        log_module(LOG_ERROR,"RECEIVE_STREAM_CB","Allocate Memory Failed:uint8_t *packet= new uint8_t[packet_size]");
        return;
    }

    memcpy(packet,ci_iter->second->tag_header,TAG_HEADER_SIZE);
    memcpy(packet+TAG_HEADER_SIZE, ci_iter->second->tag_data, ci_iter->second->tag_data_size);

    tag_type =  ci_iter->second->tag_header[0] & 0x1F;
    switch(  tag_type )
    {
        case AUDIO_TAG:
            //now parse the flv audio tag
            //log_module(LOG_DEBUG,"RECEIVE_STREAM_CB","now handle  the flv audio tag");
            if( ( ci_iter->second->tag_data[0] & 0xF0 ) >> 4 == 10 )
            {
                //log_module(LOG_DEBUG,"RECEIVE_STREAM_CB","this is the AAC data");
                uint8_t aac_packet_type = ci_iter->second->tag_data[1];
                if( aac_packet_type == 0 )
                {
                    log_module(LOG_DEBUG,"RECEIVE_STREAM_CB","This Is AAC Sequence Header");
                    tag_is_sequence_header = true;
                    ci_iter->second->flv_aac_tag = new uint8_t[packet_size];
                    if( ci_iter->second->flv_aac_tag == NULL )
                    {
                         log_module(LOG_INFO,"RECEIVE_STREAM_CB","ALLOCATE MEMORY FAILED:%s",LOG_LOCATION);
                         DELETE_CAMERA_IF(0,0);
                    }
                    memcpy(ci_iter->second->flv_aac_tag,packet,packet_size);
                    ci_iter->second->aac_tag_total_bytes = packet_size;
                    ci_iter->second->aac_tag_sent_bytes = 0;
                    log_module(LOG_DEBUG,"RECEIVE_STREAM_CB","Handle The AAC Sequence Header Done");
                }
                else if(  aac_packet_type == 1 )
                {
                    //log_module(LOG_DEBUG,"RECEIVE_STREAM_CB","This Is Audio Payload");
                }
            }
            else
            {
                //log_module(LOG_DEBUG,"RECEIVE_STREAM_CB","This Isn't the AAC DATA");
            }
            break;
        case VIDEO_TAG:
            //now parse the flv video tag
            //log_module(LOG_DEBUG,"RECEIVE_STREAM_CB","now handle  the flv video tag");
            if ( (ci_iter->second->tag_data[0] & 0xF0) >> 4 == 1 )
            {
                is_key_frame = true;
                //log_module(LOG_DEBUG,"RECEIVE_STREAM_CB","This Is Video's Key Frame");
            }
            else
            {
                //log_module(LOG_DEBUG,"RECEIVE_STREAM_CB","This Isn't Video's Key Frame");
            }

            if( ( ci_iter->second->tag_data[0]  & 0x0F ) == 7 )
            {
                //log_module(LOG_DEBUG,"RECEIVE_STREAM_CB","this is the AVC data");
                uint8_t avc_packet_type = ci_iter->second->tag_data[1];
                if( avc_packet_type == 0 )
                {
                    log_module(LOG_DEBUG,"RECEIVE_STREAM_CB","This Is AVC Sequence Header");
                    tag_is_sequence_header = true;
                    ci_iter->second->avc_tag_total_bytes = packet_size;
                    ci_iter->second->avc_tag_sent_bytes = 0;
                    ci_iter->second->flv_avc_tag = new uint8_t[packet_size];
                    if(  ci_iter->second->flv_avc_tag == NULL )
                    {
                         log_module(LOG_INFO,"RECEIVE_STREAM_CB","ALLOCATE MEMORY FAILED:%s",LOG_LOCATION);
                         DELETE_CAMERA_IF(0,0);
                    }
                    memcpy(ci_iter->second->flv_avc_tag,packet,packet_size);
                    log_module(LOG_DEBUG,"RECEIVE_STREAM_CB","Handle The AVC Sequence Header Done");
                }
                else
                {
                    //log_module(LOG_DEBUG,"RECEIVE_STREAM_CB","This Isn't the AVC DATA");
                }
            }
            break;
        case SCRIPT_TAG:
            //now parse the flv script tag
            log_module(LOG_DEBUG,"RECEIVE_STREAM_CB","now handle  the flv script tag");
            tag_is_sequence_header = true;
            ci_iter->second->script_tag_exists = true;
            ci_iter->second->script_tag_total_bytes = packet_size;
            ci_iter->second->script_tag_sent_bytes = 0;
            ci_iter->second->flv_script_tag = new uint8_t[packet_size];
            if( ci_iter->second->flv_script_tag == NULL )
            {
                  log_module(LOG_INFO,"RECEIVE_STREAM_CB","ALLOCATE MEMORY FAILED:%s",LOG_LOCATION);
                  DELETE_CAMERA_IF(0,0);
            }
            memcpy(ci_iter->second->flv_script_tag,packet,packet_size);
            log_module(LOG_DEBUG,"RECEIVE_STREAM_CB","handle  the flv script tag done");
            break;
    }

    delete [] ci_iter->second->tag_data;
    ci_iter->second->tag_data = NULL;

    if( ! tag_is_sequence_header )
    {

        //if the flv tag is none of aac sequence header tag, svc sequence header tag or script tag
        //then merely push them into the viewer's queue
        FLV_TAG flv_tag;
        flv_tag.TAG = packet;
        flv_tag.data_size = packet_size;
        flv_tag.sent_bytes = 0;
        flv_tag.is_key_frame = is_key_frame ? true:false;

        //now parse tags ok and send the tags to all viewers
        if( !( ci_iter->second->viewer_info_pool.empty() ) )
        {
            viewer_info_iter vi_iter = ci_iter->second->viewer_info_pool.begin();
            while(vi_iter != ci_iter->second->viewer_info_pool.end())
            {
                  if( viewer_queue_full(vi_iter->second->viewer_queue) )
                 {
                      //network congestion might happened,now drop the none-key frame first
                      log_module(LOG_INFO,"RECEIVE_STREAM_CB","NETWORK CONGESTION MIGHT HAPPENED FROM:%s",vi_iter->second->IP);
                      drop_frame(vi_iter->second->viewer_queue);
                 }
                 if( vi_iter->second->register_viewer_callback_first )
                 {
                      vi_iter->second->register_viewer_callback_first = false;
                      ev_io_init(vi_iter->second->viewer_watcher,send_tags_cb,
                      vi_iter->second->client_fd,EV_WRITE);
                 }
                 ev_io_start(workthread_loop,vi_iter->second->viewer_watcher);
                 viewer_queue_push(vi_iter->second->viewer_queue,flv_tag);
                ++vi_iter;
            }
        }
        else
        {
           ;
        }
    }

    delete [] packet;
    packet = NULL;
    tag_is_sequence_header = false;
    is_key_frame = false;
    memset(ci_iter->second->tag_header,0,TAG_HEADER_SIZE);
    ci_iter->second->not_received_tag_header_done = true;
    ci_iter->second->not_received_tag_data_done = true;
}



/*
#@args:
#@desc:parse the flv stream and send the cached tags to the viwer
*/
void send_tags_cb(struct ev_loop * workthread_loop, struct  ev_io *viewer_watcher, int revents )
{
    //usefull macros for deleting the viewer's queue
    #define DELETE_VIEWER_IF(X,Y) \
    if((X) == (Y)) \
    {\
        char MSG[99];\
        snprintf(MSG,99,"VIEWER %s OFF LINE",vi_iter->second->IP);\
        log_module(LOG_INFO,"SEND_TAGS_CB",MSG);\
        close(viewer_watcher->fd);\
        delete_viewer_queue(vi_iter->second->viewer_queue);\
        delete vi_iter->second;\
        vi_iter->second=NULL;\
        ci_iter->second->viewer_info_pool.erase(vi_iter);\
        ev_io_stop(workthread_loop,viewer_watcher);\
        return;\
    }

    if( EV_ERROR & revents )
    {
        log_module(LOG_INFO,"SEND_TAGS_CB","EV_ERROR %s",LOG_LOCATION);
        return;
    }

    size_t total_bytes = 0;
    static size_t total_sent_bytes = 0;
    int sent_bytes = -1;
    workthread_info_iter wi_iter = get_workthread_info_item(pthread_self());
    if( wi_iter == workthread_info_pool.end())
    {
        log_module(LOG_DEBUG,"SEND_TAGS_CB","workthread_info_pool.end() unknown error occurred");
        return;
    }

    viewer_info_iter vi_iter = get_viewer_info_item(viewer_watcher->fd);
    camera_info_iter ci_iter = get_channel_info_item(wi_iter,vi_iter->second->channel);

    if(  vi_iter->second->send_first )
    {
        //send_first indicates send the flv header,avc tag,aac tag,script tag for once
        if( vi_iter->second->not_sent_http_response_done )
        {
            log_module(LOG_DEBUG,"SEND_TAGS_CB","START SEND HTTP 200 RESPONSE TO VIEWER");
            static const char *http_response="HTTP/1.1 200 OK\r\nContent-type:video/x-flv\r\n\r\n";
            static int sent_http_response_bytes = 0;
            total_bytes = strlen(http_response)-sent_http_response_bytes;
            sent_bytes = write_specify_size(viewer_watcher->fd,http_response+sent_http_response_bytes,
                    total_bytes);
            DELETE_VIEWER_IF(sent_bytes,-1);
            if( (size_t)sent_bytes != total_bytes )
            {
                sent_http_response_bytes += sent_bytes;
                return;
            }
            vi_iter->second->not_sent_http_response_done = false;
            log_module(LOG_DEBUG,"SEND_TAGS_CB","SEND HTTP 200 RESPONSE DONE");
        }


        if( vi_iter->second->not_sent_flv_header_done )
        {
            log_module(LOG_DEBUG,"SEND_TAGS_CB","NOW SEND THE FLV HEADER");
            total_bytes = sizeof(ci_iter->second->flv_header)-ci_iter->second->flv_header_sent_bytes;
            sent_bytes= write_specify_size(viewer_watcher->fd,
                    ci_iter->second->flv_header+ci_iter->second->flv_header_sent_bytes,
                    total_bytes);

            DELETE_VIEWER_IF(sent_bytes,-1);
            if( (size_t)sent_bytes != total_bytes )
            {
                ci_iter->second->flv_header_sent_bytes += sent_bytes;
                return;
            }
            vi_iter->second->not_sent_flv_header_done = false;
            log_module(LOG_DEBUG,"SEND_TAGS_CB","Signature:%c%c%c Version %d",
                    ci_iter->second->flv_header[0],ci_iter->second->flv_header[1],
                    ci_iter->second->flv_header[2],ci_iter->second->flv_header[3]);
            log_module(LOG_DEBUG,"SEND_TAGS_CB","SEND THE FLV HEADER DONE");
        }


        //if there is script tag in flv file then send it to the viewer
        if( ci_iter->second->script_tag_exists )
        {
            if(  vi_iter->second->not_sent_script_tag_done )
            {
                log_module(LOG_DEBUG,"SEND_TAGS_CB","NOW SEND THE FLV SCRIPT TAG DATA");
                total_bytes = ci_iter->second->script_tag_total_bytes-ci_iter->second->script_tag_sent_bytes;
                sent_bytes = write_specify_size(viewer_watcher->fd,
                        ci_iter->second->flv_script_tag+ci_iter->second->script_tag_sent_bytes,
                        total_bytes);

                DELETE_VIEWER_IF(sent_bytes,-1);
                if( (size_t)sent_bytes != total_bytes )
                {
                    ci_iter->second->script_tag_sent_bytes += sent_bytes;
                    return;
                }
                log_module(LOG_DEBUG,"SEND_TAGS_CB","SEND THE FLV SCRIPT TAG DATA DONE");
            }
            vi_iter->second->not_sent_script_tag_done = false;
        }

        //avc_tag_total_bytes is not equal to 0,
        //it indiates that there is avc tag in flv file
        if( ci_iter->second->avc_tag_total_bytes != 0 )
        {
            if( vi_iter->second->not_sent_avc_tag_done )
            {
                log_module(LOG_DEBUG,"SEND_TAGS_CB","NOW SEND THE FLV AVC TAG DATA");
                total_bytes = ci_iter->second->avc_tag_total_bytes-ci_iter->second->avc_tag_sent_bytes;
                sent_bytes = write_specify_size(viewer_watcher->fd,
                        ci_iter->second->flv_avc_tag+ci_iter->second->avc_tag_sent_bytes,
                        total_bytes);
                DELETE_VIEWER_IF(sent_bytes,-1);
                if( (size_t)sent_bytes != total_bytes )
                {
                    ci_iter->second->avc_tag_sent_bytes += sent_bytes;
                    return;
                }
                vi_iter->second->not_sent_avc_tag_done = false;
                log_module(LOG_DEBUG,"SEND_TAGS_CB","SEND THE FLV AVC TAG DATA DONE");
            }
        }

        //aac_tag_total_bytes is not equal to 0,
        //it indiates that there is aac tag in flv file
        if( ci_iter->second->aac_tag_total_bytes != 0 )
        {
            if( vi_iter->second->not_sent_aac_tag_done )
            {
                log_module(LOG_DEBUG,"SEND_TAGS_CB","NOW SEND THE FLV AAC TAG DATA");
                total_bytes = ci_iter->second->aac_tag_total_bytes-ci_iter->second->aac_tag_sent_bytes;
                sent_bytes = write_specify_size(viewer_watcher->fd,
                        ci_iter->second->flv_aac_tag+ci_iter->second->aac_tag_sent_bytes,
                        total_bytes);
                DELETE_VIEWER_IF(sent_bytes,-1);
                if( (size_t)sent_bytes != total_bytes )
                {
                    ci_iter->second->aac_tag_sent_bytes += sent_bytes;
                    return;
                }
                log_module(LOG_DEBUG,"SEND_TAGS_CB","SEND THE FLV AAC TAG DATA DONE");
                vi_iter->second->not_sent_aac_tag_done = false;
            }
        }
        vi_iter->second->send_first = false;
        log_module(LOG_DEBUG,"SEND_TAGS_CB","SEND FLV SEQUENCE HEADER--DONE ID:%u",pthread_self());
    }


    //now always send the tags to viewer
    FLV_TAG *flv_tag=NULL;
    if(viewer_queue_empty(vi_iter->second->viewer_queue))
    {
        ev_io_stop(workthread_loop,viewer_watcher);
        return;
    }
    else
    {
        viewer_queue_top(vi_iter->second->viewer_queue, flv_tag);
        //you must send the key frame to viewer the first time
        if( vi_iter->second->send_key_frame_first && !flv_tag->is_key_frame )
        {
            viewer_queue_pop(vi_iter->second->viewer_queue);
            return;
        }
        else
        {
            if( flv_tag->data_size != flv_tag->sent_bytes )
            {
                total_bytes = flv_tag->data_size  - flv_tag->sent_bytes;
                if( total_bytes > FRAGMENT_SIZE )
                {
                    if( total_sent_bytes != FRAGMENT_SIZE )
                    {
		        sent_bytes = write_specify_size(viewer_watcher->fd,flv_tag->TAG+flv_tag->sent_bytes,FRAGMENT_SIZE);
                        DELETE_VIEWER_IF(sent_bytes,-1);
                        total_sent_bytes+=sent_bytes;
                        flv_tag->sent_bytes+=sent_bytes;
                        return;
                    }
                                        	
		}
                
		total_sent_bytes = 0;
                if( total_bytes > 0 )
                {
                   sent_bytes = write_specify_size(viewer_watcher->fd,flv_tag->TAG+flv_tag->sent_bytes,total_bytes);
                   DELETE_VIEWER_IF(sent_bytes,-1);
                   if( (size_t)sent_bytes != total_bytes )
                   {
                       flv_tag->sent_bytes += sent_bytes;
                       return;
                   }
                } 
            }
            flv_tag->sent_bytes = 0;
            flv_tag->data_size = 0;
            viewer_queue_pop(vi_iter->second->viewer_queue);
        }
        vi_iter->second->send_key_frame_first= false;
    }
    ev_io_stop(workthread_loop, viewer_watcher);
}



/*
*args:
*returns:void
*desc:this callback was called asynchronously 
*     when main event loop send signal to workthread's event loop
*/
void async_read_cb(struct ev_loop *workthread_loop, struct ev_async *async_watcher, int revents )
{
     
    //usefull macro for doing something clean
    LOG_START("ASYNC_READ_CB");
    if( EV_ERROR & revents )
    {
        log_module(LOG_INFO,"ASYNC_READ_CB","EV_ERROR %s",LOG_LOCATION);
        LOG_DONE("ASYNC_READ_CB");
        return;
    }

    EV_ASYNC_DATA *async_data = static_cast<EV_ASYNC_DATA *>(async_watcher->data);
    if( async_data == NULL )
    {
         LOG_DONE("ASYNC_READ_CB");
         return;
    }
    
    if( async_data->client_type == CAMERA )
    {
        //now handle camera's request
        log_module(LOG_DEBUG,"ASYNC_READ_CB","CAMERA'S REQUEST--ID:%u",pthread_self());
        string channel;
        channel.assign(async_data->channel);
        workthread_info_iter wi_iter = get_workthread_info_item(pthread_self());
        camera_info_iter ci_iter = get_channel_info_item(wi_iter,channel);
        sdk_set_nonblocking(async_data->client_fd);
        sdk_set_keepalive(async_data->client_fd);
        sdk_set_tcpnodelay(async_data->client_fd);
        sdk_set_rcvbuf(async_data->client_fd, 65535);
        sdk_set_sndbuf(async_data->client_fd,65535);
        ev_io_init(ci_iter->second->reply_watcher,reply_200OK_cb,async_data->client_fd, EV_WRITE);
        ev_io_init(ci_iter->second->camera_watcher,receive_stream_cb,async_data->client_fd, EV_READ);
        ev_io_start(workthread_loop, ci_iter->second->reply_watcher);
        ev_io_start(workthread_loop, ci_iter->second->camera_watcher);
    }
    else if( async_data->client_type == BACKER )
    {
        //now handle backing-resource's request
        log_module(LOG_DEBUG,"ASYNC_READ_CB","VIEWER'S BACKING RESOURCE REQUEST--ID:%u",pthread_self());
        struct ev_io *pull_stream_watcher = new struct ev_io;
        if( pull_stream_watcher == NULL )
        {
              log_module(LOG_INFO,"ASYNC_READ_CB","ALLOCATE MEMORY FAILED:%s",LOG_LOCATION);
              delete [] ((static_cast<EV_ASYNC_DATA*>(async_watcher->data))->data);
              delete static_cast<EV_ASYNC_DATA*>(async_watcher->data);
              async_watcher->data = NULL;
              return;
        }

        struct ev_io *send_backing_resource_watcher = new struct ev_io;
        if( send_backing_resource_watcher == NULL )
        {
              log_module(LOG_INFO,"ASYNC_READ_CB","ALLOCATE MEMORY FAILED:%s",LOG_LOCATION);
              delete pull_stream_watcher;
              pull_stream_watcher = NULL;
              delete [] ((static_cast<EV_ASYNC_DATA*>(async_watcher->data))->data);
              delete static_cast<EV_ASYNC_DATA*>(async_watcher->data);
              async_watcher->data = NULL;
              return;
        }

        struct ev_io *push_stream_watcher = new struct ev_io;
        if( push_stream_watcher == NULL )
        {
              log_module(LOG_INFO,"ASYNC_READ_CB","ALLOCATE MEMORY FAILED:%s",LOG_LOCATION);
              delete pull_stream_watcher;
              pull_stream_watcher = NULL;
              delete send_backing_resource_watcher;
              send_backing_resource_watcher = NULL;
              delete [] ((static_cast<EV_ASYNC_DATA*>(async_watcher->data))->data);
              delete static_cast<EV_ASYNC_DATA*>(async_watcher->data);
              async_watcher->data = NULL;
              return;
        }

        char *imitated_request = new char[strlen(async_data->data)+1];
        if( imitated_request == NULL )
        {
             log_module(LOG_INFO,"ASYNC_READ_CB","ALLOCATE MEMORY FAILED:%s",LOG_LOCATION);
             delete pull_stream_watcher;
             pull_stream_watcher = NULL;
             delete send_backing_resource_watcher;
             send_backing_resource_watcher = NULL;
             delete push_stream_watcher;
             push_stream_watcher = NULL;
             
             delete [] ((static_cast<EV_ASYNC_DATA*>(async_watcher->data))->data);
             delete static_cast<EV_ASYNC_DATA*>(async_watcher->data);
             async_watcher->data = NULL;
             return;
        }
        strcpy(imitated_request, (static_cast<EV_ASYNC_DATA*>(async_watcher->data))->data);
        delete [] ((static_cast<EV_ASYNC_DATA*>(async_watcher->data))->data);
        
        send_backing_resource_watcher->data =(void *)imitated_request;
        pull_stream_watcher->data = (void *)push_stream_watcher;
        sdk_set_nonblocking(async_data->client_fd);
        sdk_set_keepalive(async_data->client_fd);
        sdk_set_tcpnodelay(async_data->client_fd);
        sdk_set_sndbuf(async_data->client_fd,65535);
        sdk_set_rcvbuf(async_data->client_fd,65535);

        sdk_set_nonblocking(async_data->request_fd);
        sdk_set_keepalive(async_data->request_fd);
        sdk_set_tcpnodelay(async_data->request_fd);
        sdk_set_sndbuf(async_data->request_fd,65535);
        sdk_set_rcvbuf(async_data->request_fd,65535);
        
        ev_io_init(send_backing_resource_watcher,send_backing_resource_cb,async_data->client_fd,EV_WRITE);
        ev_io_init(pull_stream_watcher,pull_stream_cb,async_data->client_fd,EV_READ);
        ev_io_init(push_stream_watcher,push_stream_cb,async_data->request_fd,EV_WRITE);
        ev_io_start(workthread_loop, send_backing_resource_watcher);
        ev_io_start(workthread_loop, pull_stream_watcher);
            
    }
    delete static_cast<EV_ASYNC_DATA*>(async_watcher->data);
    async_watcher->data = NULL;
    LOG_DONE("ASYNC_READ_CB");
}



/*
*args:
*returns:
*desc:just send http 200 OK to camera
*/
void reply_200OK_cb(struct ev_loop * workthread_loop, struct  ev_io *reply_watcher, int revents )
{
      if( EV_ERROR & revents )
     {
          log_module(LOG_INFO,"reply_200OK_CB","EV_ERROR %s",LOG_LOCATION);
          return;
     }
     const char *http_200_ok = "HTTP/1.1 200 OK\r\n\r\n";
     write_specify_size(reply_watcher->fd,http_200_ok,strlen(http_200_ok));
     ev_io_stop(workthread_loop,reply_watcher);
     delete reply_watcher;
     reply_watcher = NULL;
}



/*
 *@args:
 *@returns:
 *@desc:
 */
struct STREAM_DATA
{
    uint8_t stream[TAG_DATA_SIZE];
    size_t received_bytes;
};

/*
*@args:
*@returns:void
*@desc:pull the "stream" from another streaming server
*/
void pull_stream_cb(struct ev_loop * workthread_loop, struct  ev_io *pull_stream_watcher, int revents )
{
    #define DELETE_RESOURCE(X,Y) \
    std::set<CLIENT_ID>::iterator it = (Y)->second.socketfd_pool.begin();\
    while( it != br_iter->second.socketfd_pool.end())\
    {\
         close(*it);\
         ++it;\
    }\
    (Y)->second.socketfd_pool.clear();\
    (X)->resource_info_pool.erase(Y);\
    ev_io_stop(workthread_loop,pull_stream_watcher);\
    ev_io_stop(workthread_loop,push_stream_watcher);\
    delete static_cast<struct ev_io *>(pull_stream_watcher->data);\
    pull_stream_watcher->data = NULL;\
    delete pull_stream_watcher;\
    pull_stream_watcher = NULL;\
    return;

        
    (void)workthread_loop;
    if( EV_ERROR & revents )
    {
        log_module(LOG_INFO,"PULL_STREAM_CB","EV_ERROR:%s",LOG_LOCATION);
        return;
    }
    
    struct ev_io *push_stream_watcher = static_cast<struct ev_io *>(pull_stream_watcher->data);
    if( push_stream_watcher->fd == -1 )
    {
         workthread_info_iter wi_iter = get_workthread_info_item(pthread_self());
         backing_resource_iter br_iter = get_backing_resource_item(wi_iter,pull_stream_watcher->fd);
         br_iter->second.socketfd_pool.erase(pull_stream_watcher->fd);
         close(pull_stream_watcher->fd);
         if( br_iter->second.socketfd_pool.empty() )
         {
              log_module(LOG_DEBUG,"PULL_STREAM_CB","SOCKET FD POOL WAS EMPTY--CHANNEL DELETING...");
              wi_iter->resource_info_pool.erase(br_iter);
              log_module(LOG_DEBUG,"PULL_STREAM_CB","SOCKET FD POOL WAS EMPTY--CHANNEL DELETED.");
         }
         ev_io_stop(workthread_loop,pull_stream_watcher);
         delete static_cast<struct ev_io *>(pull_stream_watcher->data);
         pull_stream_watcher->data = NULL;
         delete pull_stream_watcher;
         pull_stream_watcher = NULL;
         return;
    }
   
    STREAM_DATA * stream_data = new STREAM_DATA;
    if( stream_data == NULL )
    {
         workthread_info_iter wi_iter = get_workthread_info_item(pthread_self());
         backing_resource_iter br_iter = get_backing_resource_item(wi_iter,pull_stream_watcher->fd);
         log_module(LOG_INFO,"PULL_STREAM_CB","ALLOCATE MEMORY FAILED:%s",LOG_LOCATION);
         DELETE_RESOURCE(wi_iter,br_iter);
    }
    stream_data->received_bytes=read_specify_size(pull_stream_watcher->fd, stream_data->stream, sizeof(stream_data->stream));
    if( stream_data->received_bytes == 0 )
    {
         workthread_info_iter wi_iter = get_workthread_info_item(pthread_self());
         backing_resource_iter br_iter = get_backing_resource_item(wi_iter,pull_stream_watcher->fd);
         char MSG[99];
         snprintf(MSG,99,"RESOURCE %s HAS STOPED TO PUSH STREAM",br_iter->second.IP.c_str());
         log_module(LOG_INFO,"PULL_STREAM_CB",MSG);
         delete stream_data;
         stream_data = NULL;
         DELETE_RESOURCE(wi_iter,br_iter);
    }
    push_stream_watcher->data = (void *)stream_data;
    ev_io_start(workthread_loop,push_stream_watcher);
}



/*
*@args:
*@returns:void
*@desc:send the stream received from streaming server to viewer
*/
void push_stream_cb(struct ev_loop * workthread_loop, struct  ev_io *push_stream_watcher, int revents )
{
     if( EV_ERROR & revents )
    {
        log_module(LOG_INFO,"PUSH_STREAM_CB","EV_ERROR:%s",LOG_LOCATION);
        return;
    }
    static size_t total_bytes = 0;
    STREAM_DATA *stream_data = static_cast<STREAM_DATA *>(push_stream_watcher->data);
    if( total_bytes != stream_data->received_bytes )
    {
         ssize_t sent_bytes = write_specify_size(push_stream_watcher->fd,
         stream_data->stream+total_bytes,stream_data->received_bytes-total_bytes);
         if( sent_bytes == -1 )
         {
             ev_io_stop(workthread_loop,push_stream_watcher);
             workthread_info_iter wi_iter = get_workthread_info_item(pthread_self());
             backing_resource_iter br_iter = get_backing_resource_item(wi_iter,push_stream_watcher->fd);
             char MSG[99];
             snprintf(MSG,99,"VIEWER %s OFF LINE",br_iter->second.IP.c_str());
             log_module(LOG_INFO,"PUSH_STREAM_CB",MSG);
             br_iter->second.socketfd_pool.erase(push_stream_watcher->fd);
             close(push_stream_watcher->fd);
             delete static_cast<STREAM_DATA*>(push_stream_watcher->data);
             push_stream_watcher->data = NULL;
             push_stream_watcher->fd = -1;
             return;
         }
         total_bytes+=(size_t)sent_bytes;
         if( total_bytes != stream_data->received_bytes )
         return;
         
    }
    total_bytes = 0;
    delete static_cast<STREAM_DATA*>(push_stream_watcher->data);
    push_stream_watcher->data = NULL;
    ev_io_stop(workthread_loop,push_stream_watcher);
}


/*
*@args:
*@returns:void
*@desc:just send the imitated request to another streaming server
*/
void send_backing_resource_cb(struct ev_loop * workthread_loop, struct  ev_io *send_backing_watcher, int revents )
{
    if( EV_ERROR & revents )
    {
        log_module(LOG_INFO,"SEND_BACKING_RESOURCE_CB","EV_ERROR %s",LOG_LOCATION);
        return;
    }
    char * imitated_request = (char*)send_backing_watcher->data;
    log_module(LOG_DEBUG,"SEND_BACKING_RESOURCE_CB","IMITATED REQUEST:%s",imitated_request);
    static size_t total_bytes = 0;
    if( imitated_request != NULL && total_bytes != strlen(imitated_request) )
    {
        int sent_bytes = write_specify_size(send_backing_watcher->fd,
        imitated_request+total_bytes,strlen(imitated_request)-total_bytes);
        total_bytes+=sent_bytes;
        if( sent_bytes != -1 )
       {
           if( total_bytes != strlen(imitated_request))
           {
                return;
           }    
       }
       else
       {
           log_module(LOG_INFO,"SEND_BACKING_RESOURCE_CB","SEND BACKING_RESOURCE REQUEST FAILED");
           workthread_info_iter wi_iter= get_workthread_info_item(pthread_self());
           backing_resource_iter br_iter = get_backing_resource_item(wi_iter,send_backing_watcher->fd);
           br_iter->second.socketfd_pool.clear();
           wi_iter->resource_info_pool.erase(br_iter);
       }
    }
    total_bytes = 0;
    ev_io_stop(workthread_loop, send_backing_watcher);
    delete [] static_cast<char *>(send_backing_watcher->data);
    send_backing_watcher->data = NULL;
    delete send_backing_watcher;
    send_backing_watcher = NULL;
}



/*
*@args:
*@returns:
*@desc:used to notify camera's status to those server or GSLB
*/
void * notify_server_entry( void * args )
{
     std::string notify_server_file(static_cast<char*>(args));
     if( pipe2(NOTIFY_PIPE,O_NONBLOCK) != 0 )
     {
         log_module(LOG_ERROR,"NOTIFY_SERVER_ENTRY","PIPE2 FAILED:%s",strerror(errno));
         exit(EXIT_FAILURE);
     }
     std::vector<NOTIFY_SERVER_INFO> notify_servers_info_pool;
     fd_set read_fds;
     while( true )
     {
         FD_ZERO(&read_fds);
         FD_SET(NOTIFY_PIPE[0],&read_fds);
         int max_fds = NOTIFY_PIPE[0]+1;
         if( -1 == select(max_fds,&read_fds,NULL,NULL,NULL) )
         {
              log_module(LOG_INFO,"NOTIFY_SERVER_ENTRY","SELECT ERROR:%s",strerror(errno));
              pthread_exit(NULL);
         }
         NOTIFY_DATA notify_data;
         int received_bytes = read_specify_size2(NOTIFY_PIPE[0],&notify_data, sizeof(notify_data));
         if( received_bytes == 0 )     
         {
              continue;
         }
         FD_ZERO( &read_fds );
         max_fds = -1;
         std::map<std::string, int> notify_servers;
         read_config2(notify_server_file.c_str(),notify_servers);
         std::map<std::string, int>::iterator it = notify_servers.begin();
         int conn_fd;
         while( it != notify_servers.end() )
         {
              conn_fd= tcp_connect(it->first.c_str(),it->second);
              if( conn_fd == -1 )
              {
                    log_module(LOG_INFO,"NOTIFY_SERVER_ENTRY","TCP_CONNECT FAILED,TRY AGAIN:%s",LOG_LOCATION);
                    ++it;
                    continue;
              }
              sdk_set_sndbuf(conn_fd,65535);
              sdk_set_rcvbuf(conn_fd,65535);
              sdk_set_nonblocking(conn_fd);
              sdk_set_tcpnodelay(conn_fd);
              sdk_set_keepalive(conn_fd);
              NOTIFY_SERVER_INFO notify_server_info;
              notify_server_info.conn_fd = conn_fd;
              notify_server_info.IP = it->first;
              notify_server_info.PORT = it->second;
              notify_servers_info_pool.push_back( notify_server_info );
              ++it;
         }

         std::vector<NOTIFY_SERVER_INFO >::iterator notify_server_iter = notify_servers_info_pool.begin();
         while( notify_server_iter != notify_servers_info_pool.end() )
         {
              do_notify(*notify_server_iter,notify_data);
              ++notify_server_iter;
         }
         notify_servers_info_pool.clear();
     }
     return args;
}



/*
#@desc:just providing the workthread's event loop in work thread routine
*/
void * workthread_entry ( void * args )
{
    log_module(LOG_DEBUG,"WORKTHREAD_ROUTINE START","ID:%u",pthread_self());
    sigset_t signal_mask;
    sigemptyset (&signal_mask);
    sigaddset (&signal_mask, SIGPIPE);
    int ret = pthread_sigmask (SIG_BLOCK, &signal_mask, NULL);
    if (ret != 0)
    {
        log_module(LOG_INFO,"WORKTHREAD_ROUTINE","BLOCK SIGPIPE ERROR");
    }
    WORKTHREAD_LOOP_INFO * workthread_loop_info = static_cast<WORKTHREAD_LOOP_INFO * >(args);

    //register the libev async callback
    //used to receive signal from the main event loop
    ev_async_init(workthread_loop_info->async_watcher, async_read_cb);
    ev_async_start(workthread_loop_info->workthread_loop,workthread_loop_info->async_watcher);
    ev_run(workthread_loop_info->workthread_loop,0);
    free(workthread_loop_info->workthread_loop);
    workthread_loop_info->workthread_loop = NULL;
    delete workthread_loop_info->async_watcher;
    workthread_loop_info->async_watcher = NULL;
    delete static_cast<WORKTHREAD_LOOP_INFO * >(args);
    log_module(LOG_DEBUG,"WORKTHREAD_ROUTINE DONE","ID:%u",pthread_self());
    return NULL;
}



/*
#@args:seeing the detailed information from libev api document
#@returns:void
#@desc:this is the asynchronous callback API defined in libev;
#this function is called asynchronously when streaming server received the client's request
# for establising connection
*/
void accept_cb(struct ev_loop * main_event_loop, struct ev_io * listen_watcher, int revents )
{
    LOG_START("ACCEPT_CB");
    ssize_t client_fd;
    struct sockaddr_in client_addr;
    socklen_t len = sizeof(struct sockaddr_in);
    if( EV_ERROR & revents )
    {
        log_module(LOG_INFO,"ACCEPT_CB","LIBEV ERROR FOR EV_ERROR:%d--%s",EV_ERROR,LOG_LOCATION);
        return;
    }
    client_fd = accept(listen_watcher->fd, (struct sockaddr *)&client_addr, &len );
    if( client_fd < 0 )
    {
        log_module(LOG_INFO,"ACCEPT_CB","ACCEPT ERROR:%d-->%s--%s",errno,strerror(errno),LOG_LOCATION);   
        return;
    }
    
    char *client_ip = new char [INET_ADDRSTRLEN];
    if( client_ip == NULL )
    {
         log_module(LOG_INFO,"ACCEPT_CB","ALLOCATE MEMORY FAILED:%s",LOG_LOCATION);
         close(client_fd);
         return;
    }

    struct ev_io * receive_request_watcher = new struct ev_io;
    if( receive_request_watcher == NULL )
    {
        log_module(LOG_INFO,"ACCEPT_CB","ALLOCATE MEMORY FAILED:%s--%s",strerror(errno),LOG_LOCATION);
        delete [] client_ip;
        client_ip = NULL;
        close(client_fd);
        return;
    }
    receive_request_watcher->data = ( void *)client_ip;
    inet_ntop(AF_INET,&client_addr.sin_addr,client_ip,INET_ADDRSTRLEN);
    log_module(LOG_DEBUG,"ACCEPT_CB","CLIENT %s CONNECTED",client_ip);
    sdk_set_nonblocking(client_fd);
    sdk_set_rcvbuf(client_fd, 65535);
    sdk_set_tcpnodelay(client_fd);
    sdk_set_keepalive(client_fd);

    //register the socket io callback for reading client's request    
    ev_io_init(receive_request_watcher,receive_request_cb,client_fd, EV_READ);
    ev_io_start(main_event_loop,receive_request_watcher);
    LOG_DONE("ACCEPT_CB");
}



/*
#@args:
#@returns:
#@desc:dispatch workthread through simple round-robin
*/
workthread_info_iter get_workthread_through_round_robin()
{
    static int round_robin_val =0;
    workthread_info_iter wi_iter = workthread_info_pool.begin();
    int count = 0;
    while( wi_iter != workthread_info_pool.end() )
    {
        if( count == round_robin_val )
            break;
        ++count;
        ++wi_iter;
    }
    
    round_robin_val = (round_robin_val+1) % WORKTHREADS_SIZE;
    return wi_iter;
}



/*
#@args:also seeing the detailed information from the libev api document
#@returns:
#@desc:
#called asynchronously when streaming server received the data written by client
*/
void receive_request_cb( struct ev_loop * main_event_loop, struct  ev_io *receive_request_watcher, int revents )
{
    LOG_START("RECEIVE_REQUEST_CB");
    if( EV_ERROR & revents )
    {
        log_module(LOG_INFO,"RECEIVE_REQUEST_CB","ERROR FOR EV_ERROR:%s",LOG_LOCATION);
        ev_io_stop(main_event_loop,receive_request_watcher);
        close(receive_request_watcher->fd);
        delete [] static_cast<char *>(receive_request_watcher->data);
        receive_request_watcher->data = NULL;
        delete receive_request_watcher;
        receive_request_watcher = NULL;
        return;
    }

    int received_bytes;
    //used to store those key-values parsed from http request
    HTTP_REQUEST_INFO req_info;
    char request[BUFFER_SIZE];
    //read the http header and then parse it
    received_bytes = read_http_header(receive_request_watcher->fd,request,BUFFER_SIZE );
    if(  received_bytes <= 0  )
    {     
          if(  received_bytes == 0  )
          {
              log_module(LOG_INFO,"RECEIVE_REQUEST_CB","CLIENT %s DISCONNECTED",
              static_cast<char *>(receive_request_watcher->data));
          }
          else if(  received_bytes == -1 )
          {
              log_module(LOG_INFO,"RECEIVE_REQUEST_CB","READ_HTTP_HEADER:BUFFER SIZE IS TOO SMALL");
          }
          ev_io_stop(main_event_loop,receive_request_watcher);
          close(receive_request_watcher->fd);
          delete [] static_cast<char *>(receive_request_watcher->data);
          receive_request_watcher->data = NULL;
          delete receive_request_watcher;
          receive_request_watcher = NULL;
          return;
    }
    request[received_bytes]='\0';
    log_module(LOG_DEBUG,"RECEIVE_REQUEST_CB","HTTP REQUEST HEADER:%s",request);
    parse_http_request(request, req_info);
    if( req_info.method == "POST" )
    {
         //http "POST" method indicates camera's request
        //check if the channels'amounts overtop the limit before do the camera's request
        if( get_channel_list().size() > CHANNELS_LIMIT )
        {
             log_module(LOG_INFO,"RECEIVE_REQUEST_CB","CAMERA'S REQUEST:OVERTOP THE CHANNNELS' LIMIT,SEND 503 TO CAMERA");
             const char * http_503_service_unavailable = "HTTP/1.1 503 Service Unavailable\r\n\r\n";
             write_specify_size(receive_request_watcher->fd,http_503_service_unavailable, strlen(http_503_service_unavailable));
             close(receive_request_watcher->fd);
        }
        else
        {
             log_module(LOG_INFO,"RECEIVE_REQUEST_CB","RECEIVE THE CAMERA'S REQUEST--CHANNEL:%s",req_info.channel.c_str());
             bool OK = do_camera_request(main_event_loop,receive_request_watcher,req_info);
             if( !OK )
            {
                  const char * http_500_internalerror = "HTTP/1.1  500  Internal Server Error\r\n\r\n";
                  write_specify_size(receive_request_watcher->fd,http_500_internalerror, strlen(http_500_internalerror));
                  close(receive_request_watcher->fd);
                  log_module(LOG_INFO,"RECEIVE_REQUEST_CB","DO_CAMERA_REQUEST FAILED");
            }
            else
            {
                  log_module(LOG_INFO,"RECEIVE_REQUEST_CB","DO_CAMERA_REQUEST SUCCEEDED");
            }
            NOTIFY_DATA notify_data;
            strcpy(notify_data.channel,req_info.channel.c_str());
            notify_data.flag = 0;
            write_specify_size2(NOTIFY_PIPE[1],&notify_data,sizeof(notify_data));
        }    
    }
    else if( req_info.method == "GET" )
    {
        //http "GET" method indicates viewer's request
        log_module(LOG_INFO,"RECEIVE_REQUEST_CB","RECEIVE THE VIEWER'S REQUEST--CHANNEL:%s",req_info.channel.c_str());
        bool OK = do_viewer_request(main_event_loop,receive_request_watcher,req_info);
        if( !OK )
        {
             log_module(LOG_INFO,"RECEIVE_REQUEST_CB","DO_VIEWER_REQUEST FAILED");
        }
        else
        {
             log_module(LOG_INFO,"RECEIVE_REQUEST_CB","DO_VIEWER_REQUEST SUCCEEDED");
        }
    }
    else
    {
        //bad request otherwise
        log_module(LOG_INFO,"RECEIVE_REQUEST_CB","RECEIVE CLIENT'S BAD REQUEST");
        const char * http_400_badrequest="HTTP/1.1 400 Bad Request\r\n\r\n";
        write_specify_size(receive_request_watcher->fd,http_400_badrequest,strlen(http_400_badrequest));
        close(receive_request_watcher->fd);
    }

    ev_io_stop(main_event_loop,receive_request_watcher);
    delete [] static_cast<char *>(receive_request_watcher->data);
    receive_request_watcher->data = NULL;
    delete receive_request_watcher;
    receive_request_watcher = NULL;
    LOG_DONE("RECEIVE_REQUEST_CB");
}



/*
 *@args:
 *@returns:true indicates do camera's request ok,failed the otherwise
 *@desc:as the function name described,do the camera's request
 */
bool do_camera_request( struct ev_loop* main_event_loop, struct ev_io *receive_request_watcher, HTTP_REQUEST_INFO & req_info )
{
        (void)main_event_loop;
        if( camera_already_in(req_info.channel) )
        {
            //if the "channel" exists then just reply http 400,otherwise ok
            log_module(LOG_DEBUG,"RECEIVE_REQUEST_CB","CAMERA Bad Request:Channel %s Already Exist",req_info.channel.c_str());
            const char * http_400_badrequest="HTTP/1.1 400 Bad Request\r\n\r\n";
            write_specify_size(receive_request_watcher->fd,http_400_badrequest,strlen(http_400_badrequest));
            close(receive_request_watcher->fd);
            return false;
        }

        //now choose a work thread through round robin
        workthread_info_iter wi_iter = get_workthread_through_round_robin();
        log_module(LOG_DEBUG,"DO_CAMERA_REQUEST","GET_WORKTHREAD_THROUGH_ROUND_ROBIN--OK--ID:%u",wi_iter->thread_id);
        if( ev_async_pending(wi_iter->async_watcher) == 0 )
        {
             //now allocate EV_ASYNC_DATA for async_read_cb's sake
             EV_ASYNC_DATA *async_data = new EV_ASYNC_DATA;
             if( async_data == NULL )
             {
                    log_module(LOG_INFO,"DO_CAMERA_REQUEST","ALLOCATE EV_ASYNC_DATA FAILED:%s",LOG_LOCATION);
                    return false;
             }
             async_data->client_type = CAMERA;
             async_data->client_fd = receive_request_watcher->fd;
                  /*async_data->channel = new char[req_info.channel.length()+1];
           if( async_data->channel == NULL )
           {
             delete async_data;
             async_data = NULL;
             log_module(LOG_INFO,"DO_CAMERA_REQUEST","ALLOCATE CHANNEL FAILED:%s",LOG_LOCATION);
             return false;
           }
          */ 
             strcpy(async_data->channel,req_info.channel.c_str());

                //now allocate memory for camera_ptr
             CAMERAS_PTR camera_ptr = new_cameras( receive_request_watcher,req_info.channel);
             if( camera_ptr == NULL )
             {
                   log_module(LOG_INFO,"DO_CAMERA_REQUEST","ALLOCATE MEMORY FAILED:CAMERAS_PTR:%s",LOG_LOCATION);
                   //delete async_data;
                   //async_data = NULL;
                   return false;
             }
             wi_iter->camera_info_pool.insert(std::make_pair(req_info.channel,camera_ptr));
             wi_iter->async_watcher->data=(void *)async_data;
             ev_async_send(wi_iter->workthread_loop,wi_iter->async_watcher);
        }     
        return true;
}




/*
 *@args:
 *@returns:true indicates do viewer's request ok,failed the otherwise
 *@desc:as the function name described,do the viewer's request
 */
bool do_viewer_request( struct ev_loop * main_event_loop, struct ev_io * receive_request_watcher, HTTP_REQUEST_INFO & req_info)
{
        //firstly find the channel
        workthread_info_iter wi_iter = workthread_info_pool.begin();
        camera_info_iter ci_iter;
        while( wi_iter != workthread_info_pool.end() )
        {
            ci_iter = wi_iter->camera_info_pool.find(req_info.channel);
            if( ci_iter != wi_iter->camera_info_pool.end())
                break;
            ++wi_iter;
        }
  
        if( wi_iter != workthread_info_pool.end() )
        {
            if(ci_iter->second->viewer_info_pool.size() > VIEWERS_LIMIT )
            {
                const char *http_503_overload="HTTP/1.1 503 Service Unavailable\r\n\r\n";
                write_specify_size(receive_request_watcher->fd, http_503_overload,strlen(http_503_overload));
                close(receive_request_watcher->fd);
                return false;
            }
            log_module(LOG_DEBUG,"DO_VIEWER_REQUEST","FIND THE CHANNEL:%s--ID:%u",ci_iter->first.c_str(),wi_iter->thread_id );
            viewer_info_iter vi_iter = ci_iter->second->viewer_info_pool.find(receive_request_watcher->fd);
            if( vi_iter == ci_iter->second->viewer_info_pool.end() )
            {
                VIEWER_INFO_PTR viewer_info_ptr = new_viewer_info(receive_request_watcher,req_info);
                if( viewer_info_ptr == NULL )
                {
                    log_module(LOG_INFO,"DO_VIEWER_REQUEST","ALLOCATE MEMORY FAILED:VIEWER_INFO_PTR:%s",LOG_LOCATION);
                    return false;
                }
                log_module(LOG_DEBUG,"DO_VIEWER_REQUEST","ADD CLIENT_ID:%d START",receive_request_watcher->fd);
                ci_iter->second->viewer_info_pool.insert(std::make_pair(receive_request_watcher->fd,viewer_info_ptr));
                log_module(LOG_DEBUG,"DO_VIEWER_REQUEST","ADD CLIENT_ID:%d DONE",receive_request_watcher->fd);
            }
            else
            {
                 log_module(LOG_INFO,"DO_VIEWER_REQUEST","SYSTEM ERROR:System Generate The Socket Fd That Was Already In Use");
                 const char * http_500_internalerror = "HTTP/1.1  500  Internal Server Error\r\n\r\n";
                 write_specify_size(receive_request_watcher->fd,http_500_internalerror, strlen(http_500_internalerror));
                 close(receive_request_watcher->fd);
                 return false;
            }

        }
        else if(req_info.src.empty())  //if there is no channel requested in streaming server,
                                                 //either "src" is empty then simply reply http 404 not found
        {
                log_module(LOG_DEBUG,"DO_VIEWER_REQUEST","FIND THE CHANNEL FAILED, EITHER SRC IS EMPTY,JUST SEND 404 TO CLIENT");
                const char * http_404_notfound="HTTP/1.1   404   Not Found\r\n\r\n";
                write_specify_size(receive_request_watcher->fd,http_404_notfound,strlen(http_404_notfound));
                close(receive_request_watcher->fd);
                return false;
        }
        else //imitate the behaviour of viwer,sending the backing-source request
        {
            //if the src is not empty then do the backing-source request
            //it means imitate the request of client

            //also check if the channels' amount overtop the limit
            if( get_channel_list().size() > CHANNELS_LIMIT )
            {
                  log_module(LOG_INFO,"RECEIVE_REQUEST_CB","BACKING RESOURCE'S REQUEST:OVERTOP THE CHANNNELS' LIMIT,SEND 503 TO CAMERA");
                  const char * http_503_service_unavailable = "HTTP/1.1 503 Service Unavailable\r\n\r\n";
                  write_specify_size(receive_request_watcher->fd,http_503_service_unavailable, strlen(http_503_service_unavailable));
                  close(receive_request_watcher->fd);
                  return false;
            }
            
            log_module(LOG_DEBUG,"RECEIVE_REQUEST_CB","SENDING THE BACKING-SOURCE REQUEST:SRC=%s",req_info.src.c_str());
            //now do the backing resource request
            bool OK = do_the_backing_source_request(main_event_loop,receive_request_watcher->fd,req_info);
            if( !OK )
            {
                 log_module(LOG_INFO,"DO_THE_BACKING_SOURCE_REQUEST","ALLOCATE FAILED:%s",LOG_LOCATION);
                 const char * http_500_internalerror = "HTTP/1.1  500  Internal Server Error\r\n\r\n";
                 write_specify_size(receive_request_watcher->fd,http_500_internalerror, strlen(http_500_internalerror));
                 close(receive_request_watcher->fd);
                 log_module(LOG_DEBUG,"RECEIVE_REQUEST_CB","SENDING THE BACKING-SOURCE REQUEST:FAILED");
            }
            else
            {
                  log_module(LOG_DEBUG,"RECEIVE_REQUEST_CB","SENDING THE BACKING-SOURCE REQUEST:SUCCEEDED");
            }
            return OK;
        }
        return true;
}



/*
*@srgs:conn_fd[IN],notify_data[IN]
*@returns:void
*@desc:just send the camera's status to GSLB or those servers 
*/
void do_notify( NOTIFY_SERVER_INFO & notify_server_info, NOTIFY_DATA & notify_data)
{
     
    fd_set read_fd,write_fd;
    FD_SET(notify_server_info.conn_fd, &read_fd);
    FD_SET(notify_server_info.conn_fd, &write_fd);
    int max_fds = notify_server_info.conn_fd +1;
    while( true )
    {
         if( ! FD_ISSET(notify_server_info.conn_fd,&read_fd) && !FD_ISSET(notify_server_info.conn_fd,&write_fd) )
         break;   
         if( -1 == select( max_fds,&read_fd, &write_fd,NULL,NULL) )
         {
               log_module(LOG_INFO,"DO_NOTIFY","UNEXPECTED ERROR OCCURRED:%s",strerror(errno));
               return;
         }
         
         if(FD_ISSET(notify_server_info.conn_fd, &read_fd))
         {
               char reply[1024];
               read_specify_size(notify_server_info.conn_fd,reply,sizeof(reply));
               FD_CLR( notify_server_info.conn_fd, &read_fd);
         }
         
         if(FD_ISSET(notify_server_info.conn_fd, &write_fd))
         {
              std::ostringstream notify_message;
              notify_message<<"POST /daemon/channel_stat.do?channel="<<notify_data.channel<<"&src="<<notify_server_info.IP
              <<":"<<notify_server_info.PORT;
              if ( notify_data.flag == 0 )
              {
                  notify_message<<"&status=0 HTTP/1.1\r\n\r\n";
              }
              else
              {
                  notify_message<<"&status=1 HTTP/1.1\r\n\r\n";
              }
              write_specify_size2(notify_server_info.conn_fd,notify_message.str().c_str(),
                                          notify_message.str().length());
              FD_CLR(notify_server_info.conn_fd, &write_fd);
         }
         
    }
    close(notify_server_info.conn_fd);
}



/*
 *@args:
 *@returns:vector<CHANEEL>
 *@desc:return our streaming server's channel list stored in vector<channel>
*/
std::vector<CHANNEL_POOL> get_channel_list()
{
    std::vector<CHANNEL_POOL> channel_pool;
    workthread_info_iter wi_iter = workthread_info_pool.begin();
    while( wi_iter != workthread_info_pool.end() )
    {
        if( !wi_iter->camera_info_pool.empty() )
        {
            camera_info_iter ci_iter = wi_iter->camera_info_pool.begin();
            while( ci_iter != wi_iter->camera_info_pool.end() )
            {
                CHANNEL_POOL channel;
                channel.channel = ci_iter->first;
                channel.is_camera = true;
                memcpy(channel.IP,ci_iter->second->IP,INET_ADDRSTRLEN);
                channel_pool.push_back(channel);
                ++ci_iter;
            }
        }

        if( !wi_iter->resource_info_pool.empty() )
        {
             backing_resource_iter br_iter = wi_iter->resource_info_pool.begin();
             while( br_iter != wi_iter->resource_info_pool.end() )
             {
                  CHANNEL_POOL channel;
                  channel.channel = br_iter->first;
                  strcpy(channel.IP,br_iter->second.IP.c_str());
                  channel.is_camera = false;
                  channel_pool.push_back(channel);
                  ++br_iter;
             }
        }
        
        ++wi_iter;
    }
    return channel_pool;
}



/*
 *@args:channel[in]
 *@returns:int[out]
 *@desc:obtain the total amount of viewers that related to the camera's channel
 */
size_t  get_channel_viewers( const string & channel )
{
    workthread_info_iter wi_iter = workthread_info_pool.begin();
    while( wi_iter != workthread_info_pool.end() )
    {
         camera_info_iter ci_iter = wi_iter->camera_info_pool.find(channel);
         if( ci_iter != wi_iter->camera_info_pool.end() )
         return ci_iter->second->viewer_info_pool.size();

         backing_resource_iter br_iter = wi_iter->resource_info_pool.find(channel);
         if( br_iter != wi_iter->resource_info_pool.end())
         return br_iter->second.socketfd_pool.size()/2;
         
         ++wi_iter;
    }
    return 0;
}



/*
 *@desc:as the function name described:obtain all viewers online
 */
size_t get_all_online_viewers()
{
    workthread_info_iter wi_iter = workthread_info_pool.begin();
    size_t total_viewers = 0;
    while( wi_iter != workthread_info_pool.end() )
    {
        if( !wi_iter->camera_info_pool.empty() )
        {
            camera_info_iter ci_iter = wi_iter->camera_info_pool.begin();
            while( ci_iter != wi_iter->camera_info_pool.end() )
            {
                total_viewers += ci_iter->second->viewer_info_pool.size();
                ++ci_iter;
            }
        }
        if( !wi_iter->resource_info_pool.empty() )
        {
            backing_resource_iter br_iter = wi_iter->resource_info_pool.begin();
            while( br_iter != wi_iter->resource_info_pool.end() )
            {
                  total_viewers += br_iter->second.socketfd_pool.size() /2;
                  ++br_iter;
            }
        }
        ++wi_iter;
    }
    return total_viewers;
}



/*
#@args: "src" stands for the address of another streaming server,
#            and the "http_request"
#@returns:
#@desc:imitate the behaviour of viewer,sending the backing-source request
#   to the streaming server that the "src" specified
*/
//suppose that the default port is 80
//src=IP:PORT

bool do_the_backing_source_request(struct ev_loop * main_event_loop,ssize_t request_fd, HTTP_REQUEST_INFO & req_info )
{
    (void)main_event_loop;
    string IP;
    int port = 80;
    get_src_info(req_info.src,IP,port);
    log_module(LOG_DEBUG,"DO_THE_BACKING_SOURCE_REQUEST","IP:%s--PORT:%d",IP.c_str(),port);
    int local_fd = tcp_connect(IP.c_str(),port);
    if( local_fd == -1 )
    {
        log_module(LOG_INFO,"DO_THE_BACKING_SOURCE_REQUEST","TCP_CONNECT FAILED:%s",LOG_LOCATION);
        return false;
    }
    //now choose a work thread through round robin

    //log_module(LOG_DEBUG,"DO_THE_BACKING_SOURCE_REQUEST","GET_WORKTHREAD_THROUGH_ROUND_ROBIN--OK--ID:%u",wi_iter->thread_id);
    EV_ASYNC_DATA *async_data = new EV_ASYNC_DATA;
    if(  async_data == NULL )
    {
         log_module(LOG_INFO,"DO_THE_BACKING_SOURCE_REQUEST","ALLOCATE MEMORY FAILED:%s",LOG_LOCATION);
         return false;
    }

    std::ostringstream OSS_imitated_request;
    OSS_imitated_request<<"GET "<<req_info.hostname<<"/"
                           <<"channel="<<req_info.channel
                           <<"&type="<<req_info.type
                           <<"&token="<<req_info.token<<" HTTP/1.1\r\n\r\n";
    string str_imitated_request = OSS_imitated_request.str();
    async_data->data = new char[str_imitated_request.length()];
    if( async_data->data == NULL )
    {
         log_module(LOG_INFO,"DO_THE_BACKING_SOURCE_REQUEST","ALLOCATE MEMORY FAILED:%s",LOG_LOCATION);
         return false;
    }
    
    strcpy(async_data->data, str_imitated_request.c_str());
    log_module(LOG_DEBUG,"DO_THE_BACKING_SOURCE_REQUEST","IMITATED HTTP REQUEST:%s",async_data->data);
    workthread_info_iter wi_iter = workthread_info_pool.begin();
    if( !resource_already_in(req_info.channel,wi_iter))
    {
         log_module(LOG_DEBUG,"DO_THE_BACKING_SOURCE_REQUEST","RESOURCE CHANNEL:%s NOT EXIST",req_info.channel.c_str());
         wi_iter = get_workthread_through_round_robin();
         while( ev_async_pending(wi_iter->async_watcher) != 0 )
         {
              wi_iter = get_workthread_through_round_robin();
         }
         RESOURCE_INFO resource_info;
         resource_info.IP=IP;
         resource_info.socketfd_pool.insert(request_fd);
         resource_info.socketfd_pool.insert(local_fd);
         wi_iter->resource_info_pool.insert(make_pair(req_info.channel,resource_info));
    }
    else
    {
         log_module(LOG_DEBUG,"DO_THE_BACKING_SOURCE_REQUEST","RESOURCE CHANNEL:%s EXIST",req_info.channel.c_str());
         backing_resource_iter br_iter = wi_iter->resource_info_pool.find(req_info.channel);
         br_iter->second.socketfd_pool.insert(request_fd);
         br_iter->second.socketfd_pool.insert(local_fd);
    }

    async_data->client_fd = local_fd;
    async_data->request_fd = request_fd;
    async_data->client_type = BACKER;
    wi_iter->async_watcher->data = (void *) async_data;
    log_module(LOG_DEBUG,"DO_THE_BACKING_RESOURCE_REQUEST","EV_ASYNC_SEND START");
    ev_async_send(wi_iter->workthread_loop,wi_iter->async_watcher);
    log_module(LOG_DEBUG,"DO_THE_BACKING_RESOURCE_REQUEST","EV_ASYNC_SEND DONE");
    return true;
}



/*
#@args: listen_fd:the socket file descriptor for listening
#@returns:
#@desc:
providing one asynchronous event loop for handling clients's connection request
*/
void serve_forever( ssize_t streamer_listen_fd, ssize_t state_server_fd,CONFIG & config )
{
    //if run_as_daemon is true,then make this streaming server run as daemon
    if( config.run_as_daemon )
    {
        daemon(0,0);
    }

    //first set the max open files limit
    struct rlimit rt;
    rt.rlim_max = rt.rlim_cur = MAX_OPEN_FDS;
    if ( setrlimit(RLIMIT_NOFILE, &rt) == -1 ) 
    {
        log_module(LOG_ERROR,"SERVE_FOREVER","SETRLIMIT FAILED:%s",strerror(errno));
    }
    //initialize the logger
    logging_init(config.log_file.c_str(),config.log_level);
    LOG_START("SERVE_FOREVER");
    
    //you have to ignore the PIPE's signal when client close the socket
    struct sigaction sa;
    sa.sa_handler = SIG_IGN;//just ignore the sigpipe
    sa.sa_flags = 0;
    if( sigemptyset(&sa.sa_mask) == -1 ||sigaction(SIGPIPE, &sa, 0) == -1 )
    { 
        close(streamer_listen_fd);
        close(state_server_fd);
        logging_deinit();  
        log_module(LOG_ERROR,"SERVE_FOREVER","Failed To Ignore SIGPIPE Signal");
    }

    //initialization on main event-loop
    struct ev_loop *main_event_loop = EV_DEFAULT;
    struct ev_io *listen_watcher= new ev_io;
    if( main_event_loop == NULL || listen_watcher == NULL )
    {
        close( streamer_listen_fd );
        close( state_server_fd );
        logging_deinit();
        if( main_event_loop != NULL )
        {
            free(main_event_loop);
            main_event_loop = NULL;
        }
        if( listen_watcher != NULL )
        {
            delete listen_watcher;
            listen_watcher = NULL;
        }
        log_module(LOG_ERROR,"SERVE_FOREVER","ALLOCATE MEMORY FAILED:%s",LOG_LOCATION);
    }
    else
    {
           //and now initialize the workthread pool,pool size specified with global "WORKTHREADS_SIZE"   
        init_workthread_info_pool(config,WORKTHREADS_SIZE);
        sdk_set_tcpnodelay( streamer_listen_fd );
        sdk_set_nonblocking( streamer_listen_fd );
        sdk_set_keepalive( streamer_listen_fd );

        sdk_set_tcpnodelay( state_server_fd );
        sdk_set_nonblocking( state_server_fd );
        sdk_set_keepalive( state_server_fd );

        //meanwhile startup the state server
        bool state_ok = startup_state_server( state_server_fd );
        if( state_ok )
        {
             ev_io_init( listen_watcher,accept_cb, streamer_listen_fd, EV_READ );
             ev_io_start( main_event_loop, listen_watcher );
             if( !config.run_as_daemon  )
             {
                  print_welcome( config );
             }
             ev_run( main_event_loop,0 );
        }
        else
        {
             log_module( LOG_INFO,"SERVE_FOREVER","STARTUP_STATE_SERVER:FAILED" );
        }
    }
    
    close( streamer_listen_fd );
    close( state_server_fd );
    logging_deinit();
    ev_io_stop( main_event_loop, listen_watcher );
    delete listen_watcher;
    listen_watcher = NULL;
    free(main_event_loop);
    main_event_loop = NULL;
    free_workthread_info_pool();
    unlink(config.lock_file.c_str());
    LOG_DONE( "SERVE_FOREVER" );
}