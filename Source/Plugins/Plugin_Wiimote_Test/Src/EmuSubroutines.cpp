// Copyright (C) 2003-2008 Dolphin Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official SVN repository and contact information can be found at
// http://code.google.com/p/dolphin-emu/




// ===================================================
/* HID reports access guide. */
// ----------------

/* 0x10 - 0x1a   Output   EmuMain.cpp: HidOutputReport()
       0x10 - 0x14: General
	   0x15: Status report request from the Wii
	   0x16 and 0x17: Write and read memory or registers
       0x19 and 0x1a: General
   0x20 - 0x22   Input    EmuMain.cpp: HidOutputReport() to the destination
       0x15 leads to a 0x20 Input report
       0x17 leads to a 0x21 Input report
	   0x10 - 0x1a leads to a 0x22 Input report
   0x30 - 0x3f   Input    This file: Update() */

// ================



#include "pluginspecs_wiimote.h"

#include <vector>
#include <string>
#include "Common.h"
#include "wiimote_hid.h"
#include "EmuSubroutines.h"
#include "EmuDefinitions.h"
#include "Console.h" // for startConsoleWin, wprintf, GetConsoleHwnd

extern SWiimoteInitialize g_WiimoteInitialize;

namespace WiiMoteEmu
{


//******************************************************************************
// Subroutine declarations
//******************************************************************************

u32 convert24bit(const u8* src) {
	return (src[0] << 16) | (src[1] << 8) | src[2];
}

u16 convert16bit(const u8* src) {
	return (src[0] << 8) | src[1];
}


// ===================================================
/* Calibrate the mouse position to the emulation window. */
// ----------------
void GetMousePos(float& x, float& y)
{
#ifdef _WIN32
	POINT point;

	GetCursorPos(&point);
	ScreenToClient(g_WiimoteInitialize.hWnd, &point);

	RECT Rect;
	GetClientRect(g_WiimoteInitialize.hWnd, &Rect);

	int width = Rect.right - Rect.left;
	int height = Rect.bottom - Rect.top;

	x = point.x / (float)width;
	y = point.y / (float)height;
#else
        // TODO fix on linux
	x = 0.5f;
	y = 0.5f;
#endif
}


// ===================================================
/* Homebrew encryption for 0x00000000 encryption keys. */
// ----------------
void CryptBuffer(u8* _buffer, u8 _size)
{
	for (int i=0; i<_size; i++)
	{
		_buffer[i] = ((_buffer[i] - 0x17) ^ 0x17) & 0xFF;
	}
}

void WriteCrypted16(u8* _baseBlock, u16 _address, u16 _value)
{
	u16 cryptedValue = _value;
	CryptBuffer((u8*)&cryptedValue, sizeof(u16));

	*(u16*)(_baseBlock + _address) = cryptedValue;
	//PanicAlert("Converted %04x to %04x", _value, cryptedValue);
}
// ================


// ===================================================
/* Write initial values to Eeprom and registers. */
// ----------------
void Initialize()
{
	memset(g_Eeprom, 0, WIIMOTE_EEPROM_SIZE);
	memcpy(g_Eeprom, EepromData_0, sizeof(EepromData_0));
	memcpy(g_Eeprom + 0x16D0, EepromData_16D0, sizeof(EepromData_16D0));

	g_ReportingMode = 0;


	/* Extension data for homebrew applications that use the 0x00000000 key. This
	   writes 0x0000 in encrypted form (0xfefe) to 0xfe in the extension register. */
	//WriteCrypted16(g_RegExt, 0xfe, 0x0000); // Fully inserted Nunchuk


	// Copy nuncuck id and calibration to its register
	memcpy(g_RegExt + 0x20, nunchuck_calibration, sizeof(nunchuck_calibration));
	memcpy(g_RegExt + 0xfa, nunchuck_id, sizeof(nunchuck_id));


//	g_RegExt[0xfd] = 0x1e;
//	g_RegExt[0xfc] = 0x9a;
}
// ================


void DoState(void* ptr, int mode) 
{
	//TODO: implement
}

void Shutdown(void) 
{
}


// ===================================================
/* This function produce Wiimote Input, i.e. reports from the Wiimote in response
   to Output from the Wii. */
// ----------------
void InterruptChannel(u16 _channelID, const void* _pData, u32 _Size) 
{
	LOGV(WII_IPC_WIIMOTE, 0, "=============================================================");
	const u8* data = (const u8*)_pData;

	// Debugging. Dump raw data.
	{
		LOG(WII_IPC_WIIMOTE, "Wiimote_Input");
		std::string Temp;
		for (u32 j=0; j<_Size; j++)
		{
			char Buffer[128];
			sprintf(Buffer, "%02x ", data[j]);
			Temp.append(Buffer);
		}
		LOG(WII_IPC_WIIMOTE, "   Data: %s", Temp.c_str());
	}
	hid_packet* hidp = (hid_packet*) data;

	switch(hidp->type)
	{
	case HID_TYPE_DATA:
		{
			switch(hidp->param)
			{
			case HID_PARAM_OUTPUT:
				{
					wm_report* sr = (wm_report*)hidp->data;

					HidOutputReport(_channelID, sr);

					/* This is the 0x22 answer to all Inputs. In most games it didn't matter
					   if it was written before or after HidOutputReport(), but Wii Sports
					   and Mario Galaxy would stop working if it was placed before
					   HidOutputReport(). */
					wm_write_data *wd = (wm_write_data*)sr->data;
					u32 address = convert24bit(wd->address);
					WmSendAck(_channelID, sr->channel, address);
				}
				break;

			default:
				PanicAlert("HidInput: HID_TYPE_DATA - param 0x%02x", hidp->type, hidp->param);
				break;
			}
		}
		break;

	default:
		PanicAlert("HidInput: Unknown type 0x%02x and param 0x%02x", hidp->type, hidp->param);
		break;
	}
	LOGV(WII_IPC_WIIMOTE, 0, "=============================================================");
}


void ControlChannel(u16 _channelID, const void* _pData, u32 _Size) 
{
	const u8* data = (const u8*)_pData;
	// dump raw data
	{
		LOG(WII_IPC_WIIMOTE, "Wiimote_ControlChannel");
		std::string Temp;
		for (u32 j=0; j<_Size; j++)
		{
			char Buffer[128];
			sprintf(Buffer, "%02x ", data[j]);
			Temp.append(Buffer);
		}
		LOG(WII_IPC_WIIMOTE, "   Data: %s", Temp.c_str());
	}

	hid_packet* hidp = (hid_packet*) data;
	switch(hidp->type)
	{
	case HID_TYPE_HANDSHAKE:
		if (hidp->param == HID_PARAM_INPUT)
		{
			PanicAlert("HID_TYPE_HANDSHAKE - HID_PARAM_INPUT");
		}
		else
		{
			PanicAlert("HID_TYPE_HANDSHAKE - HID_PARAM_OUTPUT");
		}
		break;

	case HID_TYPE_SET_REPORT:
		if (hidp->param == HID_PARAM_INPUT)
		{
			PanicAlert("HID_TYPE_SET_REPORT input");
		}
		else
		{
			HidOutputReport(_channelID, (wm_report*)hidp->data);

			//return handshake
			u8 handshake = 0;
			g_WiimoteInitialize.pWiimoteInput(_channelID, &handshake, 1);
		}
		break;

	case HID_TYPE_DATA:
		PanicAlert("HID_TYPE_DATA %s", hidp->type, hidp->param == HID_PARAM_INPUT ? "input" : "output");
		break;

	default:
		PanicAlert("HidControlChanel: Unknown type %x and param %x", hidp->type, hidp->param);
		break;
	}

}

void Update() 
{
	//LOG(WII_IPC_WIIMOTE, "Wiimote_Update");

	switch(g_ReportingMode) {
	case 0:
		break;
	case WM_REPORT_CORE:			SendReportCore(g_ReportingChannel);			break;
	case WM_REPORT_CORE_ACCEL:		SendReportCoreAccel(g_ReportingChannel);	break;
	case WM_REPORT_CORE_ACCEL_IR12: SendReportCoreAccelIr12(g_ReportingChannel);break;
	case WM_REPORT_CORE_ACCEL_EXT16: SendReportCoreAccelExt16(g_ReportingChannel);break;
	case WM_REPORT_CORE_ACCEL_IR10_EXT6: SendReportCoreAccelIr10Ext(g_ReportingChannel);break;
	}
	// g_ReportingMode = 0;
}

}
