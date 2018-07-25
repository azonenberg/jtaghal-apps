/***********************************************************************************************************************
*                                                                                                                      *
* ANTIKERNEL v0.1                                                                                                      *
*                                                                                                                      *
* Copyright (c) 2012-2018 Andrew D. Zonenberg                                                                          *
* All rights reserved.                                                                                                 *
*                                                                                                                      *
* Redistribution and use in source and binary forms, with or without modification, are permitted provided that the     *
* following conditions are met:                                                                                        *
*                                                                                                                      *
*    * Redistributions of source code must retain the above copyright notice, this list of conditions, and the         *
*      following disclaimer.                                                                                           *
*                                                                                                                      *
*    * Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the       *
*      following disclaimer in the documentation and/or other materials provided with the distribution.                *
*                                                                                                                      *
*    * Neither the name of the author nor the names of any contributors may be used to endorse or promote products     *
*      derived from this software without specific prior written permission.                                           *
*                                                                                                                      *
* THIS SOFTWARE IS PROVIDED BY THE AUTHORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED   *
* TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL *
* THE AUTHORS BE HELD LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES        *
* (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR       *
* BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT *
* (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE       *
* POSSIBILITY OF SUCH DAMAGE.                                                                                          *
*                                                                                                                      *
***********************************************************************************************************************/

/**
	@file
	@author Andrew D. Zonenberg
	@brief Main source file for jtagsh

	\ingroup jtagsh
 */
#include "jtagsh.h"

using namespace std;

void ShowUsage();
void ShowVersion();

#ifndef _WIN32
void sig_handler(int sig);
#endif

/**
	\defgroup JTAGSH: interactive shell client to jtagd

	jtagsh is released under the same permissive 3-clause BSD license as the remainder of the project.
 */

/**
	@brief Program entry point

	\ingroup jtagsh
 */
int main(int argc, char* argv[])
{
#ifndef _WINDOWS
	signal(SIGPIPE, sig_handler);
#endif

	try
	{
		Severity console_verbosity = Severity::NOTICE;

		//Global settings
		unsigned short port = 0;
		string server = "";

		//Parse command-line arguments
		for(int i=1; i<argc; i++)
		{
			string s(argv[i]);

			//Let the logger eat its args first
			if(ParseLoggerArguments(i, argc, argv, console_verbosity))
				continue;

			if(s == "--help")
			{
				ShowUsage();
				return 0;
			}
			else if(s == "--port")
			{
				if(i+1 >= argc)
				{
					fprintf(stderr, "Not enough arguments for --port\n");
					return 1;
				}

				port = atoi(argv[++i]);
			}
			else if(s == "--server")
			{
				if(i+1 >= argc)
				{
					fprintf(stderr, "Not enough arguments for --server\n");
					return 1;
				}

				server = argv[++i];
			}
			else if(s == "--version")
			{
				ShowVersion();
				return 0;
			}
			else
			{
				fprintf(stderr, "Unrecognized command-line argument \"%s\", use --help\n", s.c_str());
				return 1;
			}
		}

		//Set up logging
		g_log_sinks.emplace(g_log_sinks.begin(), new ColoredSTDLogSink(console_verbosity));

		ShowVersion();

		//Abort cleanly if no server specified
		if( (port == 0) || (server.empty()) )
		{
			ShowUsage();
			return 0;
		}

		//Connect to the server
		NetworkedJtagInterface iface;
		iface.Connect(server, port);
		LogNotice("Connected to JTAG daemon at %s:%d\n", server.c_str(), port);
		LogVerbose("    Remote JTAG adapter is a %s (serial number \"%s\", userid \"%s\", frequency %.2f MHz)\n\n",
			iface.GetName().c_str(), iface.GetSerial().c_str(), iface.GetUserID().c_str(), iface.GetFrequency()/1E6);

		//Run the command interpreter
		TopLevelShell(iface);
	}

	catch(const JtagException& ex)
	{
		LogError("%s\n", ex.GetDescription().c_str());
		return 1;
	}

	//Done
	return 0;
}

/**
	@brief Prints program usage

	\ingroup jtagsh
 */
 void ShowUsage()
{
	LogNotice(
		"Usage: jtagsh [general args] [mode]\n"
		"\n"
		"General arguments:\n"
		"    --help                                             Displays this message and exits.\n"
		"    --nobanner                                         Do not print version number on startup.\n"
		"    --port PORT                                        Specifies the port number to connect to (defaults to 50123)\n"
		"    --server [hostname]                                Specifies the hostname of the server to connect to (defaults to localhost).\n"
		"    --version                                          Prints program version number and exits.\n"
		"\n"
		);
}

#ifndef _WIN32
/**
	@brief SIGPIPE handler

	\ingroup jtagsh
 */
void sig_handler(int sig)
{
	switch(sig)
	{
		case SIGPIPE:
			//ignore
			break;
	}
}
#endif

/**
	@brief Prints program version number

	\ingroup jtagsh
 */
 void ShowVersion()
{
	LogNotice(
		"JTAG shell [git rev %s] by Andrew D. Zonenberg.\n"
		"\n"
		"License: 3-clause (\"new\" or \"modified\") BSD.\n"
		"This is free software: you are free to change and redistribute it.\n"
		"There is NO WARRANTY, to the extent permitted by law.\n"
		"\n\n"
		, "TODO");
}
