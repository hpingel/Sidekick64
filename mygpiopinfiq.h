// this is a quickly modified variant of Circle's GPIO-FIQ class
// the purpose is to be able to ack the FIQ at (almost) freely chosen
// locations in the FIQ handler. Newer versions of Circle support this 
// and this would not be necessary.

//
// gpiopinfiq.h
//
// Circle - A C++ bare metal environment for Raspberry Pi
// Copyright (C) 2014-2017  R. Stange <rsta2@o2online.de>
// 
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//
#ifndef _circle_gpiopinfiq2_h
#define _circle_gpiopinfiq2_h

#include <circle/gpiopin.h>
#include <circle/interrupt.h>

class CGPIOPinFIQ2 : public CGPIOPin	/// GPIO fast interrupt pin (only one allowed in the system)
{
public:
	/// \param nPin GPIO pin number
	/// \param Mode GPIO pin mode (see: gpiopin.h)
	/// \param pInterrupt Pointer to the interrupt system object
	CGPIOPinFIQ2 (unsigned nPin, TGPIOMode Mode, CInterruptSystem *pInterrupt);
	~CGPIOPinFIQ2 (void);

	/// \param pHandler Pointer to the GPIO interrupt (FIQ) handler
	/// \param pParam Any parameter, will be handed over to the interrupt handler
	void ConnectInterrupt (TGPIOInterruptHandler *pHandler, void *pParam);
	void DisconnectInterrupt (void);

	static void FIQHandler (void *pParam);

	unsigned nRegOffset() { return m_nRegOffset; }
	u32 nRegMask() { return m_nRegMask; }
private:

private:
	CInterruptSystem *m_pInterrupt;
};

#endif