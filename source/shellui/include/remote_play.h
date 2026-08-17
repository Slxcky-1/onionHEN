/* Copyright (C) 2025 OnionHEN / LightningMods

This program is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the
Free Software Foundation; either version 3, or (at your option) any
later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; see the file COPYING. If not, see
<http://www.gnu.org/licenses/>.  */

#pragma once
#include "account_activator.h"

#include <cstdint>

extern void notify(const char* text, ...);
bool InitRemotePlay();
bool GeneratePINCode(uint32_t& pin);
bool GetEncodedAccountID(char* buff, uint64_t& accountid,
                         bool& activated_now);
void StopConfirmRegistLoop();
