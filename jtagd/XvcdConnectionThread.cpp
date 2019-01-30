/***********************************************************************************************************************
*                                                                                                                      *
* ANTIKERNEL v0.1                                                                                                      *
*                                                                                                                      *
* Copyright (c) 2012-2019 Andrew D. Zonenberg                                                                          *
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
	@brief Main function for handling connections from client
 */
#include "jtagd.h"
#include "../../lib/jtaghal/ProtobufHelpers.h"

using namespace std;

/**
	@brief Main function for handling connections using the XVCD protocol
 */
void ProcessXvcdConnection(TestInterface* iface, Socket& client)
{
	try
	{
		//Set no-delay flag
		if(!client.DisableNagle())
		{
			throw JtagExceptionWrapper(
				"Failed to set TCP_NODELAY",
				"");
		}

		//Pre-cache casted versions of the interface.
		//JTAG only, no SWD or GPIO supported
		auto jface = dynamic_cast<JtagInterface*>(iface);

		//"shift:", 32 bit little endian word, strings of bits
		//open_hw_target -xvc_url localhost:2542
		while(true)
		{
			//Read command (bytes until we get a colon)
			//All commands are at least six bytes long
			unsigned char cmdbuf[128] = {0};
			client.RecvLooped(cmdbuf, 6);
			LogDebug("start: %s\n", cmdbuf);

			//Should be "getinfo:", read 2 more bytes to make sure
			if(cmdbuf[0] == 'g')
			{
				client.RecvLooped(cmdbuf+6, 2);
				LogDebug("command: %s\n", cmdbuf);
				if(0 != strcmp((char*)cmdbuf, "getinfo:"))
				{
					throw JtagExceptionWrapper(
						"Got a garbage command (expected getinfo, got something else)",
						"");
				}

				const char* info = "xvcServer_v1.0:2048\n";
				LogDebug("sending %s\n", info);
				client.SendLooped((const unsigned char*)info, strlen(info));
			}

			//Is it a shift command?
			else if(!strcmp((char*)cmdbuf, "shift:"))
			{
				throw JtagExceptionWrapper(
					"shift command not supported",
					"");
			}

			//Nope, must be settck
			else
			{
				client.RecvLooped(cmdbuf+6, 1);
				if(0 != strcmp((char*)cmdbuf, "settck:"))
				{
					throw JtagExceptionWrapper(
						"Got a garbage command (expected settck, got something else)",
						"");
				}

				//Read the clock speed
				uint32_t clock_period_ns;
				client.RecvLooped((unsigned char*)&clock_period_ns, 4);
				float clock_mhz = 1000.0f / clock_period_ns;
				LogDebug("Client requested clock period %d ns (%.2f MHz)\n",
					clock_period_ns, clock_mhz);
				LogNotice("Ignoring requested clock speed (unimplemented)\n");

				client.SendLooped((unsigned char*)&clock_period_ns, 4);
			}
		}
	}
	catch(JtagException& ex)
	{
		//Socket closed? Don't display the message, it just spams the console
		if(ex.GetDescription().find("Socket closed") == string::npos)
			LogError("%s\n", ex.GetDescription().c_str());
		fflush(stdout);
	}
}
