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
void ProcessConnection(JtagInterface* iface, Socket& client)
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

		//Send the server-hello message
		JtaghalPacket packet;
		auto h = packet.mutable_hello();
		h->set_magic("JTAGHAL");
		h->set_version(1);
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

		//Sit around and wait for messages
		while(RecvMessage(client, packet))
		{
			JtaghalPacket reply;

			bool quit = false;
			switch(packet.Payload_case())
			{
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
					{
						auto ir = reply.mutable_inforeply();

						switch(packet.perfrequest().req())
						{
							case JtagPerformanceRequest::ShiftOps:
								ir->set_num(iface->GetShiftOpCount());
								break;

							case JtagPerformanceRequest::DataBits:
								ir->set_num(iface->GetDataBitCount());
								break;

							case JtagPerformanceRequest::ModeBits:
								ir->set_num(iface->GetModeBitCount());
								break;

							case JtagPerformanceRequest::DummyClocks:
								ir->set_num(iface->GetDummyClockCount());
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
					break;

				//TODO: should this be an InfoRequest?
				case JtaghalPacket::kSplitRequest:
					{
						auto ir = reply.mutable_inforeply();
						ir->set_num(iface->IsSplitScanSupported());

						if(!SendMessage(client, reply))
						{
							throw JtagExceptionWrapper(
								"Failed to send info reply",
								"");
						}
					}
					break;

				//State level interface
				case JtaghalPacket::kStateRequest:
					{
						auto state = packet.staterequest().state();
						switch(state)
						{
							case JtagStateChangeRequest::TestLogicReset:
								iface->TestLogicReset();
								break;

							case JtagStateChangeRequest::EnterShiftIR:
								iface->EnterShiftIR();
								break;

							case JtagStateChangeRequest::LeaveExitIR:
								iface->LeaveExit1IR();
								break;

							case JtagStateChangeRequest::EnterShiftDR:
								iface->EnterShiftDR();
								break;

							case JtagStateChangeRequest::LeaveExitDR:
								iface->LeaveExit1DR();
								break;

							case JtagStateChangeRequest::ResetToIdle:
								iface->ResetToIdle();
								break;

							default:
								LogError("Unimplemented chain state: %d\n", state);
								break;
						}
					}
					break;

				//JTAG scan operations
				case JtaghalPacket::kScanRequest:
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
							iface->SendDummyClocks(count);

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
									iface->ShiftDataReadOnly(rxdata, count);

								//Write only
								else
								{
									if(!iface->ShiftDataWriteOnly(req.settmsatend(), (const uint8_t*)req.writedata().c_str(), rxdata, count))
									{
										throw JtagExceptionWrapper(
											"Read wasn't actually deferred - not implemented!",
											"");
									}
								}
							}

							//Non-split scans
							else
								iface->ShiftData(req.settmsatend(), (const uint8_t*)req.writedata().c_str(), rxdata, count);
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
					break;

				//Read GPIO state and send it to the client
				case JtaghalPacket::kGpioReadRequest:
					{
						auto state = reply.mutable_bankstate();

						GPIOInterface* gpio = dynamic_cast<GPIOInterface*>(iface);
						if(gpio != NULL)
						{
							gpio->ReadGpioState();

							int count = gpio->GetGpioCount();
							vector<uint8_t> pinstates;
							for(int i=0; i<count; i++)
							{
								auto s = state->add_states();
								s->set_value(gpio->GetGpioValueCached(i));
								s->set_is_output(gpio->GetGpioDirection(i));
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
				//Deferred write processing
				case JTAGD_OP_COMMIT:
					{
						iface->Commit();

						//Send an ACK once the commit has occurred
						uint8_t dummy = 0;
						client.SendLooped((unsigned char*)&dummy, 1);
					}
					break;

				case JTAGD_OP_SHIFT_DATA:
				case JTAGD_OP_SHIFT_DATA_WO:
					{
						uint8_t last_tms;
						uint32_t count;
						client.RecvLooped((unsigned char*)&last_tms, 1);
						client.RecvLooped((unsigned char*)&count, 4);

						//JTAGD_OP_SHIFT_DATA_WO is write only, so no response
						bool want_response = (opcode == JTAGD_OP_SHIFT_DATA);

						int bytesize =  ceil(count / 8.0f);

						//Allocate buffer if we want a response
						unsigned char* recv_data = NULL;
						if(want_response)
							recv_data = new unsigned char[bytesize];

						//Allocate buffer for data
						unsigned char* send_data = new unsigned char[bytesize];

						//Receive data and send it
						client.RecvLooped(send_data, bytesize);

						try
						{
							iface->ShiftData(last_tms, send_data, recv_data, count);

							//Send response back, if desired
							if(want_response)
								client.SendLooped(recv_data, bytesize);
						}
						catch(const JtagException& ex)
						{
							//If the actual shift operation fails, send an error code to the source
							//FIXME: This is busticated! Client is trying to read a reply, not a status code
							//if(ex.GetType() == JtagException::EXCEPTION_TYPE_ADAPTER)
							if(true)
							{
								uint8_t status = 1;
								client.SendLooped(&status, 1);

								//Print error anyway
								LogWarning("Non-fatal exception, passed to caller\n");
								LogWarning("%s\n", ex.GetDescription().c_str());
							}

							//otherwise re-throw and abort
							else
								throw;
						}

						//Clean up
						delete[] send_data;
						if(want_response)
						{
							delete[] recv_data;
							recv_data = NULL;
						}
					}
					break;

				case JTAGD_OP_SHIFT_DATA_WRITE_ONLY:
					{
						uint8_t last_tms;
						uint32_t count;
						uint8_t want_response;
						client.RecvLooped((unsigned char*)&last_tms, 1);
						client.RecvLooped((unsigned char*)&count, 4);
						client.RecvLooped((unsigned char*)&want_response, 1);

						int bytesize =  ceil(count / 8.0f);

						//Allocate buffer for data
						unsigned char* send_data = new unsigned char[bytesize];

						//Receive data and send it
						client.RecvLooped(send_data, bytesize);

						try
						{
							//Send status byte back
							recv_buf.push_back(0);

							//Preallocate buffer space
							if(want_response)
								recv_buf.resize(bytesize + 1);

							//Do the shift
							recv_buf[0] = iface->ShiftDataWriteOnly(last_tms, send_data, &recv_buf[1], count);

							//Send back status
							client.SendLooped(&recv_buf[0], recv_buf[0] ? 1 : recv_buf.size());
							recv_buf.clear();
						}
						catch(const JtagException& ex)
						{
							//If the actual shift operation fails, send an error code to the source
							//if(ex.GetType() == JtagException::EXCEPTION_TYPE_ADAPTER)
							if(true)
							{
								uint8_t status = -1;
								client.SendLooped(&status, 1);

								//Print error anyway
								LogWarning("Non-fatal exception, passed to caller\n");
								LogWarning("%s\n", ex.GetDescription().c_str());
							}

							//otherwise re-throw and abort
							else
								throw;
						}

						//Clean up
						delete[] send_data;
					}
					break;

				case JTAGD_OP_SHIFT_DATA_READ_ONLY:
					{
						uint32_t count;
						client.RecvLooped((unsigned char*)&count, 4);
						if(count == 0)
						{
							throw JtagExceptionWrapper(
								"Invalid size",
								"");
						}

						int bytesize =  ceil(count / 8.0f);

						//Allocate buffer
						unsigned char* recv_data = new unsigned char[bytesize];

						try
						{
							bool deferred = iface->ShiftDataReadOnly(recv_data, count);

							//Send status byte back
							uint8_t status = deferred ? 1 : 0;
							client.SendLooped(&status, 1);

							//Send response back, if meaningful
							if(deferred)
								client.SendLooped(recv_data, bytesize);
						}
						catch(const JtagException& ex)
						{
							//If the actual shift operation fails, send an error code to the source
							//if(ex.GetType() == JtagException::EXCEPTION_TYPE_ADAPTER)
							if(true)
							{
								uint8_t status = -1;
								client.SendLooped(&status, 1);

								//Print error anyway
								LogWarning("Non-fatal exception, passed to caller\n");
								LogWarning("%s\n", ex.GetDescription().c_str());
							}

							//otherwise re-throw and abort
							else
								throw;
						}

						//Clean up
						delete[] recv_data;
						recv_data = NULL;
					}
					break;

				case JTAGD_OP_DUMMY_CLOCK:
					{
						uint32_t count;
						client.RecvLooped((unsigned char*)&count, 4);
						iface->SendDummyClocks(count);
					}
					break;

				case JTAGD_OP_DUMMY_CLOCK_DEFERRED:
					{
						uint32_t count;
						client.RecvLooped((unsigned char*)&count, 4);
						iface->SendDummyClocksDeferred(count);
					}
					break;

				case JTAGD_OP_PERF_SHIFT:
					{
						uint64_t n = iface->GetShiftOpCount();
						client.SendLooped((unsigned char*)&n, 8);
					}
					break;

				case JTAGD_OP_PERF_DATA:
					{
						uint64_t n = iface->GetDataBitCount();
						client.SendLooped((unsigned char*)&n, 8);
					}
					break;

				case JTAGD_OP_PERF_MODE:
					{
						uint64_t n = iface->GetModeBitCount();
						client.SendLooped((unsigned char*)&n, 8);
					}
					break;

				case JTAGD_OP_PERF_DUMMY:
					{
						uint64_t n = iface->GetDummyClockCount();
						client.SendLooped((unsigned char*)&n, 8);
					}
					break;

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
