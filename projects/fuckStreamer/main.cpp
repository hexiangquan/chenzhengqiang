/*
 * @author:chenzhengqiang
 * @company:swwy
 * @date:2016/2/24
 * @modified-date:
 * @version:1.0
 * @desc:
 * this is the implementation of the file live server
*/

#include "fuckstreamer.h"
#include "netutil.h"
#include "serverutil.h"
#include <iostream>
#include <cstdlib>
using std::cerr;
using std::endl;

using namespace czq;

static const char *DEFAULT_CONFIG_FILE="/etc/fuckStreamer/server.conf";

int main( int ARGC, char ** ARGV )
{
	ServerUtil::CmdOptions cmdOptions;
	ServerUtil::handleCmdOptions( ARGC, ARGV, cmdOptions );
	ServerUtil::ServerConfig serverConfig;

	if ( ! cmdOptions.configFile.empty() )
    	{
		ServerUtil::readConfig( cmdOptions.configFile.c_str(), serverConfig );
    	}
    	else
    	{
		ServerUtil::readConfig( DEFAULT_CONFIG_FILE, serverConfig );
    	}

	service::FuckStreamer  fuckStreamer(serverConfig);
    	if ( cmdOptions.needPrintHelp )
    	{
		fuckStreamer.printHelp();
    	}

    	if ( cmdOptions.needPrintVersion )
    	{
		fuckStreamer.printVersion();
    	}

	fuckStreamer.serveForever();
    	return 0;
}	