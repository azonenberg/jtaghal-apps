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
	@brief Main project header
 */
#ifndef jtagsh_h
#define jtagsh_h

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <memory.h>
#include <string>
#include <list>

#include <editline/readline.h>
#include <editline/history.h>

#include "../../lib/jtaghal/jtaghal.h"

#include <signal.h>

void TopLevelShell(NetworkedJtagInterface& iface);
void DeviceShell(TestableDevice* pdev, JtagInterface* iface);
void TargetShell(DebuggableDevice* pdev, unsigned int ndev, unsigned int ntarget, JtagInterface* iface);

void OnAutodetect(NetworkedJtagInterface& iface, bool quiet);
void OnTargets(NetworkedJtagInterface& iface);
void OnTarget(DebuggerInterface* iface, const std::vector<std::string>& args, unsigned int ndev, JtagInterface* jface);
void OnProgram(ProgrammableDevice* pdev, const std::vector<std::string>& args);
void OnTargets(DebuggerInterface* iface);

void OnARMCommand(ARMDebugPort* pdev, const std::string& cmd, const std::vector<std::string>& args);
void OnDebugCommand(DebuggableDevice* pdev, const std::string& cmd, const std::vector<std::string>& args);
void OnDebugFPBCommand(ARMFlashPatchBreakpoint* pdev, const std::string& cmd, const std::vector<std::string>& args);
void OnXilinxCommand(XilinxFPGA* pdev, const std::string& cmd, const std::vector<std::string>& args);

#endif
