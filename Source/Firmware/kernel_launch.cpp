/*
  _________.__    .___      __   .__        __       .____                               .__     
 /   _____/|__| __| _/____ |  | _|__| ____ |  | __   |    |   _____   __ __  ____   ____ |  |__  
 \_____  \ |  |/ __ |/ __ \|  |/ /  |/ ___\|  |/ /   |    |   \__  \ |  |  \/    \_/ ___\|  |  \ 
 /        \|  / /_/ \  ___/|    <|  \  \___|    <    |    |___ / __ \|  |  /   |  \  \___|   Y  \
/_______  /|__\____ |\___  >__|_ \__|\___  >__|_ \   |_______ (____  /____/|___|  /\___  >___|  /
        \/         \/    \/     \/       \/     \/           \/    \/           \/     \/     \/ 

 kernel_launch.cpp

 Sidekick64 - A framework for interfacing 8-Bit Commodore computers (C64/C128,C16/Plus4,VC20) and a Raspberry Pi Zero 2 or 3A+/3B+
            - Sidekick Launch: example how to implement a .PRG dropper (for C64 and C128)
 Copyright (c) 2019-2022 Carsten Dachsbacher <frenetic@dachsbacher.de>

 Logo created with http://patorjk.com/software/taag/
 
 This program is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
#include "kernel_launch.h"

#ifdef WITH_NET
extern CSidekickNet * pSidekickNet;
bool swiftLinkEnabled = false; //indicates if the FIQ handler should care for swiftlink register handling
static const unsigned swiftLinknetDelayDMADefault = 60000;

#ifdef SW_DEBUG
static const unsigned swiftLinkLogLengthMax = 64000;
char swiftLinkLog[ swiftLinkLogLengthMax ];
unsigned swiftLinkDataReads = 0; //how many times did c64 read from swiftlink data register, for debugging only
unsigned swiftLinkCounter = 0; //for debugging register communication
#endif
//char swiftLinkReceived[ swiftLinkLogLengthMax ];

unsigned char swiftLinkByte = 0; //incoming byte from frontend
unsigned char swiftLinkResponse = 0; //byte that comes to frontend from terminal
unsigned swiftLinkDoNMI = 0;
//unsigned swiftLinkReceivedCounter = 0; //byte count sent from terminal to frontend
unsigned swiftLinkBaud = 0;
unsigned swiftLinkBaudOld = 0;
u32 swiftLinkRegisterCmd = 0,
		swiftLinkRegisterCtrl = 0;
bool swiftLinkReleaseDMA = false;
unsigned swiftLinknetDelayDMA = swiftLinknetDelayDMADefault;

#endif

// we will read this .PRG file
static const char DRIVE[] = "SD:";
#ifndef COMPILE_MENU
static const char FILENAME[] = "SD:C64/test.prg";		// .PRG to start
static const bool c128PRG = false;
#endif
static const char FILENAME_CBM80[] = "SD:C64/launch.cbm80";	// launch code (CBM80 8k cart)
static const char FILENAME_CBM128[] = "SD:C64/launch128.cbm80";	// launch code (C128 cart)

static const char FILENAME_SPLASH_RGB[] = "SD:SPLASH/sk64_launch.tga";

// cartridge memory window bROML or bROMH
#define ROM_LH		bROML

static u32	configGAMEEXROMSet, configGAMEEXROMClr;
static u32	resetCounter, c64CycleCount;
static u32	disableCart, transferStarted, currentOfs, transferPart;
u32 prgSize;
unsigned char prgData[ 65536 ] AAA;
static u32 startAddr, prgSizeAboveA000, prgSizeBelowA000, endAddr;

// in case the launch code starts with the loading address
#define LAUNCH_BYTES_TO_SKIP	0
static unsigned char launchCode[ 65536 ] AAA;

static u32 resetFromCodeState = 0;
static u32 _playingPSID = 0;

extern volatile u8 forceReadLaunch;

void prepareOnReset( bool refresh = false )
{
	if ( !refresh )
	{
		/*CleanDataCache();
		InvalidateDataCache();
		InvalidateInstructionCache();*/
		SyncDataAndInstructionCache();
	}

	if ( _playingPSID )
	{
		extern unsigned char charset[ 4096 ];
		CACHE_PRELOADL2STRM( &charset[ 2048 ] );
		FORCE_READ_LINEAR32( (void*)&charset[ 2048 ], 1024 );
	}

	// .PRG data
	CACHE_PRELOAD_DATA_CACHE( &prgData[ 0 ], prgSize, CACHE_PRELOADL2KEEP )
	FORCE_READ_LINEAR32a( prgData, prgSize, prgSize * 8 );

	// launch code / CBM80
	CACHE_PRELOAD_DATA_CACHE( &launchCode[ 0 ], 512, CACHE_PRELOADL2KEEP )
	FORCE_READ_LINEAR32a( launchCode, 512, 512 * 16 );

	for ( u32 i = 0; i < prgSizeAboveA000; i++ )
		forceReadLaunch = prgData[ prgSizeBelowA000 + i + 2 ];

	for ( u32 i = 0; i < prgSizeBelowA000 + 2; i++ )
		forceReadLaunch = prgData[ i ];

	// FIQ handler
	CACHE_PRELOAD_INSTRUCTION_CACHE( (void*)&FIQ_HANDLER, 3 * 1024 );
	FORCE_READ_LINEAR32( (void*)&FIQ_HANDLER, 3 * 1024 );
}

static u32 nBytesRead, stage;

#ifdef COMPILE_MENU
void KernelLaunchRun( CGPIOPinFIQ m_InputPin, CKernelMenu *kernelMenu, const char *FILENAME, bool hasData = false, u8 *prgDataExt = NULL, u32 prgSizeExt = 0, u32 c128PRG = 0, u32 playingPSID = 0 )
#else
void CKernelLaunch::Run( void )
#endif
{
	
	// initialize ARM cycle counters (for accurate timing)
	//initCycleCounter();
		
	// initialize latch and software I2C buffer
	initLatch();

	latchSetClearImm( 0, LATCH_RESET | LATCH_LED_ALL | LATCH_ENABLE_KERNAL );

	if ( c128PRG )
	{
		configGAMEEXROMSet = bGAME | bEXROM | bNMI | bDMA;
		configGAMEEXROMClr = 0; 
	} else
	{
		// set GAME and EXROM as defined above (+ set NMI, DMA and latch outputs)
		configGAMEEXROMSet = bGAME | bNMI | bDMA;
		configGAMEEXROMClr = bEXROM; 
	}
	SETCLR_GPIO( configGAMEEXROMSet, configGAMEEXROMClr );

	#ifndef COMPILE_MENU
	m_EMMC.Initialize();
	#endif

	#ifdef COMPILE_MENU
	_playingPSID = playingPSID;

	if ( screenType == 0 )
	{
		splashScreen( sidekick_launch_oled );
	} else
	if ( screenType == 1 )
	{
		char fn[ 1024 ];
		// attention: this assumes that the filename ending is always ".crt"!
		memset( fn, 0, 1024 );
		strncpy( fn, FILENAME, strlen( FILENAME ) - 4 );
		strcat( fn, ".tga" );

		logger->Write( "RaspiFlash", LogNotice, "trying to load: '%s'", fn );

		if ( tftLoadBackgroundTGA( (char*)DRIVE, fn ) )
		{
			tftCopyBackground2Framebuffer();
		} else
		{
			tftLoadBackgroundTGA( DRIVE, FILENAME_SPLASH_RGB, 8 );

			int w, h; 
			extern char FILENAME_LOGO_RGBA[128];
			extern unsigned char tempTGA[ 256 * 256 * 4 ];

			if ( tftLoadTGA( DRIVE, FILENAME_LOGO_RGBA, tempTGA, &w, &h, true ) )
			{
				tftBlendRGBA( tempTGA, tftBackground, 0 );
			}

			tftCopyBackground2Framebuffer();
		}

		tftInitImm();
		tftSendFramebuffer16BitImm( tftFrameBuffer );
	}
	#endif

	// read launch code and .PRG
	u32 size;
	if ( c128PRG )
		readFile( logger, (char*)DRIVE, (char*)FILENAME_CBM128, launchCode, &size ); else
		readFile( logger, (char*)DRIVE, (char*)FILENAME_CBM80, launchCode, &size );

	#ifdef COMPILE_MENU
	if ( !hasData )
	{
		readFile( logger, (char*)DRIVE, (const char*)FILENAME, prgData, &prgSize );
	} else
	{
		prgSize = prgSizeExt;
		memcpy( prgData, prgDataExt, prgSize );
	}
	#else
	readFile( logger, (char*)DRIVE, (const char*)FILENAME, prgData, &prgSize );
	#endif

	startAddr = prgData[ 0 ] + prgData[ 1 ] * 256;
	endAddr = startAddr + prgSize - 2;
	prgSizeBelowA000 = 0xa000 - startAddr;
	if ( prgSizeBelowA000 > prgSize - 2 )
	{
		prgSizeBelowA000 = prgSize - 2;
		prgSizeAboveA000 = 0;
	} else
		prgSizeAboveA000 = prgSize - prgSizeBelowA000;

	resetFromCodeState = 0;

	#ifdef WITH_NET
	swiftLinkEnabled = false; //too early to enable here
	swiftLinkByte = 0;
	swiftLinkResponse = 0;
	swiftLinkDoNMI = 0;
	#ifdef SW_DEBUG
	swiftLinkCounter = 0;
	swiftLinkLog[0] = '\0';
	swiftLinkDataReads = 0;
	//swiftLinkReceivedCounter = 0;
	//swiftLinkReceived[0] = '\0';
	#endif
	swiftLinkRegisterCmd = 0;
	swiftLinkRegisterCtrl = 0;
	swiftLinkBaud = 0;
	swiftLinkBaudOld = 0;
	
	unsigned keepNMILow = 0;
	//bool isDoubleDirect = false;
  unsigned swiftLinkNmiDelay = 10;
	//TODO: for WLAN, test swiftLinkNmiDelay = 20000 * 2400 / 4800;

	//bool oldConnectState = false;
	bool firstEntry = false;
	swiftLinknetDelayDMA = swiftLinknetDelayDMADefault;


	pSidekickNet->setCurrentKernel( (char*)"l" );
	unsigned netDelay = _playingPSID ? 90000000: 3500000; //TODO: improve this
	unsigned followUpDelay = (pSidekickNet->getModemEmuType() == 1) ? 300000 : 300; //(_playingPSID ? 300: 300);


	if ( pSidekickNet->usesWLAN() ) followUpDelay = 10 * followUpDelay; //TODO
	#endif

	// setup FIQ
	prepareOnReset();
	DisableIRQs();
	m_InputPin.ConnectInterrupt( FIQ_HANDLER, FIQ_PARENT );
	m_InputPin.EnableInterrupt ( GPIOInterruptOnRisingEdge );

// this code is in here twice?
	c64CycleCount = resetCounter = 0;
	disableCart = transferStarted = currentOfs = 0;
	transferPart = 1;

	// warm caches
	prepareOnReset( true );
	prepareOnReset( false );

	//BEGIN_CYCLE_COUNTER


	// ready to go
	latchSetClear( LATCH_RESET, 0 );

	c64CycleCount = resetCounter = 0;
	disableCart = transferStarted = currentOfs = 0;
	transferPart = 1;
	CACHE_PRELOADL2KEEP( &prgData[ prgSizeBelowA000 + 2 ] );
	CACHE_PRELOADL2KEEP( &prgData[ 0 ] );

	nBytesRead = 0; stage = 1;
	u32 cycleCountC64_Stage1 = 0;
	#ifdef WITH_NET
	pSidekickNet->setCurrentKernel( (char*)"l" );
	unsigned netDelay = _playingPSID ? 900000000: 300; //TODO: improve this
	#endif

	// wait forever
	while ( true )
	{
		if ( !disableCart && !c128PRG )
		{
			if ( ( ( stage == 1 ) && nBytesRead > 0 ) ||
 				 ( ( stage == 2 ) && nBytesRead < 32 && c64CycleCount - cycleCountC64_Stage1 > 500000 ) )
			{
				if ( stage == 1 ) 
				{ 
					stage = 2; cycleCountC64_Stage1 = c64CycleCount; 
				} else 
				{
					stage = 0;
					latchSetClear( 0, LATCH_RESET );
					DELAY(1<<11);
					latchSetClear( LATCH_RESET, 0 );
					nBytesRead = 0; stage = 1;
					c64CycleCount = 0;
				}
			}
		}

		#ifdef WITH_NET

		if ( keepNMILow > 0 ){
				keepNMILow --;
				if ( keepNMILow == 0 )
					SET_GPIO( bNMI );
		};
		
		#endif
		
		#ifdef COMPILE_MENU
		TEST_FOR_JUMP_TO_MAINMENU( c64CycleCount, resetCounter )

		if ( resetFromCodeState == 2 )
		{
			EnableIRQs();
			m_InputPin.DisableInterrupt();
			m_InputPin.DisconnectInterrupt();
			return;		
		}
		#endif


		#ifdef WITH_NET
		if ( pSidekickNet->IsRunning() )
		{
			//bool swiftLinkDirectNetAccess = false;
			
			if (disableCart && netDelay > 0 )
			{
				if(
					(_playingPSID && transferStarted) ||
					(pSidekickNet->getModemEmuType() == 1 && !swiftLinkEnabled) ||
					pSidekickNet->getModemEmuType() != 1
				){
					netDelay--;
				}
			}
/*			
			swiftLinkDirectNetAccess = 
				swiftLinkEnabled &&
				//pSidekickNet->getModemEmuType() == 1 && 
				pSidekickNet->isModemSocketConnected() &&
				//!isDoubleDirect &&
				(swiftLinkResponse == 0) && //there is no char prepared to be sent to frontend
				(!pSidekickNet->areCharsInInputBuffer() || pSidekickNet->areCharsInOutputBuffer());
*/				

			if ( swiftLinkEnabled && !pSidekickNet->usesWLAN() && pSidekickNet->isModemSocketConnected() && swiftLinkResponse == 0 && swiftLinknetDelayDMA > 0)
				swiftLinknetDelayDMA--;

			if (swiftLinkReleaseDMA) //swiftLinkDirectNetAccess || 
			{
				netDelay = 0;
				//isDoubleDirect = true;
			}
				
			if (netDelay == 0 )
			{

				
				netDelay = followUpDelay;
				m_InputPin.DisableInterrupt();
				m_InputPin.DisconnectInterrupt();
				/*
				RESTART_CYCLE_COUNTER
				WAIT_UP_TO_CYCLE( WAIT_TRIGGER_DMA );
				CLR_GPIO( bDMA );
				*/
				EnableIRQs();
				
				if (!firstEntry)
				{
					if (_playingPSID)
						logger->Write( "sk", LogNotice, "firstEntry playing PSID");
					else
						logger->Write( "sk", LogNotice, "firstEntry");
					firstEntry = true;
				}
				
				if (pSidekickNet->getModemEmuType() == 1) //swiftlink emulation is configured by user
				{
					swiftLinknetDelayDMA = swiftLinknetDelayDMADefault; //reset to high value
					
					if ( !swiftLinkEnabled )
					{
						logger->Write( "sk", LogNotice, "swiftLink handling unlocked");
						swiftLinkEnabled = true;
					}
					else if (swiftLinkReleaseDMA)
					{
						//logger->Write( "sk", LogNotice, "DMA is pulled");
					}
					//else
					//	logger->Write( "sk", LogNotice, "netdelay is zero, received count = %i, sw data reads = %i", swiftLinkReceivedCounter, swiftLinkDataReads);

				

					
					#ifdef SW_DEBUG

					if ( swiftLinkCounter > 0 && swiftLinkCounter <200)
					{
						logger->Write( "sk", LogNotice, "swiftLinkLog: '%s', (%i)",swiftLinkLog, swiftLinkCounter );
	 					swiftLinkCounter = 0;
						swiftLinkLog[0] = '\0';
					}
					else if ( swiftLinkCounter > 0 && swiftLinkCounter >=200)
					{
						logger->Write( "sk", LogNotice, "swiftLinkLog: bla (%i)", swiftLinkCounter );
						//logger->Write( "sk", LogNotice, "swiftLinkLog: '%s', (%i)",swiftLinkLog, swiftLinkCounter );
						swiftLinkCounter = 0;
						swiftLinkLog[0] = '\0';
					}
					#endif
					
					
					if  ( swiftLinkEnabled && swiftLinkBaud != swiftLinkBaudOld)
					{
						swiftLinkBaudOld = swiftLinkBaud;
						unsigned baud = 0;
						switch ( swiftLinkBaud )
						{
							case  0: baud = 99999;	break; //enable enhanced speed!
							case  5: baud = 300;	break;
							case  6: baud = 600;	break;
							case  7: baud = 1200;	break;
							case  8: baud = 2400;	break;
							case  9: baud = 3300;	break;
							case 10: baud = 4800;	break;
							case 11: baud = 7200;	break;
							case 12: baud = 9600;	break;
							case 13: baud = 1440;	break;
							case 14: baud = 19200;	break;
							case 15: baud = 38400;	break;
							default: baud = 77777; break;
						}
						//if ( baud <= 4800)
							swiftLinkNmiDelay = 20000 * 2400 / baud;
						logger->Write( "sk", LogNotice, "swiftLinkBaud change to: %i, nmi delay = %i", baud, swiftLinkNmiDelay);	
					}
				}

				if ( pSidekickNet->isReturnToMenuRequired())
					return;
				
				kernelMenu->updateSystemMonitor();
				/*
				if ( swiftLinkEnabled && swiftLinkReleaseDMA ) //swiftLinkDirectNetAccess ) // && !pSidekickNet->usesWLAN())
				{
					//logger->Write( "sk", LogNotice, "swiftLinkDirectNetAccess");	
					pSidekickNet->handleModemEmulation( false );
				}
				else
				{
				*/
					pSidekickNet->handleQueuedNetworkAction();
					//isDoubleDirect = false;
				//}

//				if ( swiftLinkEnabled && swiftLinkReleaseDMA )
//					logger->Write( "sk", LogNotice, "end of netloop - releasing DMA soon");
//				else
//					logger->Write( "sk", LogNotice, "end of netloop");


				// warm caches
				prepareOnReset( true );
				DELAY(1<<18);
				prepareOnReset( true );
				DELAY(1<<18);
				prepareOnReset( true );
				
				DisableIRQs();
				
				m_InputPin.ConnectInterrupt( FIQ_HANDLER, FIQ_PARENT );
				m_InputPin.EnableInterrupt( GPIOInterruptOnRisingEdge );
				if (swiftLinkEnabled && swiftLinkReleaseDMA)
				{
					swiftLinkReleaseDMA = false;
					SET_GPIO( bDMA );
//					clrLatchFIQ( LATCH_LED0 );
					//FINISH_BUS_HANDLING
				}

			} //end of netdelay
			
			else if (swiftLinkEnabled && pSidekickNet->getModemEmuType() == 1 && !pSidekickNet->isModemSocketConnected())
			{
				//oldConnectState = pSidekickNet->isModemSocketConnected();
				//logger->Write( "sk", LogNotice, "handle modem emu without connection");
			 	pSidekickNet->handleModemEmulation( true ); //silent - without network access
			}
			
			if (swiftLinkEnabled && 
				keepNMILow == 0 && 
				swiftLinkDoNMI == 0  
			){
				if ( swiftLinkResponse == 0 )
				{
					if ( pSidekickNet->areCharsInInputBuffer())
					{
					unsigned char tmpOutput = pSidekickNet->getCharFromInputBuffer();
					if ( tmpOutput > 0)
					{
						swiftLinkResponse = tmpOutput;
//						swiftLinkReceived[swiftLinkReceivedCounter++] = tmpOutput;
//						swiftLinkReceived[swiftLinkReceivedCounter] = '\0';
						swiftLinkDoNMI = swiftLinkNmiDelay;
					}
				}
				}
				else
					swiftLinkDoNMI = swiftLinkNmiDelay;
			}

			if ( swiftLinkDoNMI > 0 ){
				/*if (swiftLinkResponse == 0)
				{
					swiftLinkDoNMI = 0; // turn it off
				}	
				else */
				if ( swiftLinkDoNMI > 1)
					swiftLinkDoNMI--;
					//check for disabled receive interrupts
				if ( (swiftLinkRegisterCmd & 1) != 0 && swiftLinkDoNMI == 1)
//				if ( swiftLinkDoNMI == 1)
				{
						swiftLinkDoNMI = 0;
						keepNMILow = ( pSidekickNet->usesWLAN() ? 20 : 4);
						CLR_GPIO( bNMI );
				}
			}

		}
		#endif

		asm volatile ("wfi");
		
	}

	// and we'll never reach this...
	m_InputPin.DisableInterrupt();
}


#ifdef COMPILE_MENU
void KernelLaunchFIQHandler( void *pParam )
#else
void CKernelLaunch::FIQHandler (void *pParam)
#endif
{
	register u32 D;

	// after this call we have some time (until signals are valid, multiplexers have switched, the RPi can/should read again)
	START_AND_READ_ADDR0to7_RW_RESET_CS

	// update some counters
	UPDATE_COUNTERS_MIN( c64CycleCount, resetCounter )

	// read the rest of the signals
	WAIT_AND_READ_ADDR8to12_ROMLH_IO12_BA

	if ( resetCounter > 3 && resetFromCodeState != 2 )
	{
		disableCart = transferStarted = 0;
		nBytesRead = 0; stage = 1;
		SETCLR_GPIO( configGAMEEXROMSet | bNMI, configGAMEEXROMClr );
		FINISH_BUS_HANDLING
		return;
	}

	if ( disableCart )
	{
		if ( _playingPSID )
		{
			if ( IO2_ACCESS && CPU_READS_FROM_BUS && GET_IO12_ADDRESS == 0x55 )
			{
				static u32 oc = 0;
				extern unsigned char charset[ 4096 ];
				u32 D = charset[ 2048 + oc ];
				oc ++; oc &= 1023;
				WRITE_D0to7_TO_BUS( D )
				CACHE_PRELOADL2STRM( &charset[ 2048 + oc ] );
			}
			if ( resetFromCodeState == 0 && IO2_ACCESS && CPU_WRITES_TO_BUS && GET_IO12_ADDRESS == 0x11 )
			{
				READ_D0to7_FROM_BUS( D )
				if ( D == 0x22 )
					resetFromCodeState = 1;
			}
			if ( resetFromCodeState == 1 && IO2_ACCESS && CPU_WRITES_TO_BUS && GET_IO12_ADDRESS == 0x33 )
			{
				READ_D0to7_FROM_BUS( D )
				if ( D == 0x44 )
				{
					resetFromCodeState = 2;
					latchSetClear( 0, LATCH_RESET );
				}
			}
			OUTPUT_LATCH_AND_FINISH_BUS_HANDLING
			return;
		}
		#ifdef WITH_NET
		else if ( swiftLinkEnabled )
		{
			if ( IO1_ACCESS && CPU_READS_FROM_BUS ) // && (GET_IO12_ADDRESS >= 0x00 && GET_IO12_ADDRESS <= 0x03))
			{
				u32 D = 0;
				bool hasReadByte = false;
				if ( GET_IO12_ADDRESS == 0x01){ //status register
					if ( swiftLinkResponse == 0)
						D = 16 + 32; //set transmit flag ( +DSR), show that we are ready to get next byte from C64
					else
						D = 8; //8; //set data register flag, show c64 that it may now pick up a byte
				}
				else if ( GET_IO12_ADDRESS == 0x00) //data register
				{ 
					hasReadByte = true;
					if ( swiftLinkResponse > 0)
					{
						D = swiftLinkResponse;
						swiftLinkResponse = 0;
					}
					else 
					  D = 72;//fake echo
					#ifdef SW_DEBUG
					swiftLinkDataReads++;
					#endif
				}
				if ( GET_IO12_ADDRESS == 0x03) //control register
				{ 
					D = swiftLinkRegisterCtrl;
				}
				else if ( GET_IO12_ADDRESS == 0x02) // command register
				{
					D = swiftLinkRegisterCmd;
				}
				WRITE_D0to7_TO_BUS( D )

				if ( pSidekickNet->isModemSocketConnected() && 
				 	(!pSidekickNet->areCharsInInputBuffer() // hasReadByte &&  
					||	swiftLinknetDelayDMA < 1 )
					//swiftLinkResponse == 0 && //there is no char prepared to be sent to frontend
				){
					//FINISH_BUS_HANDLING
					WAIT_UP_TO_CYCLE( WAIT_TRIGGER_DMA );
					CLR_GPIO( bDMA );
					//setLatchFIQ( LATCH_LED0 );
					FINISH_BUS_HANDLING
					swiftLinknetDelayDMA = swiftLinknetDelayDMADefault;
					swiftLinkReleaseDMA = true;
					return;
				}
				
/*				
				if ( !hasReadByte && pSidekickNet->isModemSocketConnected() &&
					(swiftLinkResponse == 0) && //there is no char prepared to be sent to frontend
					!pSidekickNet->areCharsInInputBuffer()
				){
					WAIT_UP_TO_CYCLE( WAIT_TRIGGER_DMA );
					CLR_GPIO( bDMA );
					setLatchFIQ( LATCH_LED0 );
					FINISH_BUS_HANDLING
					swiftLinkReleaseDMA = true;
					return;
				}
	*/			
/*				
				#ifdef SW_DEBUG
				unsigned hh = D/16, hl = D % 16;
				
				hh = hh < 10 ? hh + 48 : hh+55;
				hl = hl < 10 ? hl + 48 : hl+55;
				
				if ( swiftLinkCounter + 20 < swiftLinkLogLengthMax)
				{
					swiftLinkLog[swiftLinkCounter++] = GET_IO12_ADDRESS + 48;
					swiftLinkLog[swiftLinkCounter++] = 'r';
					swiftLinkLog[swiftLinkCounter++] = hh;
					swiftLinkLog[swiftLinkCounter++] = hl;
					swiftLinkLog[swiftLinkCounter++] = '/';
					swiftLinkLog[swiftLinkCounter] = '\0';
				}
				#endif
				//FINISH_BUS_HANDLING
*/				
			}
			else if ( IO1_ACCESS && CPU_WRITES_TO_BUS  )
			{
				READ_D0to7_FROM_BUS( D )
				
				unsigned hh = D/16, hl = D % 16;
				
				#ifdef SW_DEBUG
				unsigned hh2 = hh < 10 ? hh + 48 : hh+55;
				#endif
				unsigned hl2 = hl < 10 ? hl + 48 : hl+55;
/*				
				#ifdef SW_DEBUG
				
				boolean doLog = swiftLinkCounter + 20 < swiftLinkLogLengthMax;
				if (doLog){
					swiftLinkLog[swiftLinkCounter++] = GET_IO12_ADDRESS +48;
					swiftLinkLog[swiftLinkCounter++] = 'w';
					swiftLinkLog[swiftLinkCounter++] = hh2;
					swiftLinkLog[swiftLinkCounter++] = hl2;
				}
				#endif
*/				
				if ( GET_IO12_ADDRESS == 0x03) //control register
				{ 
					swiftLinkBaud = hl;
					swiftLinkRegisterCtrl = D;
				}
				else if ( GET_IO12_ADDRESS == 0x02) //command register
				{ 
					swiftLinkRegisterCmd = D;
				}
				else if ( GET_IO12_ADDRESS == 0x00) //data register
				{
					swiftLinkByte = D;
					pSidekickNet->addToModemOutputBuffer( D );
					//pSidekickNet->handleModemEmulation( true );
					if ( pSidekickNet->isModemSocketConnected() || D == 13)
					{
						WAIT_UP_TO_CYCLE( WAIT_TRIGGER_DMA );
						CLR_GPIO( bDMA );
						//setLatchFIQ( LATCH_LED0 );
						FINISH_BUS_HANDLING
						swiftLinkReleaseDMA = true;
						swiftLinknetDelayDMA = swiftLinknetDelayDMADefault;
						return;
					}
				}

				#ifdef SW_DEBUG
				if (doLog){
					swiftLinkLog[swiftLinkCounter++] = '/';
					swiftLinkLog[swiftLinkCounter] = '\0';
				}
				#endif
				
				if ( pSidekickNet->isModemSocketConnected() &&
					(swiftLinkResponse == 0) && //there is no char prepared to be sent to frontend
					!pSidekickNet->areCharsInInputBuffer() && 
					swiftLinknetDelayDMA < 1
				){
					WAIT_UP_TO_CYCLE( WAIT_TRIGGER_DMA );
					CLR_GPIO( bDMA );
					//setLatchFIQ( LATCH_LED0 );
					FINISH_BUS_HANDLING
					swiftLinknetDelayDMA = swiftLinknetDelayDMADefault;
					swiftLinkReleaseDMA = true;
					return;
				}
				
				//FINISH_BUS_HANDLING
			}
			
			else if ( IO2_ACCESS && CPU_READS_FROM_BUS ) // && (GET_IO12_ADDRESS >= 0x00 && GET_IO12_ADDRESS <= 0x03))
			{
				u32 D = 0;
				WRITE_D0to7_TO_BUS( D )
				//FINISH_BUS_HANDLING
			}
			else if ( IO2_ACCESS && CPU_WRITES_TO_BUS ) //&& (GET_IO12_ADDRESS >= 0x00 && GET_IO12_ADDRESS <= 0x03))
			{
				u32 D = 0;
				READ_D0to7_FROM_BUS( D )
				//FINISH_BUS_HANDLING
			}
			//return;
		}//end of swiftlink enabled
		#endif
	}

	#ifdef WITH_NET
 	if ( swiftLinkEnabled )
	{ 
		FINISH_BUS_HANDLING
		return;
	}
 	#endif

	// access to CBM80 ROM (launch code)
	if ( CPU_READS_FROM_BUS && ACCESS( ROM_LH ) )
	{
		WRITE_D0to7_TO_BUS( launchCode[ GET_ADDRESS + LAUNCH_BYTES_TO_SKIP ] );
		nBytesRead++;
	}

	if ( !disableCart && IO1_ACCESS ) 
	{
		if ( CPU_WRITES_TO_BUS ) 
		{
			transferStarted = 1;

			// any write to IO1 will (re)start the PRG transfer
			if ( GET_IO12_ADDRESS == 2 )
			{
				currentOfs = prgSizeBelowA000 + 2;
				transferPart = 1; 
				CACHE_PRELOADL2KEEP( &prgData[ prgSizeBelowA000 + 2 ] );
				FINISH_BUS_HANDLING
				forceReadLaunch = prgData[ prgSizeBelowA000 + 2 ];
			} else
			{
				currentOfs = 0;
				transferPart = 0;
				CACHE_PRELOADL2KEEP( &prgData[ 0 ] );
				FINISH_BUS_HANDLING
				forceReadLaunch = prgData[ 0 ];
			}
			return;
		} else
		// if ( CPU_READS_FROM_BUS ) 
		{
			if ( GET_IO12_ADDRESS == 1 )	
			{
				// $DE01 -> get number of 256-byte pages
				if ( transferPart == 1 ) // PRG part above $a000
					D = ( prgSizeAboveA000 + 255 ) >> 8;  else
					D = ( prgSizeBelowA000 + 255 ) >> 8; 
				WRITE_D0to7_TO_BUS( D )
				CACHE_PRELOADL2KEEP( &prgData[ currentOfs ] );
				FINISH_BUS_HANDLING
				forceReadLaunch = prgData[ currentOfs ];
			} else
			if ( GET_IO12_ADDRESS == 4 ) // full 256-byte pages 
			{
				D = ( prgSize - 2 ) >> 8;
				WRITE_D0to7_TO_BUS( D )
				CACHE_PRELOADL2KEEP( &prgData[ currentOfs ] );
				FINISH_BUS_HANDLING
				forceReadLaunch = prgData[ currentOfs ];
			} else
			if ( GET_IO12_ADDRESS == 5 ) // bytes on last non-full 256-byte page
			{
				D = ( prgSize - 2 ) & 255;
				WRITE_D0to7_TO_BUS( D )
				CACHE_PRELOADL2KEEP( &prgData[ currentOfs ] );
				FINISH_BUS_HANDLING
				forceReadLaunch = prgData[ currentOfs ];
			} else
			if ( GET_IO12_ADDRESS == 2 )	
			{
				// $DE02 -> get BASIC end address
				WRITE_D0to7_TO_BUS( (u8)( endAddr & 255 ) )
				FINISH_BUS_HANDLING
			} else
			if ( GET_IO12_ADDRESS == 3 )	
			{
				// $DE02 -> get BASIC end address
				WRITE_D0to7_TO_BUS( (u8)( (endAddr>>8) & 255 ) )
				FINISH_BUS_HANDLING
			} else
			{
				// $DE00 -> get next byte
/*				if ( transferPart == 1 ) // PRG part above $a000
				{
					//D = prgData[ prgSizeBelowA000 + 2 + currentOfs++ ]; 
					D = forceReadLaunch;	currentOfs ++;
					//D = (currentOfs ++) & 255;
					WRITE_D0to7_TO_BUS( D )
					CACHE_PRELOADL2KEEP( &prgData[ prgSizeBelowA000 + 2 + currentOfs ] );
					FINISH_BUS_HANDLING
					forceReadLaunch = prgData[ prgSizeBelowA000 + 2 + currentOfs ];
				} else*/
				{
					D = forceReadLaunch;	currentOfs ++;
					//D = prgData[ currentOfs++ ];
					WRITE_D0to7_TO_BUS( D )
					CACHE_PRELOADL2KEEP( &prgData[ currentOfs ] );
					FINISH_BUS_HANDLING
					forceReadLaunch = prgData[ currentOfs ];
				}
			}
				
			return;
		}
	}

	if ( !disableCart && CPU_WRITES_TO_BUS && IO2_ACCESS ) // writing #123 to $df00 (IO2) will disable the cartridge
	{
		READ_D0to7_FROM_BUS( D )

		if ( GET_IO12_ADDRESS == 0 && D == 123 )
		{
			disableCart = 1;
			SET_GPIO( bGAME | bEXROM | bNMI );
			FINISH_BUS_HANDLING
			return;
		}
	}

	OUTPUT_LATCH_AND_FINISH_BUS_HANDLING
}
