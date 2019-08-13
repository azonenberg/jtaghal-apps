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
	@brief Command interpreter functions

	\ingroup jtagsh
 */
#include "jtagsh.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Shell loops

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
		char* cmd = readline("jtag> ");
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
			OnAutodetect(iface, false);
		else if(scmd == "autodetect quiet")
			OnAutodetect(iface, true);
		else if(scmd == "ls")
			OnTargets(iface);

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
			DeviceShell(pdev, &iface);
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

void DeviceShell(TestableDevice* pdev, JtagInterface* iface)
{
	char prompt[32];
	size_t index = 0;
	JtagDevice* pj = dynamic_cast<JtagDevice*>(pdev);
	if(pj)
		index = pj->GetChainIndex();
	snprintf(prompt, sizeof(prompt), "jtag/device%zu> ", index);

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

		//Parse the command into space-separated arguments
		vector<string> args;
		string tmp;
		for(size_t i=0; i<scmd.length(); i++)
		{
			char c = scmd[i];
			if(isspace(c))
			{
				if(!tmp.empty())
					args.push_back(tmp);
				tmp = "";
			}
			else
				tmp += c;
		}
		if(!tmp.empty())
			args.push_back(tmp);

		//Empty line is a legal no-op
		if(args.size() == 0)
			continue;

		double start = GetTime();

		try
		{
			//Check for easy commands
			if( (args[0] == "exit") || (args[0] == "quit") )
				return;
			else if(args[0] == "info")
				pdev->PrintInfo();
			else if(args[0] == "ls")
				OnTargets(dynamic_cast<DebuggerInterface*>(pdev));

			else if(args[0] == "lock")
			{
				auto plock = dynamic_cast<LockableDevice*>(pdev);
				if(plock)
				{
					LogNotice("Setting reversible read protection... (power cycle or reset may be needed to take effect)\n");
					plock->SetReadLock();
				}
				else
				{
					LogError("This device does not have any supported read protection mechanism\n");
					continue;
				}
			}

			else if(args[0] == "unlock")
			{
				auto plock = dynamic_cast<LockableDevice*>(pdev);
				if(plock)
				{
					LogNotice("Clearing read protection... (will trigger bulk erase in most parts)\n");
					plock->ClearReadLock();
				}
				else
				{
					LogError("This device does not have any supported read protection mechanism\n");
					continue;
				}
			}

			else if(args[0] == "erase")
			{
				auto pprog = dynamic_cast<ProgrammableDevice*>(pdev);
				if(pprog)
				{
					LogNotice("Bulk erasing device...\n");
					pprog->Erase();
				}
				else
				{
					LogError("This device is not programmable\n");
					continue;
				}
			}

			//Do stuff that takes arguments
			else if(args[0] == "select")
			{
				OnTarget(dynamic_cast<DebuggerInterface*>(pdev), args, index, iface);
				continue;
			}
			else if(args[0] == "program")
			{
				OnProgram(dynamic_cast<ProgrammableDevice*>(pdev), args);
				continue;
			}

			//Look for prefixes of commands that have more complex parsing
			else if(args[0].find("xilinx-") == 0)
				OnXilinxCommand(dynamic_cast<XilinxFPGA*>(pdev), args[0].substr(7), args);
			else if(args[0].find("arm-") == 0)
				OnARMCommand(dynamic_cast<ARMDebugPort*>(pdev), args[0].substr(4), args);

			else
			{
				LogNotice("Unrecognized command\n");
				continue;
			}
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

void TargetShell(DebuggableDevice* pdev, unsigned int ndev, unsigned int ntarget, JtagInterface* iface)
{
	if(pdev == NULL)
	{
		LogError("The \"target\" shell requires a debuggable device\n");
		return;
	}

	char prompt[64];
	snprintf(prompt, sizeof(prompt), "jtag/device%u/target%u> ", ndev, ntarget);

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

		//Parse the command into space-separated arguments
		vector<string> args;
		string tmp;
		for(size_t i=0; i<scmd.length(); i++)
		{
			char c = scmd[i];
			if(isspace(c))
			{
				if(!tmp.empty())
					args.push_back(tmp);
				tmp = "";
			}
			else
				tmp += c;
		}
		if(!tmp.empty())
			args.push_back(tmp);

		//Empty line is a legal no-op
		if(args.size() == 0)
			continue;

		double start = GetTime();

		try
		{
			//Check for easy commands
			if( (args[0] == "exit") || (args[0] == "quit") )
				return;

			else if(args[0] == "reset")
				iface->ResetToIdle();

			//Look for prefixes of commands that have more complex parsing
			else if(args[0].find("debug-") == 0)
				OnDebugCommand(pdev, args[0].substr(6), args);

			else if(args[0] == "info")
			{
				auto papb = dynamic_cast<ARMAPBDevice*>(pdev);
				if(papb)
					papb->PrintInfo();
			}

			else
			{
				LogNotice("Unrecognized command\n");
				continue;
			}
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

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Generic commands

void OnAutodetect(NetworkedJtagInterface& iface, bool quiet)
{
	//Figure out what devices we have
	iface.InitializeChain(quiet);
}

void OnTargets(NetworkedJtagInterface& iface)
{
	//TODO: how to handle non-JTAG interfaces?

	//Print out a list of what we found
	LogNotice("%10s %7s %10s  %-60s %-50s\n", "Index", "IR len", "ID code", "Description", "Device capabilities");
	for(size_t i=0; i<iface.GetDeviceCount(); i++)
	{
		auto pdev = iface.GetJtagDevice(i);

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

		LogNotice("%10zu %7zu   %08x  %-60s %-50s\n",
			i,
			pdev->GetIRLength(),
			pdev->GetIdcode(),
			pdev->GetDescription().c_str(),
			attribs.c_str());
	}

	LogNotice("\nNOTE: Capabilities listed are based on ID code scan only, and may be restricted by device security bits.\n");
}

void OnTarget(DebuggerInterface* iface, const vector<string>& args, unsigned int ndev, JtagInterface* jface)
{
	//Sanity checks
	if(iface == NULL)
	{
		LogError("The \"target\" command can only be used on a debugger interface\n");
		return;
	}
	if(args.size() != 2)
	{
		LogError("Usage: target [target number]\n");
		return;
	}

	//Pull out the target number from the args
	uint32_t tnum = 0;
	if(1 != sscanf(args[1].c_str(), "%d", &tnum))
	{
		LogError("Usage: target [target number]\n");
		return;
	}
	if(tnum >= iface->GetNumTargets())
	{
		LogError("Usage: target [target number]\n");
		return;
	}

	//Run the interactive shell
	auto ptarget = iface->GetTarget(tnum);
	LogNotice("Selected target %u: %s\n", tnum, ptarget->GetDescription().c_str());
	TargetShell(ptarget, ndev, tnum, jface);
}

void OnProgram(ProgrammableDevice* pdev, const vector<string>& args)
{
	//Sanity checks
	if(pdev == NULL)
	{
		LogError("The \"program\" command can only be used on a programmable device\n");
		return;
	}
	if(args.size() != 2)
	{
		LogError("Usage: program [file name]\n");
		return;
	}

	//Load the firmware
	auto img = pdev->LoadFirmwareImage(args[1]);
	if(!img)
	{
		LogError("Failed to load firmware image\n");
		return;
	}

	//Flash it
	pdev->Program(img);

	delete img;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Device class commands

void OnDebugCommand(DebuggableDevice* pdev, const string& cmd, const vector<string>& args)
{
	if(pdev == NULL)
	{
		LogError("debug-* commands can only be used on a debuggable device\n");
		return;
	}

	if(cmd == "halt")
		pdev->DebugHalt();
	else if(cmd == "resume")
		pdev->DebugResume();
	else if(cmd == "regs")
		pdev->PrintRegisters();

	else if(cmd == "fpb")
	{
		if(args.size() < 2)
		{
			LogError("Usage: debug-fpb [command]\n");
			return;
		}

		auto cpu = dynamic_cast<ARMv7MProcessor*>(pdev);
		if(cpu)
		{
			auto fpb = cpu->GetFlashPatchBreakpoint();
			if(fpb)
				OnDebugFPBCommand(fpb, args[1], args);
			else
				LogError("debug-fpb requires a CPU with a Flash Patch/Breakpoint unit\n");
		}
		else
			LogError("debug-fpb requires an ARMv7-M target\n");
	}

	//Dump memory to a file
	else if(cmd == "dumpmem")
	{
		if(args.size() != 4)
		{
			LogError("Usage: dumpmem [hex base address] [hex length] [filename]\n");
			return;
		}

		uint32_t addr;
		sscanf(args[1].c_str(), "%x", &addr);

		uint32_t len;
		sscanf(args[2].c_str(), "%x", &len);

		string fname = args[3];

		FILE* fp = fopen(fname.c_str(), "wb");
		if(!fp)
		{
			LogError("couldn't open file\n");
			return;
		}

		LogIndenter li;
		for(uint32_t off=0; off<len; off += 4)
		{
			uint32_t ptr = addr + off;

			if( (ptr & 0xfff) == 0)
				LogNotice("%08x\n", ptr);

			uint32_t value = pdev->ReadMemory(ptr);
			if(1 != fwrite(&value, 4, 1, fp))
			{
				LogError("couldn't write file\n");
				return;
			}
		}
		fclose(fp);
	}

	//Read memory from the default RAM source (generally AHB or AXI bus on ARM targets)
	else if(cmd == "readmem")
	{
		if(args.size() != 2)
		{
			LogError("Usage: readmem [hex address]\n");
			return;
		}

		uint32_t addr;
		sscanf(args[1].c_str(), "%x", &addr);

		uint32_t value = pdev->ReadMemory(addr);
		LogNotice("*0x%08x = 0x%08x\n", addr, value);
	}

	//Write memory to the default destination
	else if(cmd == "writemem")
	{
		if(args.size() != 3)
		{
			LogError("Usage: writemem [hex address] [hex data]\n");
			return;
		}

		uint32_t addr;
		sscanf(args[1].c_str(), "%x", &addr);

		uint32_t value;
		sscanf(args[2].c_str(), "%x", &value);

		pdev->WriteMemory(addr, value);
	}

	else
		LogNotice("Unrecognized command\n");
}

void OnDebugFPBCommand(ARMFlashPatchBreakpoint* pdev, const string& cmd, const vector<string>& args)
{
	//Turn the FPB on/off
	if(cmd == "enable")
		pdev->Enable();
	else if(cmd == "disable")
		pdev->Disable();

	//Print debug info
	else if(cmd == "info")
		pdev->PrintInfo();

	//Set the base address of the remapping table
	else if(cmd == "setbase")
	{
		if(args.size() != 3)
		{
			LogError("Usage: debug-fpb setbase [hex address]\n");
			return;
		}

		uint32_t addr;
		sscanf(args[2].c_str(), "%x", &addr);

		pdev->SetRemapTableBase(addr);
	}

	//Write to the remapping table
	else if(cmd == "remap")
	{
		if(args.size() != 5)
		{
			LogError("Usage: debug-fpb remap [slot] [hex flash address] [new hex opcode]\n");
			return;
		}

		uint32_t slot;
		sscanf(args[2].c_str(), "%d", &slot);

		uint32_t addr;
		sscanf(args[3].c_str(), "%x", &addr);

		uint32_t opcode;
		sscanf(args[4].c_str(), "%x", &opcode);

		pdev->RemapFlashWord(slot, addr, opcode);
	}

	else
		LogNotice("Unrecognized command\n");
}

void OnTargets(DebuggerInterface* iface)
{
	if(iface == NULL)
	{
		LogError("The \"targets\" command can only be used on a debugger interface\n");
		return;
	}

	LogNotice("%10s %-50s\n", "Index", "Description");
	for(size_t i=0; i<iface->GetNumTargets(); i++)
	{
		auto target = iface->GetTarget(i);
		LogNotice("%10zu %-50s\n", i, target->GetDescription().c_str() );
	}
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Vendor commands

void OnARMCommand(ARMDebugPort* pdev, const string& cmd, const vector<string>& args)
{
	if(pdev == NULL)
	{
		LogError("arm-* commands can only be used on an ARM debug port\n");
		return;
	}

	//Read memory from the default RAM Mem-AP (generally AHB bus but might be AXI)
	if(cmd == "readmem")
	{
		if(args.size() != 2)
		{
			LogError("Usage: readmem [hex address]\n");
			return;
		}

		uint32_t addr;
		sscanf(args[1].c_str(), "%x", &addr);

		uint32_t value = pdev->ReadMemory(addr);
		LogNotice("*0x%08x = %08x\n", addr, value);
	}

	//Read memory from the default register Mem-AP
	//Generally APB bus in high-end SoCs but on lower-end may be the same bus as RAM
	else if(cmd == "readreg")
	{
		if(args.size() != 2)
		{
			LogError("Usage: arm-readreg [hex address]\n");
			return;
		}

		uint32_t addr;
		sscanf(args[1].c_str(), "%x", &addr);

		uint32_t value = pdev->ReadDebugRegister(addr);
		LogNotice("*0x%08x = 0x%08x\n", addr, value);
	}
	else if(cmd == "status")
		pdev->PrintStatusRegister();

	//Doesn't make sense
	else
		LogNotice("Unrecognized command\n");
}

void OnXilinxCommand(XilinxFPGA* pdev, const string& cmd, const vector<string>& args)
{
	if(pdev == NULL)
	{
		LogError("xilinx-* commands can only be used on Xilinx FPGAs\n");
		return;
	}

	//Do commands
	if(cmd == "readreg")
	{
		if(args.size() != 2)
		{
			LogError("Usage: xilinx-readreg [register name]\n");
			return;
		}

		//Look up the register ID
		//For now, only support named registers, not numbers
		uint32_t regid;
		if(!pdev->LookupConstant(args[1], regid))
		{
			LogError("\"%s\" is not a known register name\n", args[1].c_str());
			return;
		}

		//Do the actual read
		uint32_t value = pdev->ReadWordConfigRegister(regid);
		LogNotice("%s = 0x%08x\n", args[1].c_str(), value);
	}
	else if(cmd == "status")
		pdev->PrintStatusRegister();

	//Doesn't make sense
	else
		LogNotice("Unrecognized command\n");

}
