// this is a quickly modified variant of Circle's GPIO-FIQ class
// the purpose is to be able to ack the FIQ at (almost) freely chosen
// locations in the FIQ handler. Newer versions of Circle support this 
// and this would not be necessary.


//
// gpiopinfiq.cpp
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
#include "mygpiopinfiq.h"
#include <circle/bcm2835.h>
#include <circle/memio.h>
#include <circle/synchronize.h>
#include <assert.h>

CGPIOPinFIQ2::CGPIOPinFIQ2 (unsigned nPin, TGPIOMode Mode, CInterruptSystem *pInterrupt)
:	CGPIOPin (nPin, Mode),
	m_pInterrupt (pInterrupt)
{
}

CGPIOPinFIQ2::~CGPIOPinFIQ2 (void)
{
	m_pInterrupt = 0;
}

void CGPIOPinFIQ2::ConnectInterrupt (TGPIOInterruptHandler *pHandler, void *pParam)
{
	assert (   m_Mode == GPIOModeInput
		|| m_Mode == GPIOModeInputPullUp
		|| m_Mode == GPIOModeInputPullDown);

	assert (m_Interrupt == GPIOInterruptUnknown);
	assert (m_Interrupt2 == GPIOInterruptUnknown);

	assert (pHandler != 0);
	assert (m_pHandler == 0);
	m_pHandler = pHandler;

	m_pParam = pParam;

	assert (m_pInterrupt != 0);
	m_pInterrupt->ConnectFIQ (ARM_FIQ_GPIO3, pHandler, this);
}

void CGPIOPinFIQ2::DisconnectInterrupt (void)
{
	assert (   m_Mode == GPIOModeInput
		|| m_Mode == GPIOModeInputPullUp
		|| m_Mode == GPIOModeInputPullDown);

	assert (m_Interrupt == GPIOInterruptUnknown);
	assert (m_Interrupt2 == GPIOInterruptUnknown);

	assert (m_pHandler != 0);
	m_pHandler = 0;

	assert (m_pInterrupt != 0);
	m_pInterrupt->DisconnectFIQ ();
}

/*void CGPIOPinFIQ2::FIQHandler (void *pParam)
{
	CGPIOPinFIQ2 *pThis = (CGPIOPinFIQ2 *) pParam;

	PeripheralEntry ();
	write32 (ARM_GPIO_GPEDS0+pThis->m_nRegOffset, pThis->m_nRegMask);
	PeripheralExit ();

	(*pThis->m_pHandler) (pThis->m_pParam);
}
*/