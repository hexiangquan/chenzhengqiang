/*
*@filename:xtrartmp.cpp
*@author:chenzhengqiang
*@start date:2015/11/26 11:26:16
*@modified date:
*@desc: 
*/

#include "common.h"
#include "errors.h"
#include "netutil.h"
#include "serverutil.h"
#include "nana.h"
#include "xtrartmpserver.h"
#include<iostream>
#include<stdint.h>



namespace czq
{
	namespace service
	{
		//define the global macro for libev's event clean
		#define DO_LIBEV_CB_CLEAN(M,W) \
	    	ev_io_stop(M, W);\
	    	delete W;\
	    	W= NULL;\
	    	return;

		//the global logger
		Nana *nana=0;

		//the max rtmp package's size
		static const size_t MAX_RTMP_PACKAGE_SIZE=128;
		//the default workthread pool's size
		static  size_t DEFAULT_WORKTHREADS_SIZE = 8;
		//the default channel's length
		static size_t MAX_CHANNEL_LENGTH=32;
		static const int CAMERA = 1;
		static const int VIEWER = 2;
		std::vector<WorkthreadInfoPtr> WorkthreadInfoPool;

		//usefull aliases
		typedef std::vector<WorkthreadInfoPtr>::iterator WorkthreadInfoIterator;
		typedef std::vector<WorkthreadInfoPtr>::iterator & WorkthreadInfoReferenceIterator;
		typedef std::map<std::string, ChannelPtr>::iterator ChannelInfoIterator;
		typedef std::map<std::string, ChannelPtr>::iterator & ChannelInfoReferenceIterator;
		typedef std::map<int , ViewerPtr>::iterator ViewerInfoIterator;
		typedef std::map<int , ViewerPtr>::iterator & ViewerInfoReferenceIterator;
		
		//the amf command
		namespace AmfCommand
	       {
	       	static const char *connect = "connect";
	        	static const char *play = "play";
	        	static const char *play2 = "play2";
	        	static const char *seek = "seek";
	        	static const char *pause = "pause";
	        	static const char *publish = "publish";
	        	static const char *createStream = "createStream";
	        	static const char *releaseStream = "releaseStream";
	        	static const char *deleteStream = "deleteStream";
	        	static const char *FCPublish = "FCPublish";
	        	static const char *receiveAudio = "receiveAudio";
	        	static const char *receiveVideo = "receiveVideo";
			static const char *_checkbw = "_checkbw";
			static const char *getStreamLength = "getStreamLength";
	       };


		   
		//define the global variable for xtrartmp server's sake
		//just simply varify the app field
		static std::string APP="mycs/live";
		
		XtraRtmpServer::XtraRtmpServer( const ServerUtil::ServerConfig & serverConfig )
		 :listenFd_(-1),serverConfig_(serverConfig)
		{
			APP = serverConfig_.server["rtmp-app"];
			if ( ! serverConfig_.server["channel-length-max"].empty() )
			{
				int channelLength = atoi(serverConfig_.server["channel-length-max"].c_str());
				if ( channelLength > 0 && (size_t) channelLength < MAX_CHANNEL_LENGTH )
				{
					MAX_CHANNEL_LENGTH = static_cast<size_t>(channelLength);
				}
			}
		}

		void XtraRtmpServer::printHelp()
		{
			std::cout<<serverConfig_.usage<<std::endl;
			exit(EXIT_SUCCESS);
		}

		void XtraRtmpServer::printVersion()
		{
			std::cout<<serverConfig_.meta["version"]<<std::endl<<std::endl;
			exit(EXIT_SUCCESS);
		}


		void XtraRtmpServer::registerServer( int listenFd )
		{
			listenFd_ = listenFd;
			
		}


		void XtraRtmpServer::serveForever()
		{
			#define ToServeForever __func__
			
			if ( listenFd_ >= 0 )
			{
				if ( serverConfig_.server["daemon"] == "yes" )
	    			{
	        			daemon(0,0);
	    			}

	    			nana = Nana::born(serverConfig_.server["log-file"], atoi(serverConfig_.server["log-level"].c_str()),
	    					   		atoi(serverConfig_.server["flush-time"].c_str()));
	    					   
	    			nana->say(Nana::HAPPY, ToServeForever,  "LISTEN SOCKET FD:%d", listenFd_);
	    
	    			//you have to ignore the PIPE's signal when client close the socket
	    			struct sigaction sa;
	    			sa.sa_handler = SIG_IGN;//just ignore the sigpipe
	    			sa.sa_flags = 0;
	    			if ( sigemptyset( &sa.sa_mask ) == -1 ||sigaction( SIGPIPE, &sa, 0 ) == -1 )
	    			{ 
	        			nana->say(Nana::COMPLAIN, ToServeForever, "FAILED TO IGNORE SIGPIPE SIGNAL");
	    			}
				else
				{
					//startup the threads pool,each thread attach a event loop
	    				//initialization on main event-loop
	    				size_t totalThreads = atoi(serverConfig_.server["threads"].c_str());
					if ( totalThreads <= 0 )
					totalThreads = DEFAULT_WORKTHREADS_SIZE;
					
	    				if ( service::startupThreadsPool(totalThreads) )
	    				{
						struct ev_loop * mainEventLoopEntry = EV_DEFAULT;
	    					struct ev_io * listenWatcher = new ev_io;
						if ( mainEventLoopEntry != 0 && listenWatcher != 0 )
						{
							ev_io_init( listenWatcher, service::acceptCallback, listenFd_, EV_READ );
	             					ev_io_start( mainEventLoopEntry, listenWatcher);
	            					ev_run( mainEventLoopEntry, 0 );
									
							free(mainEventLoopEntry);
							mainEventLoopEntry = 0;
							delete listenWatcher;
							listenWatcher = 0;
							service::freeThreadsPool();
							
						}
						else
						{
							nana->say(Nana::COMPLAIN, ToServeForever, "FAILED TO ALLOCATE MEMORY");	
						}
	    				}
					else
					{
						nana->say(Nana::COMPLAIN, ToServeForever, "STARTUP THE THREADS POOL FAILED");
					}
	    				
	    			}
	    			nana->die();
			}
			else
			{
				nana->say(Nana::COMPLAIN, ToServeForever, "INVALID LISTEN SOCKET FD");
			}
		}


		
		//startup a workthreads pool
		//the workthread pool's size configured in config file
		bool startupThreadsPool( size_t totalThreads )
		{
			#define ToStartupThreadsPool __func__
			bool ret = true;
			pthread_attr_t threadAttr;
			
			for ( size_t index = 0; index < totalThreads; ++index )
			{
				pthread_attr_init(&threadAttr);
    				pthread_attr_setdetachstate( &threadAttr, PTHREAD_CREATE_DETACHED );
				service::WorkthreadInfoPtr workthreadInfoPtr = new service::WorkthreadInfo;
				workthreadInfoPtr->eventLoopEntry = ev_loop_new( EVBACKEND_EPOLL );
				workthreadInfoPtr->asyncWatcher = new ev_async;

				if ( workthreadInfoPtr->eventLoopEntry != 0 && workthreadInfoPtr->asyncWatcher != 0 )
				{
					if ( pthread_create( &workthreadInfoPtr->threadID, &threadAttr, service::workthreadEntry, 
									static_cast<void *>(workthreadInfoPtr) ) == 0 )
					{
						service::WorkthreadInfoPool.push_back(workthreadInfoPtr);
					}
					else
					{
						nana->say(Nana::COMPLAIN, ToStartupThreadsPool, "PTHREAD_CREATE FAILED:%s", strerror(errno));
						ret = false;
						break;
					}
				}
				else
				{
					nana->say(Nana::COMPLAIN, ToStartupThreadsPool, "ALLOCATE MEMORY FAILED");
					ret = false;
					break;
				}
				
			}
			return ret;
		}


		void freeThreadsPool()
		{
			WorkthreadInfoIterator wiIter = WorkthreadInfoPool.begin();
			while ( wiIter != WorkthreadInfoPool.end() )
			{
				if ( *wiIter != 0 )
				{
					if ( (*wiIter)->eventLoopEntry != 0 )
					{
						free((*wiIter)->eventLoopEntry);
						(*wiIter)->eventLoopEntry = 0;
					}

					if ( (*wiIter)->asyncWatcher != 0 )
					{
						delete (*wiIter)->asyncWatcher;
						(*wiIter)->asyncWatcher = 0;
					}

					delete (*wiIter);
					(*wiIter) = 0;
				}
				++wiIter;
			}
		}



		/*
		*returns:return the iterator points to the workthread item
		*desc:get the workthread item through the thread's ID
		*/
		WorkthreadInfoIterator getWorkthreadInfoItem( pthread_t threadID )
		{
			WorkthreadInfoIterator wiIter = WorkthreadInfoPool.begin();
			if ( ! WorkthreadInfoPool.empty() )
			{
				while ( wiIter != WorkthreadInfoPool.end() )
				{
					if ( pthread_equal(threadID, (*wiIter)->threadID ))
					{
						break;
					}
					++wiIter;
				}
			}
			return wiIter;
		}


		
		/*
		*returns:void
		*desc:this callback was called asynchronously 
		*when main event loop send signal to workthread's event loop
		*/
		void asyncReadCallback( struct ev_loop *eventloopEntry, struct ev_async *asyncWatcher, int revents )
		{
			#define ToAsyncReadCallback __func__
			
			if ( EV_ERROR & revents )
			{
				nana->say(Nana::COMPLAIN, ToAsyncReadCallback, "EV_ERROR FOR ASYNC, JUST TRY AGAIN");
				return;
			}

			
			if ( eventloopEntry != 0 && asyncWatcher != 0 )
			{
				
				service::LibevAsyncData * asyncData = static_cast<service::LibevAsyncData *>(asyncWatcher->data);
				if ( asyncData != 0 )
				{
					if ( asyncData->type == CAMERA )
					{
						struct ev_io * receiveStreamWatcher = new struct ev_io;
						if ( receiveStreamWatcher != 0 )
						{	
							receiveStreamWatcher->active = 0;
							receiveStreamWatcher->data = static_cast<void *>(asyncData->channel);   
							ev_io_init(  receiveStreamWatcher, receiveStreamCallback, asyncData->sockFd, EV_READ );
							ev_io_start( eventloopEntry, receiveStreamWatcher);
						}
						else
						{
							close(asyncData->sockFd);
							nana->say(Nana::COMPLAIN, ToAsyncReadCallback, "ALLOCATE MEMORY FAILED");
						}
					}
					else if ( asyncData->type == VIEWER )
					{
						
					}

					delete asyncData;
					asyncData = 0;
				}
				
			}
		}


		
		//the work thread's entry
		void * workthreadEntry( void * arg )
		{
			#define ToWorkthreadEntry __func__
			nana->say(Nana::HAPPY, ToWorkthreadEntry, "WORKTHREAD %u STARTUP", pthread_self());
			WorkthreadInfoPtr workthreadInfoPtr = static_cast<WorkthreadInfoPtr>(arg);
			//there is no need to verify the async watcher and the workthread loop entry
			ev_async_init(workthreadInfoPtr->asyncWatcher, asyncReadCallback);
    			ev_async_start(workthreadInfoPtr->eventLoopEntry, workthreadInfoPtr->asyncWatcher);
    			ev_run(workthreadInfoPtr->eventLoopEntry, 0 );
			free(workthreadInfoPtr->eventLoopEntry);
			workthreadInfoPtr->eventLoopEntry = 0;
			delete workthreadInfoPtr->asyncWatcher;
			workthreadInfoPtr->asyncWatcher = 0;
			return NULL;
		}

		
		
		void acceptCallback( struct ev_loop * mainEventLoopEntry, struct ev_io * listenWatcher, int revents )
		{
			#define ToAcceptCallback __func__
		
			if ( EV_ERROR & revents )
			{
				nana->say(Nana::COMPLAIN, ToAcceptCallback, "LIBEV ERROR FOR EV_ERROR:%d", EV_ERROR);
				DO_LIBEV_CB_CLEAN(mainEventLoopEntry, listenWatcher);
			}
		
			struct sockaddr_in clientAddr;
			socklen_t len = sizeof( struct sockaddr_in );
			int connFd = accept( listenWatcher->fd, (struct sockaddr *)&clientAddr, &len );
			if ( connFd < 0 )
			{
				nana->say(Nana::COMPLAIN, ToAcceptCallback, "ACCEPT ERROR:%s", strerror(errno));   
				return;
			}
					
			struct ev_io * receiveRequestWatcher = new struct ev_io;
			if ( receiveRequestWatcher != 0 )
			{
				receiveRequestWatcher->active = 0;
				receiveRequestWatcher->data = 0;   
				if ( nana->is(Nana::HAPPY) )
				{
					std::string peerInfo = NetUtil::getPeerInfo(connFd);
					nana->say(Nana::HAPPY, ToAcceptCallback, "CLIENT %s CONNECTED SOCK FD IS:%d",
																	 peerInfo.c_str(), connFd);
				}
				
				//register the socket io callback for reading client's request	  
				ev_io_init(  receiveRequestWatcher, shakeHandCallback, connFd, EV_READ );
				ev_io_start( mainEventLoopEntry, receiveRequestWatcher );
			}
			else
			{
				nana->say(Nana::COMPLAIN, ToAcceptCallback, "ALLOCATE MEMORY FAILED:%s", strerror( errno ));
				close(connFd);
			}
		}


		
		void shakeHandCallback( struct ev_loop * mainEventLoopEntry, struct ev_io * receiveRequestWatcher, int revents )
		{
			#define ToShakeHandCallback __func__
			if ( EV_ERROR & revents )
			{
				nana->say(Nana::COMPLAIN, ToShakeHandCallback, "LIBEV ERROR FOR EV_ERROR:%d", EV_ERROR);
				return;
			}

			
			static uint8_t c0c1Buffer[1537]={0};
			static uint8_t c2Buffer[1536]={0};
			static size_t c0c1ReadBytes = 0;
			static size_t c2ReadBytes = 0;
			
			if ( c0c1ReadBytes < sizeof(c0c1Buffer) )
			{
				ssize_t readBytes = NetUtil::readSpecifySize(receiveRequestWatcher->fd, 
								   c0c1Buffer+c0c1ReadBytes, sizeof(c0c1Buffer) - c0c1ReadBytes);
				if ( readBytes > 0 )
				{
					nana->say(Nana::HAPPY, ToShakeHandCallback, "READ C0 C1 BUFFER %d BYTES", readBytes);
					c0c1ReadBytes += readBytes;
					if ( c0c1ReadBytes == sizeof(c0c1Buffer) )
					{
						nana->say(Nana::HAPPY, ToShakeHandCallback, "READ C0 C1 BUFFER DONE,TOTAL READ %u BYTES", c0c1ReadBytes);
						uint8_t s0s1s2Buffer[1+1536+1536]={0};
						s0s1s2Buffer[0] = c0c1Buffer[0];
						uint32_t timestamp = htonl(0);
						uint32_t zero = htonl(0);
						memcpy(s0s1s2Buffer+1, &timestamp, 4);
						memcpy(s0s1s2Buffer+5, &zero, 4);
						ServerUtil::generateSimpleRandomValue(s0s1s2Buffer+9, XtraRtmp::RANDOM_VALUE_SIZE);
						memcpy(s0s1s2Buffer+1537, c0c1Buffer+1, 4);
						memcpy(s0s1s2Buffer+1541, c0c1Buffer+5, 4);
						memcpy(s0s1s2Buffer+1545, c0c1Buffer+9, XtraRtmp::RANDOM_VALUE_SIZE);
						ssize_t ret = NetUtil::writeSpecifySize2(receiveRequestWatcher->fd, s0s1s2Buffer, sizeof(s0s1s2Buffer));
						if ( ret == -1)
						{
							nana->say(Nana::COMPLAIN, ToShakeHandCallback, "WRITE SPECIFY SIZE(2) ERROR OR CLIENT DISCONNECTED ALREADY");
							close(receiveRequestWatcher->fd);
							DO_LIBEV_CB_CLEAN(mainEventLoopEntry, receiveRequestWatcher);
						}
						nana->say(Nana::HAPPY, ToShakeHandCallback, "SEND S0 S1 S2 BUFFER DONE,TOTAL SENT %d BYTES", ret);
					}
					return;
				}
				else
				{
					nana->say(Nana::COMPLAIN, ToShakeHandCallback, "READ SPECIFY SIZE ERROR OR CLIENT DISCONNECTED ALREADY");
					close(receiveRequestWatcher->fd);
					DO_LIBEV_CB_CLEAN(mainEventLoopEntry, receiveRequestWatcher);
				}
			}

			if ( c2ReadBytes < sizeof(c2Buffer) )
			{
				ssize_t readBytes = NetUtil::readSpecifySize(receiveRequestWatcher->fd, 
								   c2Buffer+c2ReadBytes, sizeof(c2Buffer) - c2ReadBytes);
				if ( readBytes > 0 )
				{
					c2ReadBytes += readBytes;
					if ( c2ReadBytes == sizeof(c2Buffer) )
					{
						nana->say(Nana::HAPPY, ToShakeHandCallback, "READ C2 BUFFER DONE,TOTAL READ %u BYTES", c2ReadBytes);
						struct ev_io * consultWatcher = new struct ev_io;
						if ( consultWatcher != 0 )
						{
							consultWatcher->active = 0;
							consultWatcher->data = 0;	
							ev_io_init(  consultWatcher, consultCallback, receiveRequestWatcher->fd, EV_READ );
							ev_io_start( mainEventLoopEntry, consultWatcher);
							
						}
						else
						{
							close(receiveRequestWatcher->fd);
							nana->say(Nana::COMPLAIN, ToShakeHandCallback, "ALLOCATE MEMORY FAILED:%s", strerror( errno ));
						}
						
						DO_LIBEV_CB_CLEAN(mainEventLoopEntry, receiveRequestWatcher);
					}
				}
				else
				{
					nana->say(Nana::COMPLAIN, ToShakeHandCallback, "READ SPECIFY SIZE ERROR OR CLIENT DISCONNECTED ALREADY");
					close(receiveRequestWatcher->fd);
					DO_LIBEV_CB_CLEAN(mainEventLoopEntry, receiveRequestWatcher);
				}
				return;
			}

		}
		

		
		void  consultCallback(struct ev_loop * mainEventLoopEntry, struct ev_io * consultWatcher, int revents)
		{
			#define ToConsultCallback __func__
			if ( EV_ERROR & revents )
			{
				nana->say(Nana::COMPLAIN, ToConsultCallback, "LIBEV ERROR FOR EV_ERROR:%d", EV_ERROR);
				close(consultWatcher->fd);	
				DO_LIBEV_CB_CLEAN(mainEventLoopEntry, consultWatcher);
			}

			static unsigned char consultRequest[1024]={0};
			static unsigned char *pointer = consultRequest;
			size_t readBytes;
			static bool readChunkBasicHeaderDone =false;
			static unsigned char format=0;
			static unsigned char channelID = 0;
			static size_t chunkMsgHeaderSize = 0;
			static size_t readChunkMsgHeaderSize = 0;
			static size_t totalAmfSize = 0;
			static size_t readAmfSize = 0;
			static size_t amfType = 0;
			static bool consultDone = false;
			ssize_t ret = 0;
			
			if ( ! readChunkBasicHeaderDone )
			{
				readBytes = read(consultWatcher->fd, consultRequest, 1);
				if ( readBytes == 1 )
				{
					readChunkBasicHeaderDone = true;
					pointer+=1;
					unsigned char chunkBasicHeader = consultRequest[0];
					nana->say(Nana::HAPPY, ToConsultCallback, "CHUNK BASIC HEADER:%u", consultRequest[0]);
					format = static_cast<unsigned char>((chunkBasicHeader & 0xc0) >> 6);
					channelID = static_cast<unsigned char>(chunkBasicHeader & 0x3f);

					switch( channelID )
					{
						case 0:
							//just read one byte more
							nana->say(Nana::HAPPY, ToConsultCallback, "CHANNEL ID IS 0,JUST READ ONE MORE BYTE");
							NetUtil::readSpecifySize2(consultWatcher->fd, &channelID, 1);
							channelID = static_cast<unsigned char>(channelID +64);
							break;
						case 1: 
							break;
						case 2:
						case 3:
						case 4: 
							nana->say(Nana::HAPPY, ToConsultCallback, "CHANNEL ID IS [234],CONTROL MESSAGE");
							break;	
						default:
							nana->say(Nana::COMPLAIN, ToConsultCallback, "RECEIVE THE COMPLETE CHANNEL ID:%d", channelID);
							break;	
					}

					switch ( format )
					{
						case 0:
							chunkMsgHeaderSize = 11;
							break;
						case 1:
							chunkMsgHeaderSize = 7;
							break;
						case 2:
							chunkMsgHeaderSize = 3;
							break;
						case 3:
							chunkMsgHeaderSize = 0;
							break;
					}

					switch ( channelID )
					{
						case XtraRtmp::CHANNEL_PING_BYTEREAD:
							nana->say(Nana::HAPPY, ToConsultCallback, "WELCOME TO THE CHANNEL OF PING & BYTE READ");
							break;
						case XtraRtmp::CHANNEL_INVOKE:
							nana->say(Nana::HAPPY, ToConsultCallback, "WELCOME TO THE CHANNEL OF INVOKE");
							break;
						case XtraRtmp::CHANNEL_AUDIO_VIDEO:
							nana->say(Nana::HAPPY, ToConsultCallback, "WELCOME TO THE CHANNEL OF AUDIO & VIDEO");
							break;
					}
					nana->say(Nana::HAPPY, ToConsultCallback, "CHUNK MESSAGE HEADER SIZE:%u", chunkMsgHeaderSize);
				}
				else if ( errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR )
				{
					return;
				}
				else 
				{
					close(consultWatcher->fd);
					DO_LIBEV_CB_CLEAN(mainEventLoopEntry, consultWatcher);
				}
				
			}

			
			if ( readChunkMsgHeaderSize != chunkMsgHeaderSize )
			{
				readBytes = NetUtil::readSpecifySize(consultWatcher->fd, pointer+readChunkMsgHeaderSize, 
												chunkMsgHeaderSize-readChunkMsgHeaderSize);
				if ( readBytes > 0 )
				{
					readChunkMsgHeaderSize += readBytes;
					if ( readChunkMsgHeaderSize == chunkMsgHeaderSize )
					{
						nana->say(Nana::HAPPY, ToConsultCallback, "READ RTMP CHUNK MESSAGE HEADER DONE");
						if ( chunkMsgHeaderSize >= 7 )
						{
							totalAmfSize = static_cast<size_t>(pointer[3]*65536+pointer[4]*256+pointer[5]);
							amfType = pointer[6];
							nana->say(Nana::HAPPY, ToConsultCallback, "AMF PAYLOAD TOTAL SIZE:%u", totalAmfSize);
						}
						else
						{
							nana->say(Nana::COMPLAIN, ToConsultCallback, "INLVALID RTMP REQUEST FORMAT");
							close(consultWatcher->fd);
							DO_LIBEV_CB_CLEAN(mainEventLoopEntry, consultWatcher);
						}
						pointer+=chunkMsgHeaderSize;
					}
					
					return;
				}
				else
				{
					nana->say(Nana::PEACE, ToConsultCallback, "CLIENT DISCONNECTED ALREADY");
					close(consultWatcher->fd);
					DO_LIBEV_CB_CLEAN(mainEventLoopEntry,consultWatcher);
				}
			}

			if ( readAmfSize != totalAmfSize )
			{
				readBytes = NetUtil::readSpecifySize(consultWatcher->fd, pointer+readAmfSize, 
											      totalAmfSize - readAmfSize);
				if ( readBytes > 0 )
				{
					readAmfSize += readBytes;
					if ( readAmfSize == totalAmfSize )
					{
						nana->say(Nana::HAPPY, ToConsultCallback, "READ RTMP AMF PAYLOAD DONE");
						pointer += totalAmfSize-1;
						if ( amfType == XtraRtmp::MESSAGE_INVOKE && format == 0 && channelID == 3)
						{
							while ( *pointer != 9 && *pointer != 5)
							{
								pointer +=1;
								NetUtil::readSpecifySize2(consultWatcher->fd, pointer, 1);
							}
						}

						
						nana->say( Nana::HAPPY, ToConsultCallback, "READ THE TOTAL RTMP PACKET %d BYTES", 
															1+ chunkMsgHeaderSize +totalAmfSize);
						XtraRtmp::RtmpPacket rtmpPacket;
						 ret = XtraRtmp::parseRtmpPacket(consultRequest, 
													1+ chunkMsgHeaderSize +totalAmfSize, 
													rtmpPacket);
						if ( ret == MISS_OK )
						{
							XtraRtmp::AmfPacket amfPacket;
							XtraRtmp::parseRtmpAMF0(rtmpPacket.rtmpPacketPayload, 
												     rtmpPacket.rtmpPacketHeader.AMFSize, 
											 	     amfPacket, 
											 	     (XtraRtmp::RtmpMessageType)rtmpPacket.rtmpPacketHeader.AMFType);
							
							XtraRtmp::rtmpAMF0Dump(amfPacket, nana);
							XtraRtmp::rtmpMessageDump((XtraRtmp::RtmpMessageType)rtmpPacket.rtmpPacketHeader.AMFType, nana);

							switch (rtmpPacket.rtmpPacketHeader.AMFType)
							{
								case XtraRtmp::MESSAGE_CHANGE_CHUNK_SIZE:
								break;
								case XtraRtmp::MESSAGE_DROP_CHUNK:
									break;
								case XtraRtmp::MESSAGE_SEND_BOTH_READ:
									break;
								case XtraRtmp::MESSAGE_USER_CONTROL:
									nana->say(Nana::HAPPY, ToConsultCallback, "RECEIVE THE MESSAGE OF USER CONTROL");
									break;
								case XtraRtmp::MESSAGE_WINDOW_ACKNOWLEDGEMENT_SIZE:
									nana->say(Nana::HAPPY, ToConsultCallback, "RECEIVE THE MESSAGE OF WINDOW ACKNOWLEDGEMENT SIZE");
									break;
								case XtraRtmp::MESSAGE_SET_PEER_BANDWIDTH:
									break;
								case XtraRtmp::MESSAGE_AUDIO:
									break;
								case XtraRtmp::MESSAGE_VIDEO:
									break;
								case XtraRtmp::MESSAGE_AMF0_DATA:
									break;
								case XtraRtmp::MESSAGE_SUBTYPE:
									break;
								case XtraRtmp::MESSAGE_INVOKE:
									ret = XtraRtmpServer::onRtmpInvoke(rtmpPacket.rtmpPacketHeader, amfPacket, consultWatcher->fd);
									if ( ret >= 0  )
									{
										if ( ret >= 1 )
										consultDone = true;
									}
									else
									{
										nana->say(Nana::PEACE, ToConsultCallback, "ON RTMP INVOKE FAILED RELATED TO COMMAND:%s", 
																			   amfPacket.command.c_str());
										close(consultWatcher->fd);
										DO_LIBEV_CB_CLEAN(mainEventLoopEntry, consultWatcher);
									}
									break;
								default:
									break;
							}
						}
						else
						{
							nana->say(Nana::PEACE, ToConsultCallback, "INVALID RTMP REQUEST FORMAT");
							close(consultWatcher->fd);
							DO_LIBEV_CB_CLEAN(mainEventLoopEntry,consultWatcher);
						}

						readChunkBasicHeaderDone = false;
						chunkMsgHeaderSize = 0;
						readChunkMsgHeaderSize = 0;
						totalAmfSize = 0;
						readAmfSize = 0;
						bzero(consultRequest, sizeof(consultRequest));
						pointer = consultRequest;
					}
				}
				else
				{
					nana->say(Nana::PEACE, ToConsultCallback, "CLIENT DISCONNECTED ALREADY");
					close( consultWatcher->fd );
					DO_LIBEV_CB_CLEAN(mainEventLoopEntry,consultWatcher);
				}
			}
			
			if ( consultDone )
			{
				nana->say(Nana::HAPPY, ToConsultCallback, "RTMP CONSULT DONE,STARTING RECEIVE THE RTMP STREAM");
				DO_LIBEV_CB_CLEAN(mainEventLoopEntry, consultWatcher);
			}
		}



		void removeChannel( struct ev_loop * eventLoopEntry, struct  ev_io *ioWatcher, 
                                    WorkthreadInfoReferenceIterator wirIter, ChannelInfoReferenceIterator cirIter )

		{
			if ( eventLoopEntry != 0 && ioWatcher != 0 )
        		{
            			if ( !cirIter->second->viewers.empty() )
        			{
            				ViewerInfoIterator viIter = cirIter->second->viewers.begin();
            				while ( viIter != cirIter->second->viewers.end() )
            				{
               				 if ( viIter->second->viewerWatcher != 0 )
                				{	
							close( viIter->second->viewerWatcher->fd );
							if ( viIter->second->viewerWatcher->active == 1 )		
                        				ev_io_stop( eventLoopEntry, viIter->second->viewerWatcher );
							delete viIter->second->viewerWatcher;
							viIter->second->viewerWatcher = 0;
                				}
                				//deleteViewerQueue( viIter->second->viewerQueue );
                				delete viIter->second;
                				viIter->second = 0;
                				++viIter;
            				}
							
            				cirIter->second->viewers.clear();
        			}


        			if ( ioWatcher->fd > 0 )
        			{
            				close( ioWatcher->fd );
        			}
        
        			ev_io_stop(eventLoopEntry, ioWatcher);
        			delete ioWatcher;
        			ioWatcher = 0;

       			 if( cirIter->second->scriptTagBuffer != 0 )
        			delete [] cirIter->second->scriptTagBuffer;
        			cirIter->second->scriptTagBuffer = 0;
					
        			if( cirIter->second->avcSeqHeaderBuffer != 0 )
        			delete [] cirIter->second->avcSeqHeaderBuffer;
        			cirIter->second->avcSeqHeaderBuffer = 0;
					
        			if( cirIter->second->aacSeqHeaderBuffer != 0 )
        			delete [] cirIter->second->aacSeqHeaderBuffer;
        			cirIter->second->aacSeqHeaderBuffer = 0;

				delete cirIter->second;
        			cirIter->second = 0;

				(*wirIter)->channelsPool.erase( cirIter );	
        		}
		}


		
		void  receiveStreamCallback(struct ev_loop * eventLoopEntry, struct ev_io * receiveStreamWatcher, int revents)
		{
			#define ToReceiveStreamCallback __func__
			#define DELETE_CHANNEL() \
			removeChannel(eventLoopEntry, receiveStreamWatcher, wiIter, ciIter);\
			delete [] channel;\
			nana->say(Nana::PEACE, ToReceiveStreamCallback, "CLIENT DISCONNECTED OR UNKNOWN ERROR OCCURRED");\
			return;
	
			nana->say(Nana::HAPPY, ToReceiveStreamCallback, "++++++++++++++++++++START++++++++++++++++++++");
			if ( EV_ERROR & revents )
			{
				nana->say(Nana::COMPLAIN, ToReceiveStreamCallback, "LIBEV ERROR FOR EV_ERROR:%d", EV_ERROR);
				close(receiveStreamWatcher->fd);
				nana->say(Nana::HAPPY, ToReceiveStreamCallback, "++++++++++++++++++++DONE++++++++++++++++++++");
				DO_LIBEV_CB_CLEAN(eventLoopEntry, receiveStreamWatcher);
			}
	
			static bool alreadyReadAvcSequenceHeader = false;
			static bool alreadyReadAacSequenceHeader = false;
			static bool readChunkBasicHeaderDone = false;
			static bool AMFDataSizeGT128 = false;

			static unsigned char format = 0;
			static size_t leftReadSize = 0;
			static size_t previousAMFDataSize = 0;
			static unsigned char chunkBasicHeader = 0;
			static size_t chunkMsgHeaderSize = 0;
			static size_t readChunkMsgHeaderSize = 0;
			static unsigned char chunkMsgHeaderBuffer[11]={0};
			static size_t amfPayloadSize = 0;
			static size_t readAmfPayloadSize = 0;
			static unsigned char amfPayloadBuffer[512];
			static size_t totalReadBytes = 0;
			static size_t segmentSize = 0;
			static size_t readSegmentSize = 0;
			size_t readBytes = 0;
			
			WorkthreadInfoIterator wiIter = getWorkthreadInfoItem(pthread_self());
			char *channel = static_cast<char *>(receiveStreamWatcher->data);
			ChannelInfoIterator ciIter = (*wiIter)->channelsPool.find(channel);

			//read the first one byte of rtmp packet
			if ( !readChunkBasicHeaderDone )
			{
				readBytes=NetUtil::readSpecifySize(receiveStreamWatcher->fd, &chunkBasicHeader, 1);
				if ( 1 == readBytes )
				{
					readChunkBasicHeaderDone = true;
					nana->say(Nana::HAPPY, ToReceiveStreamCallback, "CHUNK BASIC HEADER:%x", chunkBasicHeader);
					format = static_cast<unsigned char>((chunkBasicHeader & 0xc0) >> 6);
					unsigned char channelID = static_cast<unsigned char>(chunkBasicHeader & 0x3f);
					unsigned char channelID2[2];

					switch( channelID )
					{
						case 0:
							//just read one byte more
							nana->say(Nana::HAPPY, ToReceiveStreamCallback, "CHANNEL ID IS 0,JUST READ ONE MORE BYTE");
							NetUtil::readSpecifySize(receiveStreamWatcher->fd, &channelID, 1);
							channelID = static_cast<unsigned char>(channelID + 64);
							break;
						case 1: 
							nana->say(Nana::HAPPY, ToReceiveStreamCallback, "CHANNEL ID IS 1,JUST READ TWO MORE BYTES");
							NetUtil::readSpecifySize2(receiveStreamWatcher->fd, channelID2, 2);
							//channelID = channelID2[0]*256+ channelID2[1]*64;
							break;
						case 2:
						case 3:
						case 4: 
							nana->say(Nana::HAPPY, ToReceiveStreamCallback, "CHANNEL ID IS [234],CONTROL MESSAGE");
							break;	
						default:
							nana->say(Nana::COMPLAIN, ToReceiveStreamCallback, "RECEIVE THE COMPLETE CHANNEL ID:%d", channelID);
							break;	
					}	

	
					nana->say(Nana::HAPPY, ToReceiveStreamCallback, "CHUNK BASIC HEADER PARSED.FORMAT:%x CHANNEL ID:%x", format, channelID);
					//according to the format, you can compute the chunk message header size
					switch ( format )
					{
						case 0:
							chunkMsgHeaderSize = 11;
							break;
						case 1:
							chunkMsgHeaderSize = 7;
							break;
						case 2:
							chunkMsgHeaderSize = 3;
							break;
						case 3:
							chunkMsgHeaderSize = 0;
							break;
					}

					nana->say(Nana::HAPPY, ToReceiveStreamCallback, "CHUNK MESSAGE HEADER SIZE:%u", chunkMsgHeaderSize);
					switch (channelID)
					{
						case XtraRtmp::CHANNEL_PING_BYTEREAD:
							nana->say(Nana::HAPPY, ToReceiveStreamCallback, "WELCOME TO THE CHANNEL OF PING & BYTE READ");
							break;
						case XtraRtmp::CHANNEL_INVOKE:
							nana->say(Nana::HAPPY, ToReceiveStreamCallback, "WELCOME TO THE CHANNEL OF INVOKE");
							break;
						case XtraRtmp::CHANNEL_AUDIO_VIDEO:
							nana->say(Nana::HAPPY, ToReceiveStreamCallback, "WELCOME TO THE CHANNEL OF AUDIO & VIDEO");
							break;
					}
				}
				else
				{
					DELETE_CHANNEL();
				}
			}


			//read the chunk message header size 
			if ( ( chunkMsgHeaderSize > 0 ) && ( readChunkMsgHeaderSize < chunkMsgHeaderSize ) )
			{
				
				readBytes = NetUtil::readSpecifySize(receiveStreamWatcher->fd, 
											      chunkMsgHeaderBuffer + readChunkMsgHeaderSize,
											      chunkMsgHeaderSize - readChunkMsgHeaderSize);
				if ( readBytes > 0 )

				
				{
					readChunkMsgHeaderSize += readBytes;
					if ( readChunkMsgHeaderSize == chunkMsgHeaderSize )
					{
						nana->say(Nana::HAPPY, ToReceiveStreamCallback, "READ CHUNK MESSAGE HEADER DONE,TOTAL READ %u BYTES",chunkMsgHeaderSize);
						int timestamp = chunkMsgHeaderBuffer[0]*65536+chunkMsgHeaderBuffer[1]*256+chunkMsgHeaderBuffer[2];
				 		amfPayloadSize = static_cast<size_t>(chunkMsgHeaderBuffer[3]*65536+chunkMsgHeaderBuffer[4]*256+chunkMsgHeaderBuffer[5]);
						nana->say(Nana::HAPPY, ToReceiveStreamCallback, "TIMESTAMP:%d AMF SIZE:%d HEX:%x %x %x DECIMAL:%d %d %d", 
																	timestamp, amfPayloadSize,  chunkMsgHeaderBuffer[3], chunkMsgHeaderBuffer[4], chunkMsgHeaderBuffer[5],
																	chunkMsgHeaderBuffer[3],chunkMsgHeaderBuffer[4],chunkMsgHeaderBuffer[5]);
						unsigned char msgType = chunkMsgHeaderBuffer[6];
						if ( msgType ==  XtraRtmp::MESSAGE_VIDEO || msgType == XtraRtmp::MESSAGE_AUDIO )
						{
							if ( amfPayloadSize > MAX_RTMP_PACKAGE_SIZE )
							{
								previousAMFDataSize = amfPayloadSize;
								leftReadSize = amfPayloadSize - MAX_RTMP_PACKAGE_SIZE;
								amfPayloadSize = MAX_RTMP_PACKAGE_SIZE;
								totalReadBytes += MAX_RTMP_PACKAGE_SIZE;
								AMFDataSizeGT128 = true;
							}
							
							if ( msgType ==  XtraRtmp::MESSAGE_VIDEO )
							{
								if (!alreadyReadAvcSequenceHeader)
								{
									alreadyReadAvcSequenceHeader = true;
									nana->say(Nana::HAPPY, ToReceiveStreamCallback, "RECEIVE THE AVC SEQUENCE HEADER");
								}
								else if ( format == 0 )
								{
									nana->say(Nana::HAPPY, ToReceiveStreamCallback, "RECEIVE THE KEY FRAME & THE FRAME'S SIZE IS %d", amfPayloadSize);
								}
								else
								{
									nana->say(Nana::HAPPY, ToReceiveStreamCallback, "RECEIVE THE INTER FRAME & THE FRAME'S SIZE IS %d", amfPayloadSize);
								}
							}
							else
							{
								if ( !alreadyReadAacSequenceHeader )
								{
									alreadyReadAacSequenceHeader = true;
									nana->say(Nana::HAPPY, ToReceiveStreamCallback, "RECEIVE THE AAC SEQUENCE HEADER");
								}
								else
								{
									nana->say(Nana::HAPPY, ToReceiveStreamCallback, "RECEIVE THE RTMP AUDIO DATA");
								}
							}
					
						}
						else
						{
							nana->say(Nana::HAPPY, ToReceiveStreamCallback, "RECEIVE THE UNKNOWN RTMP  DATA");
						}
					}
					else
					{
						return;
					}
				}
				else
				{
					DELETE_CHANNEL();
				}
				
			}

			//and if the chunk message header size is greater than 7,you have to calculate the amf payload's size	
			if ( chunkMsgHeaderSize >= 7 && readAmfPayloadSize < amfPayloadSize )
			{
				readBytes = NetUtil::readSpecifySize(receiveStreamWatcher->fd, 
												amfPayloadBuffer+readAmfPayloadSize,
												amfPayloadSize - readAmfPayloadSize);
				if ( readBytes > 0 )
				{
					readAmfPayloadSize += readBytes;
					if ( readAmfPayloadSize == amfPayloadSize )
					{
							unsigned char msgType = chunkMsgHeaderBuffer[6];
							if ( msgType == XtraRtmp::MESSAGE_AMF0_DATA )
							{
								unsigned char trailer[3];
								readBytes=NetUtil::readSpecifySize2(receiveStreamWatcher->fd, trailer, 2);
								if ( readBytes == 0 )
								{
									DELETE_CHANNEL();
								}
								
								if ( trailer[0] == 0 && trailer[1] == 9 )
								{
									;
								}
								else
								{
									readBytes = NetUtil::readSpecifySize(receiveStreamWatcher->fd, trailer+2, 1);
									if ( readBytes == 0 )
									{
										DELETE_CHANNEL();
									}
								}
								nana->say(Nana::HAPPY, ToReceiveStreamCallback, "READ RTMP SET DATA FRAME MESSAGE DONE");
							}
							
							nana->say(Nana::HAPPY, ToReceiveStreamCallback, "READ AMF PAYLOAD DONE:%d BYTES", amfPayloadSize);
							nana->say(Nana::HAPPY, ToReceiveStreamCallback, "++++++++++++++++++++DONE++++++++++++++++++++");
							readChunkBasicHeaderDone = false;
							readChunkMsgHeaderSize = 0;
							readAmfPayloadSize = 0;
							return;
					}
					return;
				}
				else
				{
					DELETE_CHANNEL();
				}
			}
			else if ( format == 2 )
			{
				nana->say(Nana::HAPPY, ToReceiveStreamCallback, "THIS AMF DATA SIZE IS EQUAL TO PREVIOUS DATA SIZE:%u", previousAMFDataSize);
				leftReadSize = previousAMFDataSize;
				AMFDataSizeGT128 = true;
				totalReadBytes = 0;
			}

			if ( segmentSize == 0 )
			{
				if ( leftReadSize >= MAX_RTMP_PACKAGE_SIZE )
				{
					segmentSize = MAX_RTMP_PACKAGE_SIZE;
					leftReadSize -=MAX_RTMP_PACKAGE_SIZE;
				}
				else
				{
					segmentSize = leftReadSize;
					leftReadSize = 0;
				}
	
				if ( ! AMFDataSizeGT128 )
				{
					segmentSize = MAX_RTMP_PACKAGE_SIZE;
				}
			}

			
			
			if ( segmentSize > 0 && readSegmentSize < segmentSize )
			{
				readBytes = NetUtil::readSpecifySize( receiveStreamWatcher->fd, 
												amfPayloadBuffer+readSegmentSize,
												segmentSize-readSegmentSize);
				if ( readBytes > 0 )
				{
					readSegmentSize += readBytes;
					if ( readSegmentSize == segmentSize )
					{
						totalReadBytes += segmentSize;
						nana->say(Nana::HAPPY, ToReceiveStreamCallback, "READ RTMP SEGMENT SIZE %u DONE, TOTAL READ BYTES:%u",
														segmentSize, totalReadBytes);
						segmentSize = 0;
						readSegmentSize = 0;
						readChunkBasicHeaderDone = false;
						
						if ( leftReadSize == 0 )
						{
							AMFDataSizeGT128 = false;
						}
			
						if (leftReadSize == 0 || !AMFDataSizeGT128 )
						{
							totalReadBytes = 0;
							segmentSize = 0;
							readSegmentSize = 0;
							readChunkBasicHeaderDone = false;
							chunkMsgHeaderSize = 0;
							readChunkMsgHeaderSize = 0;
							amfPayloadSize = 0;
							readAmfPayloadSize = 0;
						}
					}
					else
					{
						return;
					}
				}
				else
				{
					DELETE_CHANNEL();
				}
			}
			
			nana->say(Nana::HAPPY, ToReceiveStreamCallback, "++++++++++++++++++++DONE++++++++++++++++++++");
	
		}


		
		ssize_t XtraRtmpServer::onRtmpInvoke(XtraRtmp::RtmpPacketHeader & rtmpPacketHeader, XtraRtmp::AmfPacket & amfPacket, int connFd)
		{
			#define onRtmpInvoke __func__
			ssize_t ret = 0;
			std::string peerInfo = NetUtil::getPeerInfo(connFd);
			if (amfPacket.command == AmfCommand::connect)
			{
				nana->say(Nana::HAPPY, onRtmpInvoke, "RECEIVE A AMF connect COMMAND RELATED TO ID:%s", peerInfo.c_str());
				if ( amfPacket.parameters["app"] == APP )
				{
					ret = onConnect(rtmpPacketHeader, amfPacket, connFd);
				}
				else
				{
					ret = -1;
				}
			}
			else if (amfPacket.command == AmfCommand::createStream)
			{
				nana->say(Nana::HAPPY, onRtmpInvoke, "RECEIVE THE AMF createStream COMMAND RELATED TO ID:%s", peerInfo.c_str());
				ret = onCreateStream(rtmpPacketHeader, amfPacket, connFd);
			}
			else if (amfPacket.command == AmfCommand::releaseStream)
			{
				nana->say(Nana::HAPPY, onRtmpInvoke, "RECEIVE THE AMF releaseStream COMMAND RELATED TO ID:%s", peerInfo.c_str());
				ret = onReleaseStream(rtmpPacketHeader, amfPacket, connFd);			
			}
			else if (amfPacket.command == AmfCommand::deleteStream)
			{
				
			}
			else if (amfPacket.command == AmfCommand::play)
			{
				nana->say(Nana::HAPPY, onRtmpInvoke, "RECEIVE THE AMF play COMMAND");
				ret = onPlay(rtmpPacketHeader, amfPacket, connFd);
			}
			else if (amfPacket.command == AmfCommand::play2)
			{
				
			}
			else if (amfPacket.command == AmfCommand::FCPublish)
			{
				nana->say(Nana::HAPPY, onRtmpInvoke, "RECEIVE THE AMF FCPublish COMMAND");
				ret = onFCPublish(rtmpPacketHeader, amfPacket, connFd);
			}
			else if (amfPacket.command == AmfCommand::receiveAudio)
			{
				
			}
			else if (amfPacket.command == AmfCommand::receiveVideo)
			{
				
			}
			else if (amfPacket.command == AmfCommand::publish)
			{
				nana->say(Nana::HAPPY, onRtmpInvoke, "RECEIVE THE AMF publish COMMAND");
				if ( amfPacket.streamName.length() <= MAX_CHANNEL_LENGTH )
				{
					ret = onPublish(rtmpPacketHeader, amfPacket, connFd);
				}
				else
				{
					ret = -1;
				}
			}
			else if (amfPacket.command == AmfCommand::seek)
			{
				
			}
			else if (amfPacket.command == AmfCommand::pause)
			{
				
			}
			else if (amfPacket.command == AmfCommand::_checkbw)
			{
				nana->say(Nana::HAPPY, onRtmpInvoke, "RECEIVE THE _checkbw COMMAND");
				onCheckbw(rtmpPacketHeader, amfPacket, connFd);
			}
			else if (amfPacket.command == AmfCommand::getStreamLength)
			{
				nana->say(Nana::HAPPY, onRtmpInvoke, "RECEIVE THE getStreamLength COMMAND");
				onGetStreamLength(rtmpPacketHeader, amfPacket, connFd);
			}
			return ret;
		}
	
	
		ssize_t XtraRtmpServer::onPlay(XtraRtmp::RtmpPacketHeader &rtmpPacketHeader, XtraRtmp::AmfPacket &amfPacket, int connFd)
		{
			
			#define ToOnPlay __func__
			
			const char * OnPlay1[4][2]={ 
										{"onStatus",0},
										{0, 0},//null data
										{"level", "status"},
										{"code","NetStream.Play.Reset"}
									};
			
			
			const char * OnPlay2[4][2]={ 
											{"onStatus",0},
											{0, 0},
											{"level", "status"},
											{"code","NetStream.Play.Start"}
									   };
			
			const char * OnPlay3[1][2]={
									   {"|RtmpSampleAccess", 0},
									};

			
			rtmpPacketHeader.flag = 0;
			//the audio video channel
			rtmpPacketHeader.chunkStreamID = 4;
			rtmpPacketHeader.size = 12;
			ssize_t ret = 0;
			ret = onRtmpReply(rtmpPacketHeader, amfPacket.transactionID+1, OnPlay1, 4, connFd);
			ret += onRtmpReply(rtmpPacketHeader, amfPacket.transactionID+1, OnPlay2, 4, connFd);
			ret += onRtmpReply(rtmpPacketHeader, amfPacket.transactionID+1, OnPlay3, 1, connFd);
			if ( ret < 0 )
			{
				ret = -1;
			}
			return ret;
		}
	
			
		ssize_t XtraRtmpServer::onGetStreamLength(XtraRtmp::RtmpPacketHeader &rtmpPacketHeader, XtraRtmp::AmfPacket &amfPacket, int connFd)
		{
			#define ToOnGetStreamLength __func__
			
			if (rtmpPacketHeader.size  < 8)
			return -1;
			const char * GetStreamParameters[2][2]={ 
													{"_result",0},
													{0,0},
												 };
			ssize_t ret = 0;
			ret += onRtmpReply(rtmpPacketHeader, amfPacket.transactionID+1, GetStreamParameters, 2, connFd);
			nana->say(Nana::HAPPY, ToOnGetStreamLength, "REPLY RTMP AMF COMMAND DONE:getStreamLength");
			ret += onRtmpReply(XtraRtmp::MESSAGE_USER_CONTROL, connFd);
			nana->say(Nana::HAPPY, ToOnGetStreamLength, "REPLY USER CONTROL MESSAGE DONE");
			if ( ret < 0 )
			{
				ret = -1;
			}
			return ret;
		}
	
		
		ssize_t XtraRtmpServer::onConnect(XtraRtmp::RtmpPacketHeader &rtmpPacketHeader, XtraRtmp::AmfPacket &amfPacket, int connFd)
		{
			#define ToOnConnect __func__
		
			if ( rtmpPacketHeader.size  < 8 )
			return -1;	
			const char * ConnectParameters[4][2]={ 
										{"_result",0},
										{"level", "status"},
										{"code", "NetConnection.Connect.Success"},
										{"description", "Connection succeeded."}
									};
	
			const char number[8]={(const char)0x40,(const char)0xc0,(const char)0x0,(const char)0x0,(const char)0x0,
								 (const char)0x0,(const char)0x0,(const char)0x00};
			
			const char *OnBWDone[3][2]={
										{"onBWDone",0},
										{0,0},
										{0,(const char *)number}
									};
		
			size_t windowAcknowledgementSize = 2500000;
			size_t peerBandwidth = 2500000;

			ssize_t ret = 0;
			ret += onRtmpReply(XtraRtmp::MESSAGE_WINDOW_ACKNOWLEDGEMENT_SIZE, connFd, windowAcknowledgementSize);
			nana->say(Nana::HAPPY, ToOnConnect, "REPLY WINDOW ACKNOWLEDGEMENSIZE MESSAGE DONE:%u", windowAcknowledgementSize);
			ret +=onRtmpReply(XtraRtmp::MESSAGE_SET_PEER_BANDWIDTH, connFd, peerBandwidth);
			nana->say(Nana::HAPPY, ToOnConnect, "REPLY SET PEER BANDWIDTH MESSAGE DONE:%u", peerBandwidth);
			ret +=onRtmpReply(XtraRtmp::MESSAGE_USER_CONTROL, connFd);
			nana->say(Nana::HAPPY, ToOnConnect, "REPLY USER CONTROL MESSAGE DONE");
			ret +=onRtmpReply(rtmpPacketHeader, amfPacket.transactionID+1, ConnectParameters, 4, connFd);
			nana->say(Nana::HAPPY, ToOnConnect, "REPLY RTMP AMF COMMAND DONE:{_result,{code:NetConnection.Connect.Success}}");
			rtmpPacketHeader.flag = 1;
			rtmpPacketHeader.size = 8;
			ret +=onRtmpReply(rtmpPacketHeader, 0, OnBWDone, 3, connFd);
			nana->say(Nana::HAPPY, ToOnConnect, "REPLY RTMP AMF COMMAND DONE:onBWDone");
			
			if ( ret < 0 )
			{
				ret = -1;
			}
			return ret;
		}
	
	
		ssize_t XtraRtmpServer::onCreateStream(XtraRtmp::RtmpPacketHeader &rtmpPacketHeader, XtraRtmp::AmfPacket &amfPacket, int connFd)
		{
			#define ToOnCreateStream __func__
				
			if (rtmpPacketHeader.size  < 8)
			return -1;
			const char * CreateStreamParameters[2][2]={ 
														{"_result",0},
														{0, 0}
													};
			
			rtmpPacketHeader.flag = 1;
			rtmpPacketHeader.chunkStreamID = 3;
			rtmpPacketHeader.size = 8;
			return onRtmpReply(rtmpPacketHeader, amfPacket.transactionID+1, CreateStreamParameters, 2, connFd);
									  
		}
	
	
		
		ssize_t XtraRtmpServer::onCheckbw(XtraRtmp::RtmpPacketHeader &rtmpPacketHeader, XtraRtmp::AmfPacket &amfPacket, int connFd)
		{
			#define ToOnCheckbw __func__
					
			if (rtmpPacketHeader.size  < 8)
			return -1;
			const char * CheckBWS[5][2]={ 
										{"_error", 0},
										{0, 0},
										{"level", "error"},
										{"code", "NetConnection.Call.Failed"},
										{"description", "call to function _checkbw failed"}

									};

				
			rtmpPacketHeader.flag = 1;
			rtmpPacketHeader.chunkStreamID = 3;
			rtmpPacketHeader.size = 8;
			nana->say(Nana::HAPPY, ToOnCheckbw, "REPLY RTMP AMF COMMAND DONE:{_error,{code:NetConnection.Call.Failed}}");
			return onRtmpReply(rtmpPacketHeader, amfPacket.transactionID+1, CheckBWS, 5, connFd);											  
		}
	
	
		
		ssize_t XtraRtmpServer::onReleaseStream(XtraRtmp::RtmpPacketHeader &rtmpPacketHeader, XtraRtmp::AmfPacket &amfPacket, int connFd)
		{
			#define ToReleaseStream __func__
			nana->say(Nana::HAPPY, ToReleaseStream, "+++++++++++++++START+++++++++++++++");
			const char * ReleaseStreamParametersS[4][2]={ 
									{"_result", 0},
									{"level", "status"},
									{"code", "NetConnection.Call.Failed"}
								};
			rtmpPacketHeader.flag = 1;
			rtmpPacketHeader.chunkStreamID = 3;
			rtmpPacketHeader.size = 8;
			nana->say(Nana::HAPPY, ToReleaseStream, "+++++++++++++++DONE+++++++++++++++");
			return onRtmpReply(rtmpPacketHeader, amfPacket.transactionID+1, ReleaseStreamParametersS, 4, connFd);
			
		}
	
	
	
		ssize_t XtraRtmpServer::onFCPublish(XtraRtmp::RtmpPacketHeader &rtmpPacketHeader, XtraRtmp::AmfPacket &amfPacket, int connFd)
		{
			#define ToFCPublish __func__

			ssize_t ret = 0;
			if ( rtmpPacketHeader.size  >= 8 )
			{
		
				nana->say(Nana::HAPPY, ToFCPublish, "+++++++++++++++START+++++++++++++++");
				const char * FCPublishParametersS[3][2]={ 
														{"_result",0},
														{0, 0},
														{0, 0}
													};
					
				const char * FCPublishParametersC[4][2]={ 
														{"onFCPublish",0},
														{0, 0},
														{"code","NetStream.Publish.Start"},
														{"description",0}
													};
				rtmpPacketHeader.flag = 0;
				rtmpPacketHeader.chunkStreamID = 3;
				rtmpPacketHeader.size = 12;
				FCPublishParametersC[3][1]=amfPacket.streamName.c_str();
				ret = onRtmpReply(rtmpPacketHeader, amfPacket.transactionID+1, FCPublishParametersS, 3, connFd);
				ret += onRtmpReply(rtmpPacketHeader, 0, FCPublishParametersC, 4, connFd);
				nana->say(Nana::HAPPY, ToFCPublish, "+++++++++++++++DONE+++++++++++++++");
			}
			else
			{
				ret = -1;
			}
			
			if ( ret < 0 )
			{
				ret = -1;
			}

			return ret;
		}
	
	
		WorkthreadInfoIterator getWorkthreadThroughRoundrobin()
		{
    			static size_t round =0;
    			WorkthreadInfoIterator wiIter = WorkthreadInfoPool.begin();
    			size_t count = 0;
    			while( wiIter != WorkthreadInfoPool.end() )
    			{
        			if( count == round )
            			break;
        			++count;
        			++wiIter;
    			}
    
    			round = (round+1) % DEFAULT_WORKTHREADS_SIZE;
    			return wiIter;
		}
		

		ssize_t XtraRtmpServer::onPublish(XtraRtmp::RtmpPacketHeader &rtmpPacketHeader, XtraRtmp::AmfPacket &amfPacket, int connFd)
		{
			#define ToOnPublish __func__

			ssize_t ret = 0;
			WorkthreadInfoIterator wiIter= WorkthreadInfoPool.begin();
			while ( wiIter != WorkthreadInfoPool.end() )
			{
				if ( (*wiIter)->channelsPool.find( amfPacket.streamName ) != (*wiIter)->channelsPool.end() )
				break;	
				++wiIter;
			}

			if ( wiIter == WorkthreadInfoPool.end () )
			{
				nana->say(Nana::HAPPY, ToOnPublish, "+++++++++++++++START+++++++++++++++");
				nana->say(Nana::HAPPY, ToOnPublish, "CHANNEL %s NOT EXISTS,JUST CREATE IT", 
												   amfPacket.streamName.c_str() );
				
				const char * PublishParametersS[5][2]={ 
													{"onStatus",0},
													{0, 0},
													{"level", "status"},
													{"code","NetStream.Publish.Start"},
													{"clientid","mlgb123456"}
												};

				
				rtmpPacketHeader.flag = 0;
				//the audio video channel
				rtmpPacketHeader.chunkStreamID = 4;
				rtmpPacketHeader.size = 12;
				ret = onRtmpReply(rtmpPacketHeader, 0, PublishParametersS, 5, connFd);
				if ( ret >= 0 )
				{
					wiIter = getWorkthreadThroughRoundrobin();
					if( ev_async_pending( (*wiIter)->asyncWatcher ) == 0 )
					{
						ChannelPtr channelPtr = new Channel;
						if ( channelPtr != 0 )
						{
							channelPtr->scriptTagBuffer = 0;
							channelPtr->avcSeqHeaderBuffer = 0;
							channelPtr->aacSeqHeaderBuffer = 0;
							
							std::pair<std::map<std::string, ChannelPtr>::iterator,bool> mapInsert = 
							(*wiIter)->channelsPool.insert(std::make_pair(amfPacket.streamName, channelPtr));
							if ( mapInsert.second )
							{
								service::LibevAsyncData *asyncData = new service::LibevAsyncData;
								if ( asyncData != 0 )
								{
									asyncData->type = CAMERA;
									asyncData->sockFd = connFd;
									asyncData->channel = new char[amfPacket.streamName.length()+1];
									if ( asyncData->channel != 0 )
									{
										strcpy(asyncData->channel, amfPacket.streamName.c_str());
										(*wiIter)->asyncWatcher->data=(void *)asyncData;
                    							ev_async_send((*wiIter)->eventLoopEntry, (*wiIter)->asyncWatcher );
									}
									else
									{
										(*wiIter)->channelsPool.erase(amfPacket.streamName);
										ret = -1;
									}
								}
								else
								{
									nana->say(Nana::COMPLAIN, ToOnPublish, "ALLOCATE MEMORY FAILED");	
									delete channelPtr;
									channelPtr = 0;
									ret = -1;
								}
								
							}
							else
							{
								delete channelPtr;
								channelPtr = 0;
								ret = -1;
							}
						}
						else
						{
							ret = -1;
							nana->say(Nana::COMPLAIN, ToOnPublish, "ALLOCATE MEMORY FAILED");	
						}	
					}
					else
					{
						ret = -1;
						nana->say(Nana::COMPLAIN, ToOnPublish, "EV ASYNC PENDING ,JUST TRY AGAIN");
					}
				}
				nana->say(Nana::HAPPY, ToOnPublish, "+++++++++++++++DONE+++++++++++++++");
			}
			else
			{
				ret = -1;
			}

			if ( ret == 0 )
			{
				ret = 1;
			}
			return ret;
		}

			
		ssize_t XtraRtmpServer::onRtmpReply(XtraRtmp::RtmpPacketHeader &rtmpPacketHeader, unsigned char *transactionID, 
										const char *parameters[][2], int rows, int connFd)
		{
			#define ToOnRtmpReply __func__
			unsigned char reply[250]={0};
			size_t AMFSize = generateReply(reply, transactionID, parameters, rows);
			unsigned char * rtmpPack = new unsigned char[rtmpPacketHeader.size+AMFSize];
			rtmpPack[0] =(unsigned char)(rtmpPacketHeader.flag << 6) |rtmpPacketHeader.chunkStreamID;
			//set the rtmp packet's size
			rtmpPack[4] = static_cast<unsigned char>((AMFSize & 0xff0000) >> 16);
			rtmpPack[5] = static_cast<unsigned char>((AMFSize & 0xff00 ) >> 8);
			rtmpPack[6] = static_cast<unsigned char>(AMFSize & 0xff);
			rtmpPack[7] = XtraRtmp::MESSAGE_INVOKE;
	
			if (rtmpPacketHeader.size == 12)
			{
				int streamID = htonl(rtmpPacketHeader.streamID);
				memcpy(rtmpPack+8, &streamID, 4); 
			}
			
			memcpy(rtmpPack+rtmpPacketHeader.size, reply, AMFSize);
			ssize_t wroteBytes = NetUtil::writeSpecifySize2(connFd, rtmpPack, rtmpPacketHeader.size+AMFSize);

			ssize_t ret = 0;
			
			if ( wroteBytes < 0  )
			{
				nana->say(Nana::COMPLAIN, ToOnRtmpReply, "CLIENT MIGHT DISCONNECTED ALREADY");
				ret = -1;
			}
			
			delete [] rtmpPack;
			return ret;

		}
	
	
	
		
		size_t XtraRtmpServer::generateReply(unsigned char *reply, unsigned char *transactionID, 
												  const char * parameters[][2], int rows)
		{
			//the core string for reply
			int row = 0;
			reply[row]=XtraRtmp::TYPE_CORE_STRING;
			size_t AMFSize = 1;
			size_t len = strlen(parameters[0][0]);
			reply[AMFSize] = static_cast<char>((len & 0xff00) >> 8);
			reply[AMFSize+1] = static_cast<char>(len & 0xff);
			AMFSize += 2;
			memcpy(reply+AMFSize, parameters[0][0], len);
			AMFSize += len;
	
			reply[AMFSize]=XtraRtmp::TYPE_CORE_NUMBER;
			AMFSize += 1;
			if (transactionID != 0)
			{
				memcpy(reply+AMFSize, transactionID, 8);
			}
			else
			{
				memset(reply+AMFSize, 0, 8);
			}
			
			AMFSize += 8;
			bool object = false;
			
			for(row=1; row < rows; ++row)
			{
				if (parameters[row][0] != 0 && parameters[row][1] != 0)
				{
					object = true;
					continue;
				}
				
				if (parameters[row][0] == 0 && parameters[row][1] == 0)
				{
					reply[AMFSize]=XtraRtmp::TYPE_CORE_NULL;
					AMFSize += 1;
				}
				else 
				{
					reply[AMFSize]=XtraRtmp::TYPE_CORE_NUMBER;
					AMFSize += 1;
					memcpy(reply+AMFSize, (unsigned char *)parameters[row][1], 8);
					AMFSize += 8;
				}
			}
	
			if ( object )
			{
					
				//the core object for reply
				reply[AMFSize]=XtraRtmp::TYPE_CORE_OBJECT;
				AMFSize += 1;
			}
	
			
			for (row=1; row < rows; ++row)
			{
				if (parameters[row][0] == 0 ||parameters[row][1] == 0)
				continue;
				
				len = strlen(parameters[row][0]);
				reply[AMFSize] = static_cast<char>((len & 0xff00) >> 8);
				reply[AMFSize+1] = static_cast<char>(len & 0xff);
				AMFSize += 2;
				memcpy(reply+AMFSize, parameters[row][0], len);
				AMFSize += len;
				reply[AMFSize]=XtraRtmp::TYPE_CORE_STRING;
				AMFSize += 1;
				len = strlen(parameters[row][1]);
				reply[AMFSize] = static_cast<char>((len & 0xff00) >> 8);
				reply[AMFSize+1] = static_cast<char>(len & 0xff);
				AMFSize += 2;
				memcpy(reply+AMFSize, parameters[row][1], len);
				AMFSize += len;
			}
	
			//the end of core object
			if ( object )
			{
				reply[AMFSize]=0;
				reply[AMFSize+1]=0;
				reply[AMFSize+2]=9;
				AMFSize += 3;
			}
			return AMFSize;
		}
	
	

		ssize_t XtraRtmpServer::onRtmpReply(const XtraRtmp::RtmpMessageType & rtmpMessageType, int connFD,  size_t size)
		{
			unsigned char reply[250]={0};
			int pos = 0;
			int AMFSize = 0;
			switch (rtmpMessageType)
			{
				case XtraRtmp::MESSAGE_WINDOW_ACKNOWLEDGEMENT_SIZE:
					AMFSize = 4;
					reply[pos]=2;
					pos += 4;
					reply[pos+2] = static_cast<unsigned char>(AMFSize);
					pos+=3;
					reply[pos]=XtraRtmp::MESSAGE_WINDOW_ACKNOWLEDGEMENT_SIZE;
					pos += 1;
					pos += 4;
					reply[pos] = static_cast<unsigned char>((size & 0xff000000) >> 24);
					reply[pos+1] = static_cast<unsigned char>((size & 0x00ff0000) >> 16);
					reply[pos+2] = static_cast<unsigned char>((size & 0x0000ff00) >> 8);
					reply[pos+3] = static_cast<unsigned char>(size & 0x000000ff);
					pos += 4;
				break;
				case XtraRtmp::MESSAGE_SET_PEER_BANDWIDTH:
					AMFSize = 5;
					reply[pos]=2;
					pos += 4;
					reply[pos+2] = static_cast<unsigned char>(AMFSize);
					pos+=3;
					reply[pos]=XtraRtmp::MESSAGE_SET_PEER_BANDWIDTH;
					pos += 1;
					pos += 4;
					reply[pos] = static_cast<unsigned char>((size & 0xff000000) >> 24);
					reply[pos+1] = static_cast<unsigned char>((size & 0x00ff0000) >> 16);
					reply[pos+2] = static_cast<unsigned char>((size & 0x0000ff00) >> 8);
					reply[pos+3] = static_cast<unsigned char>(size & 0x000000ff);
					pos += 4;
					reply[pos]=2;
					pos+=1;
				break;
				case XtraRtmp::MESSAGE_USER_CONTROL:
					AMFSize = 6;
					reply[pos]=2;
					pos += 4;
					reply[pos+2] = static_cast<unsigned char>(AMFSize);
					pos+=3;
					reply[pos]=XtraRtmp::MESSAGE_USER_CONTROL;
					pos += 1;
					pos += 4;
					pos += 6;
				break;
				default:
					break;
			}

			ssize_t ret;
			if ( NetUtil::writeSpecifySize2(connFD, reply, pos) > 0 )
			{
				ret = 0;
			}
			else
			{
				ret = -1;
			}
			return ret;
		}
	};
}		
