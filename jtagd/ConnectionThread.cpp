/***********************************************************************************************************************
*                                                                                                                      *
* ANTIKERNEL v0.1                                                                                                      *
*                                                                                                                      *
* Copyright (c) 2012-2016 Andrew D. Zonenberg                                                                          *
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
	@brief Main function for handling connections
 */
void ProcessConnection(TestInterface* iface, Socket& client)
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

		//Pre-cache casted versions of the interface
		auto jface = dynamic_cast<JtagInterface*>(iface);
		auto sface = dynamic_cast<SWDInterface*>(iface);
		auto gface = dynamic_cast<GPIOInterface*>(iface);

		//Send the server-hello message
		JtaghalPacket packet;
		auto h = packet.mutable_hello();
		h->set_magic("JTAGHAL");
		h->set_version(1);
		if(jface)
			h->set_transport(Hello::TRANSPORT_JTAG);
		else if(sface)
			h->set_transport(Hello::TRANSPORT_SWD);
		else
		{
			throw JtagExceptionWrapper(
				"Unsupported transport",
				"");
		}
		if(!SendMessage(client, packet))
		{
			throw JtagExceptionWrapper(
				"Failed to send serverhello",
				"");
		}

		//Get the client-hello message
		if(!RecvMessage(client, packet, JtaghalPacket::kHello))
		{
			throw JtagExceptionWrapper(
				"Failed to get clienthello",
				"");
		}
		auto ch = packet.hello();
		if( (ch.magic() != "JTAGHAL") || (ch.version() != 1) )
		{
			throw JtagExceptionWrapper(
				"ClientHello has wrong magic/version",
				"");
		}
		switch(ch.transport())
		{
			case Hello::TRANSPORT_JTAG:
				if(!jface)
				{
					throw JtagExceptionWrapper(
						"Client requested JTAG but this adapter doesn't support it",
						"");
				}
				break;

			case Hello::TRANSPORT_SWD:
				if(!sface)
				{
					throw JtagExceptionWrapper(
						"Client requested SWD but this adapter doesn't support it",
						"");
				}
				break;

			default:
				break;
		}

		//Sit around and wait for messages
		while(RecvMessage(client, packet))
		{
			JtaghalPacket reply;

			bool quit = false;
			switch(packet.Payload_case())
			{
				case JtaghalPacket::kHello:
					LogWarning("Got unexpected hello packet in the middle of a session\n");
					break;

				//Client is disconnecting
				case JtaghalPacket::kDisconnectRequest:
					LogVerbose("Normal termination requested\n");
					quit = true;
					break;

				//Flushing the queue
				case JtaghalPacket::kFlushRequest:
					iface->Commit();
					break;

				//Read adapter info and send it to the client
				case JtaghalPacket::kInfoRequest:
					{
						auto ir = reply.mutable_inforeply();

						switch(packet.inforequest().req())
						{
							case InfoRequest::HwName:
								ir->set_str(iface->GetName());
								break;

							case InfoRequest::HwSerial:
								ir->set_str(iface->GetSerial());
								break;

							case InfoRequest::Userid:
								ir->set_str(iface->GetUserID());
								break;

							case InfoRequest::Freq:
								ir->set_num(iface->GetFrequency());
								break;

							default:
								LogError("Got invalid InfoRequest\n");
						}

						if(!SendMessage(client, reply))
						{
							throw JtagExceptionWrapper(
								"Failed to send info reply",
								"");
						}
					};
					break;

				//Query performance counters
				case JtaghalPacket::kPerfRequest:
					if(jface)
					{
						auto ir = reply.mutable_inforeply();

						switch(packet.perfrequest().req())
						{
							case JtagPerformanceRequest::ShiftOps:
								ir->set_num(jface->GetShiftOpCount());
								break;

							case JtagPerformanceRequest::DataBits:
								ir->set_num(jface->GetDataBitCount());
								break;

							case JtagPerformanceRequest::ModeBits:
								ir->set_num(jface->GetModeBitCount());
								break;

							case JtagPerformanceRequest::DummyClocks:
								ir->set_num(jface->GetDummyClockCount());
								break;

							default:
								LogError("Got invalid PerfRequest\n");
						}

						if(!SendMessage(client, reply))
						{
							throw JtagExceptionWrapper(
								"Failed to send info reply",
								"");
						}
					}
					else
						LogWarning("PerfRequest not supported - adapter isn't JTAG\n");
					break;

				//TODO: should this be an InfoRequest?
				case JtaghalPacket::kSplitRequest:
					if(jface)
					{
						auto ir = reply.mutable_inforeply();
						ir->set_num(jface->IsSplitScanSupported());

						if(!SendMessage(client, reply))
						{
							throw JtagExceptionWrapper(
								"Failed to send info reply",
								"");
						}
					}
					else
						LogWarning("SplitRequest not supported - adapter isn't JTAG\n");
					break;

				//State level interface
				case JtaghalPacket::kStateRequest:
					if(jface)
					{
						auto state = packet.staterequest().state();
						switch(state)
						{
							case JtagStateChangeRequest::TestLogicReset:
								jface->TestLogicReset();
								break;

							case JtagStateChangeRequest::EnterShiftIR:
								jface->EnterShiftIR();
								break;

							case JtagStateChangeRequest::LeaveExitIR:
								jface->LeaveExit1IR();
								break;

							case JtagStateChangeRequest::EnterShiftDR:
								jface->EnterShiftDR();
								break;

							case JtagStateChangeRequest::LeaveExitDR:
								jface->LeaveExit1DR();
								break;

							case JtagStateChangeRequest::ResetToIdle:
								jface->ResetToIdle();
								break;

							default:
								LogError("Unimplemented chain state: %d\n", state);
								break;
						}
					}
					else
						LogWarning("StateRequest not supported - adapter isn't JTAG\n");
					break;

				//JTAG scan operations
				case JtaghalPacket::kScanRequest:
					if(jface)
					{
						auto req = packet.scanrequest();
						size_t count = req.totallen();
						size_t bytesize =  ceil(count / 8.0f);

						//If we are going to have read data, allocate a buffer for it
						uint8_t* rxdata = NULL;
						if(req.readrequested())
							rxdata = new uint8_t[bytesize];

						//If no read or write data, just send dummy clocks
						if(req.writedata().empty() && !req.readrequested())
							jface->SendDummyClocks(count);

						//We're sending or receiving data. It's an actual shift operation.
						else
						{
							//Sanity check that the send data is big enough
							if(req.writedata().size() < bytesize)
							{
								throw JtagExceptionWrapper(
									"Not enough TX data for requested clock cycle count",
									"");
							}

							//Split scans
							if(req.split())
							{
								//Read only
								if(req.writedata().size() == 0)
									jface->ShiftDataReadOnly(rxdata, count);

								//Write only
								else
								{
									if(!jface->ShiftDataWriteOnly(req.settmsatend(), (const uint8_t*)req.writedata().c_str(), rxdata, count))
									{
										throw JtagExceptionWrapper(
											"Read wasn't actually deferred - not implemented!",
											"");
									}
								}
							}

							//Non-split scans
							else
								jface->ShiftData(req.settmsatend(), (const uint8_t*)req.writedata().c_str(), rxdata, count);
						}

						//Send the reply
						if(rxdata)
						{
							auto sr = reply.mutable_scanreply();
							sr->set_readdata(string((char*)rxdata, bytesize));

							if(!SendMessage(client, reply))
							{
								throw JtagExceptionWrapper(
									"Failed to send scan reply",
									"");
							}
							delete[] rxdata;
						}
					}
					else
						LogWarning("ScanRequest not supported - adapter isn't JTAG\n");
					break;

				//Read GPIO state and send it to the client
				case JtaghalPacket::kGpioReadRequest:
					{
						auto state = reply.mutable_bankstate();
						if(gface)
						{
							gface->ReadGpioState();

							int count = gface->GetGpioCount();
							vector<uint8_t> pinstates;
							for(int i=0; i<count; i++)
							{
								auto s = state->add_states();
								s->set_value(gface->GetGpioValueCached(i));
								s->set_is_output(gface->GetGpioDirection(i));
							}
						}

						if(!SendMessage(client, reply))
						{
							throw JtagExceptionWrapper(
								"Failed to send GPIO state",
								"");
						}
					}
					break;

				default:
					LogError("Unimplemented type field: %d\n", packet.Payload_case());
					break;
			}

			/*
			switch(opcode)
			{
				case JTAGD_OP_WRITE_GPIO_STATE:
					{
						GPIOInterface* gpio = dynamic_cast<GPIOInterface*>(iface);
						if(gpio != NULL)
						{
							int count = gpio->GetGpioCount();
							uint8_t* buf = new uint8_t[count];
							client.RecvLooped(buf, count);
							for(int i=0; i<count; i++)
							{
								uint8_t val = buf[i];
								gpio->SetGpioValueDeferred(i, (val & 1) ? true : false);
								gpio->SetGpioDirectionDeferred(i, (val & 2) ? true : false);
							}
							delete[] buf;
							gpio->WriteGpioState();
						}
					};
					break;
			}
			*/

			if(quit)
				break;
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
