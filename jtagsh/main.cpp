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
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <memory.h>
#include <string>
#include <list>

//import from editline
extern "C" char *readline (const char *prompt);
extern "C" void add_history(const char* line);

#include "../../lib/jtaghal/jtaghal.h"

#include <signal.h>

using namespace std;

void ShowUsage();
void ShowVersion();
void PrintDeviceInfo(JtagDevice* pdev);

void TopLevelShell(NetworkedJtagInterface& iface);
void OnAutodetect(NetworkedJtagInterface& iface);
void DeviceShell(JtagDevice* pdev);
void OnXilinxCommand(XilinxFPGA* pdev, const string& cmd);

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

		/*
		//Initialize the chain
		LogVerbose("Initializing chain...\n");
		double start = GetTime();
		iface.InitializeChain();
		if(profile_init_time)
		{
			double dt = GetTime() - start;
			LogDebug("    Chain walking took %.3f ms\n", dt*1000);
		}

		//Get device count and see what we've found
		LogNotice("Scan chain contains %d devices\n", (int)iface.GetDeviceCount());
		*/
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
	@brief Command interpreter
 */
void TopLevelShell(NetworkedJtagInterface& iface)
{
	while(true)
	{
		//Make sure past command output prints above the prompt
		fflush(stdout);
		fflush(stderr);

		//Get the command
		char* cmd = readline("jtag-chain> ");
		if(cmd == NULL)
			return;
		add_history(cmd);
		string scmd(cmd);
		free(cmd);
		cmd = NULL;

		double start = GetTime();

		//Check for easy commands
		if( (scmd == "exit") || (scmd == "quit") )
			return;
		else if(scmd == "autodetect")
			OnAutodetect(iface);

		//Select a new device and run the shell for it
		else if(scmd.find("select") == 0)
		{
			unsigned int ndev;
			if(1 != sscanf(scmd.c_str(), "select %u", &ndev))
			{
				LogError("Usage: \"select N\" where N is the 0-based device index\n");
				continue;
			}

			if(ndev > iface.GetDeviceCount())
			{
				LogError("Device index %u is out of range\n", ndev);
				continue;
			}

			auto pdev = iface.GetDevice(ndev);
			if(pdev == NULL)
			{
				LogError("Device index %u is NULL\n", ndev);
				continue;
			}
			LogNotice("Selected device %u: %s\n", ndev, pdev->GetDescription().c_str());

			//Actually run the shell.
			//skip elapsed time when in an interactive shell
			DeviceShell(pdev);
			continue;
		}

		else
		{
			LogNotice("Unrecognized command\n");
			continue;
		}

		//Print elapsed time if requested
		double dt = GetTime() - start;
		LogVerbose("%.3f ms\n", dt*1000);
	}
}

void DeviceShell(JtagDevice* pdev)
{
	char prompt[32];
	snprintf(prompt, sizeof(prompt), "jtag-device%zu> ", pdev->GetChainIndex());

	while(true)
	{
		//Make sure past command output prints above the prompt
		fflush(stdout);
		fflush(stderr);

		//Get the command
		char* cmd = readline(prompt);
		if(cmd == NULL)
			return;
		add_history(cmd);
		string scmd(cmd);
		free(cmd);
		cmd = NULL;

		double start = GetTime();

		try
		{
			//Check for easy commands
			if( (scmd == "exit") || (scmd == "quit") )
				return;

			//Look for prefixes of commands that have more complex parsing
			else if(scmd.find("xilinx-") == 0)
				OnXilinxCommand(dynamic_cast<XilinxFPGA*>(pdev), scmd.substr(7));
		}
		catch(const JtagException& ex)
		{
			LogError("%s\n", ex.GetDescription().c_str());
		}

		//Print elapsed time if requested
		double dt = GetTime() - start;
		LogVerbose("%.3f ms\n", dt*1000);
	}
}

void OnAutodetect(NetworkedJtagInterface& iface)
{
	//Figure out what devices we have
	iface.InitializeChain();

	//Print out a list of what we found
	LogNotice("%10s %10s  %-30s %-50s\n", "Index", "ID code", "Description", "Attributes");
	for(size_t i=0; i<iface.GetDeviceCount(); i++)
	{
		auto pdev = iface.GetDevice(i);

		//Figure out what this device is
		vector<string> alist;
		if(dynamic_cast<ProgrammableDevice*>(pdev))
			alist.push_back("programmable");
		if(dynamic_cast<LockableDevice*>(pdev))
			alist.push_back("lockable");
		if(dynamic_cast<DebuggerInterface*>(pdev))
			alist.push_back("debuggable");
		if(dynamic_cast<SerialNumberedDevice*>(pdev))
			alist.push_back("serial numbered");
		if(dynamic_cast<FPGA*>(pdev))
			alist.push_back("FPGA");
		if(dynamic_cast<CPLD*>(pdev))
			alist.push_back("CPLD");
		if(dynamic_cast<Microcontroller*>(pdev))
			alist.push_back("MCU");

		//Format attribute list
		string attribs;
		for(auto a : alist)
		{
			if(attribs.empty())
				attribs = a;
			else
				attribs += string(", ") + a;
		}

		LogNotice("%10zu   %08x  %-30s %-50s\n", i, pdev->GetIdcode(), pdev->GetDescription().c_str(), attribs.c_str());
	}
}

void OnXilinxCommand(XilinxFPGA* pdev, const string& cmd)
{
	if(pdev == NULL)
	{
		LogError("xilinx-* commands can only be used on Xilinx FPGAs\n");
		return;
	}

	//Split the command up into command and arguments
	size_t index = cmd.find(" ");
	string cbase;
	string args;
	if(index == string::npos)
	{
		cbase = cmd;
		args = "";
	}
	else
	{
		cbase = cmd.substr(0, index);
		args = cmd.substr(index+1);
	}

	//Do commands
	if(cbase == "readreg")
	{
		//Look up the register ID
		//For now, only support named registers, not numbers
		uint32_t regid;
		if(!pdev->LookupConstant(args, regid))
		{
			LogError("\"%s\" is not a known register name\n", args.c_str());
			return;
		}

		//Do the actual read
		uint32_t value = pdev->ReadWordConfigRegister(regid);
		LogNotice("%s = 0x%08x\n", args.c_str(), value);
	}
}

/**
	@brief Prints info about a single device in the chain

	@param i		Device index
	@param pdev		The device

	\ingroup jtagsh
 */
 /*
void PrintDeviceInfo(JtagDevice* pdev)
{
	if(pdev == NULL)
	{
		throw JtagExceptionWrapper(
			"Device is null, cannot continue",
			"");
	}

	pdev->PrintInfo();
}*/

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
