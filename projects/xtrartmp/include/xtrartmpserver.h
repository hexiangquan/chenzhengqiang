/*
*@filename:xtrartmp.h
*@author:chenzhengqiang
*@start date:2015/11/26 11:26:16
*@modified date:
*@desc: 
*/



#ifndef _CZQ_XTRARTMPSERVER_H_
#define _CZQ_XTRARTMPSERVER_H_
#include "xtrartmp.h"
#include "serverutil.h"
#include <ev++.h>
#include <vector>
#include <pthread.h>
#include <stdint.h>

namespace czq
{
	//the detailed definition of xtrartmp class
	namespace service
	{
		class XtraRtmpServer
		{
		        public:
				XtraRtmpServer(const ServerUtil::ServerConfig & serverConfig);
				void printHelp();
				void printVersion();
				void registerServer( int listenFd );
				void serveForever();
			public:
				static ssize_t onRtmpInvoke(XtraRtmp::RtmpPacketHeader &rtmpPacketHeader, XtraRtmp::AmfPacket &amfPacket, int connFd);
			private:
				XtraRtmpServer( const XtraRtmpServer &){}
				XtraRtmpServer & operator=(const XtraRtmpServer &){ return *this;}
				static ssize_t onConnect(XtraRtmp::RtmpPacketHeader &rtmpPacketHeader, XtraRtmp::AmfPacket &amfPacket, int connFd);
				static ssize_t onCheckbw(XtraRtmp::RtmpPacketHeader &rtmpPacketHeader, XtraRtmp::AmfPacket &amfPacket, int connFd);
				static ssize_t onCreateStream(XtraRtmp::RtmpPacketHeader &rtmpPacketHeader, XtraRtmp::AmfPacket &amfPacket, int connFd);
				static ssize_t onReleaseStream(XtraRtmp::RtmpPacketHeader &rtmpPacketHeader, XtraRtmp::AmfPacket &amfPacket, int connFd);
				static ssize_t onFCPublish(XtraRtmp::RtmpPacketHeader &rtmpPacketHeader, XtraRtmp::AmfPacket &amfPacket, int connFd);
				static ssize_t onPublish(XtraRtmp::RtmpPacketHeader &rtmpPacketHeader, XtraRtmp::AmfPacket &amfPacket, int connFd);
				static ssize_t onPlay(XtraRtmp::RtmpPacketHeader &rtmpPacketHeader, XtraRtmp::AmfPacket &amfPacket, int connFd);
				static ssize_t onGetStreamLength(XtraRtmp::RtmpPacketHeader &rtmpPacketHeader, XtraRtmp::AmfPacket &amfPacket, int connFd);
				static size_t generateReply(unsigned char *reply, unsigned char *transactionID, const char * parameters[][2], int rows);
				static ssize_t onRtmpReply(XtraRtmp::RtmpPacketHeader & rtmpPacketHeader, unsigned char *transactionID, const char *parameters[][2], int rows, int connFD);
				static ssize_t onRtmpReply(const XtraRtmp::RtmpMessageType & rtmpMessageType, int connFD, size_t size=0);				
			private:
				int listenFd_;
				ServerUtil::ServerConfig serverConfig_;	
		};

		enum {MAX_CHUNK_SIZE=128};
		struct RtmpChunk
		{
			//only support the size max for 128*2
			unsigned char buffer[MAX_CHUNK_SIZE];
			int pos;
		};
		//the global structure audio video frame
		struct AVFrame
		{
			bool isKeyFrame;//true key frame,otherwise non key-frame
			int chunkSize;
			std::vector<RtmpChunk> chunkList;
			int index;
			int lastChunkSize;
		};
		
		//the viewer structure
		typedef struct Viewer
		{
    			struct ev_io * viewerWatcher;
    			//CircularQueue viewerQueue;
		}*ViewerPtr;

		//the channel structure
		typedef struct Channel
		{
			bool receiveFirst;
    			//for flv script tag
    			uint8_t  *scriptTagBuffer;
    			size_t  scriptTagTotalBytes;
    			size_t  scriptTagSentBytes;

    			//for flv's aac sequence header tag
    			uint8_t  *aacSeqHeaderBuffer;
    			size_t  aacSeqHeaderTotalBytes;
    			size_t  aacSeqHeaderSentBytes;

    			//for rtmp flv's avc sequence header tag
    			uint8_t  *avcSeqHeaderBuffer;
    			size_t  avcSeqHeaderTotalBytes;
    			size_t  avcSeqHeaderSentBytes;
    			std::map<int , ViewerPtr> viewers;
		}*ChannelPtr;

		struct LibevAsyncData
		{
    			int type;
    			int sockFd;
			char *channel;	
		};

		//each single thread is related to a WorkthreadInfo structure
		struct WorkthreadInfo
		{
			pthread_t threadID;
    			struct ev_loop *eventLoopEntry;
    			struct ev_async *asyncWatcher;
    			std::map<std::string, ChannelPtr> channelsPool;
		};

		typedef WorkthreadInfo * WorkthreadInfoPtr;
		void * workthreadEntry( void * arg );
		bool startupThreadsPool( size_t totalThreads );
		void freeThreadsPool();
		
		//global libev callback functions in namespace czq;
		void  acceptCallback( struct ev_loop * mainEventLoop, struct ev_io * listenWatcher, int revents );
		void  shakeHandCallback( struct ev_loop * mainEventLoop, struct ev_io * receiveRequestWatcher, int revents );
		void  consultCallback(struct ev_loop * mainEventLoop, struct ev_io * consultWatcher, int revents);
		void  receiveStreamCallback(struct ev_loop * mainEventLoop, struct ev_io * receiveStreamWatcher, int revents);
	};
};
#endif
