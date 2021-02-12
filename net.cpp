/*
  _________.__    .___      __   .__        __        _________   ________   _____  
 /   _____/|__| __| _/____ |  | _|__| ____ |  | __    \_   ___ \ /  _____/  /  |  | 
 \_____  \ |  |/ __ |/ __ \|  |/ /  |/ ___\|  |/ /    /    \  \//   __  \  /   |  |_
 /        \|  / /_/ \  ___/|    <|  \  \___|    <     \     \___\  |__\  \/    ^   /
/_______  /|__\____ |\___  >__|_ \__|\___  >__|_ \     \______  /\_____  /\____   | 
        \/         \/    \/     \/       \/     \/            \/       \/      |__| 
 
 net.cpp

 RasPiC64 - A framework for interfacing the C64 and a Raspberry Pi 3B/3B+
          - misc code
 Copyright (c) 2019, 2020 Carsten Dachsbacher <frenetic@dachsbacher.de>

 Network related code in this file contributed by Henning Pingel based on 
 the networking examples within Rene Stanges Circle framework.

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

#include "net.h"
#include "helpers.h"

#ifndef IS264
#include "c64screen.h"
#include "config.h"
#include "PSID/psid64/psid64.h"
#else
#include "264screen.h"
#include "264config.h"
#endif


#include <circle/net/dnsclient.h>
#include <circle/net/ntpclient.h>
#ifdef WITH_TLS
#include <circle-mbedtls/httpclient.h>
#else
#include <circle/net/httpclient.h>
#endif

#ifdef WITH_TLS
#include <circle-mbedtls/tlssimpleclientsocket.h>
#endif
#include <circle/net/in.h>
#include <circle/types.h>
#include <ctype.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#ifdef WITH_TLS
#include <mbedtls/error.h>
#endif

// Network configuration
#ifndef WITH_WLAN
#define USE_DHCP
#endif
// Time configuration
static const unsigned bestBefore = 1614556800;
static const char NTPServer[]    = "pool.ntp.org";
static const int nTimeZone       = 1*60;		// minutes diff to UTC
static const char DRIVE[] = "SD:";
//nDocMaxSize reserved 2 MB as the maximum size of the kernel file
static const unsigned nDocMaxSize = 2000*1024;
static const char msgNoConnection[] = "Sorry, no network connection!";
static const char msgNotFound[]     = "Message not found. :(";

static const char CSDB_HOST[] = "csdb.dk";

#ifdef WITH_WLAN
#define DRIVE		"SD:"
#define FIRMWARE_PATH	DRIVE "/firmware/"		// firmware files must be provided here
#define CONFIG_FILE	DRIVE "/wpa_supplicant.conf"
#endif

//temporary hack
extern u32 prgSizeLaunch;
extern unsigned char prgDataLaunch[ 1025*1024 ] AAA;
#ifdef WITH_RENDER
  extern unsigned char logo_bg_raw[32000];
#endif

CSidekickNet::CSidekickNet( CInterruptSystem * pInterruptSystem, CTimer * pTimer, CScheduler * pScheduler, CEMMCDevice * pEmmcDevice, CKernelMenu * pKernelMenu  )
		:m_USBHCI (0),
		m_pScheduler(pScheduler),
		m_pInterrupt(pInterruptSystem),
		m_pTimer(pTimer),
		m_EMMC( *pEmmcDevice),
		m_kMenu(pKernelMenu),
		m_Net (0),
#ifdef WITH_WLAN		
		m_WLAN (0),
		m_WPASupplicant (0),
		m_useWLAN (true),
#else
		m_useWLAN (false),
#endif
		m_DNSClient(0),
#ifdef WITH_TLS
		m_TLSSupport(0),
#endif
		m_WebServer(0),
		m_isFSMounted( false ),
		m_isActive( false ),
		m_isPrepared( false ),
		m_isUSBPrepared( false ),
		m_isNetworkInitQueued( false ),
		//m_isKernelUpdateQueued( false ),
		m_isFrameQueued( false ),
		m_isSktpKeypressQueued( false ),
		m_isCSDBDownloadQueued( false ),
		m_isCSDBDownloadSavingQueued( false ),
		m_isDownloadReady( false ),
		m_isDownloadReadyForLaunch( false ),
		m_isRebootRequested( false ),
		m_isReturnToMenuRequested( false ),
		m_networkActionStatusMsg( (char * ) ""),
		m_sktpScreenContent( (unsigned char * ) ""),
		m_sktpSessionID( (char * ) ""),
		m_isSktpScreenActive( false ),
		m_isMenuScreenUpdateNeeded( false ),
		m_CSDBDownloadPath( (char * ) ""),
		m_CSDBDownloadExtension( (char * ) ""),
		m_CSDBDownloadFilename( (char * ) ""),
		m_CSDBDownloadSavePath( (char *)"" ),
		m_bSaveCSDBDownload2SD( false ),
		m_PiModel( m_pMachineInfo->Get()->GetMachineModel () ),
		//m_SidekickKernelUpdatePath(0),
		m_queueDelay(0),
		m_timestampOfLastWLANKeepAlive(0),
		m_timeoutCounterStart(0),
		m_skipSktpRefresh(0),
		m_sktpScreenPosition(0),
		m_sktpResponseLength(0),
		m_sktpResponseType(0),
		m_sktpKey(0),
		m_sktpSession(0),
		m_videoFrameCounter(1),
		//m_sysMonInfo(""),
		m_sysMonHeapFree(0),
		m_sysMonCPUTemp(0),
		m_loglevel(2),
		m_currentKernelRunning((char *) "-"),
		m_oldSecondsLeft(0)
		
{
	assert (m_pTimer != 0);
	assert (& m_pScheduler != 0);

	//timezone is not really related to net stuff, it could go somewhere else
	m_pTimer->SetTimeZone (nTimeZone);
}

void CSidekickNet::setErrorMsgC64( char * msg, boolean sticky = true ){ 
	#ifndef WITH_RENDER
	setErrorMsg2( msg, sticky );
	m_isMenuScreenUpdateNeeded = true;
	#endif
};

/*
void CSidekickNet::setSidekickKernelUpdatePath( unsigned type)
{
	m_SidekickKernelUpdatePath = type;
};*/

boolean CSidekickNet::ConnectOnBoot (){
	return netConnectOnBoot;
}

boolean CSidekickNet::usesWLAN (){
	return m_useWLAN;
}

boolean CSidekickNet::Initialize()
{
	const unsigned sleepLimit = 100 * (m_useWLAN ? 10:1);
	unsigned sleepCount = 0;
	
	if (m_isActive)
	{
		if (m_loglevel > 1)
			logger->Write( "CSidekickNet::Initialize", LogNotice, 
				"Strange: Network is already up and running. Skipping another init."
			);
		return true;
	}
	if ( !m_isPrepared )
	{
		if (!Prepare()){
			return false;
		}
		m_isPrepared = true;
	}
	while (!m_Net->IsRunning () && sleepCount < sleepLimit)
	{
		m_USBHCI->UpdatePlugAndPlay ();
		m_pScheduler->Yield ();
		m_pScheduler->MsSleep(100);
		sleepCount ++;
	}

	if (!m_Net->IsRunning () && sleepCount >= sleepLimit){
		if ( m_useWLAN )
		{
			if (m_loglevel > 1)
				logger->Write( "CSidekickNet::Initialize", LogNotice, 
					"WLAN connection can't be established - maybe poor reception?"
				);
			//                    "012345678901234567890123456789012345XXXX"
			setErrorMsgC64((char*)"   Wireless network connection failed!  ");
		}
		else{
			if (m_loglevel > 1)
				logger->Write( "CSidekickNet::Initialize", LogNotice, 
					"Network connection is not running - is ethernet cable not attached?"
				);
			//                    "012345678901234567890123456789012345XXXX"
			if ( !netConnectOnBoot)
				setErrorMsgC64((char*)"  Cable plugged in? Check and reconnect ");
		}
		return false;
	}

	//net connection is up and running now
	m_isActive = true;

	m_DNSClient = new CDNSClient (m_Net);

#ifdef WITH_TLS
	m_TLSSupport = new CTLSSimpleSupport (m_Net);
#endif

	//TODO: the resolves could be postponed to the moment where the first 
	//actual access takes place
	
	m_CSDB.hostName = CSDB_HOST;
	m_CSDB.port = 443;
	m_CSDB.ipAddress = getIPForHost( CSDB_HOST );
	m_CSDB.logPrefix = getLoggerStringForHost( CSDB_HOST, 443);

	m_NTPServerIP  = getIPForHost( NTPServer );

	if (strcmp(netSktpHostName,"") != 0)
	{
		int port = netSktpHostPort != 0 ? netSktpHostPort: HTTP_PORT;
		m_SKTPServer.hostName = netSktpHostName;
		m_SKTPServer.port = port;
		m_SKTPServer.ipAddress = getIPForHost( netSktpHostName );
		m_SKTPServer.logPrefix = getLoggerStringForHost( netSktpHostName, port);
	}
	else
	{
		m_SKTPServer.hostName = "";
		m_SKTPServer.port = 0;
	}
	
	if ( netEnableWebserver ){
		EnableWebserver();
	}
				
	#ifndef WITH_RENDER
	 m_isMenuScreenUpdateNeeded = true;
	 clearErrorMsg(); //on c64screen, kernel menu
  #endif
	return true;
}

void CSidekickNet::EnableWebserver(){
	if (m_loglevel > 1)
		logger->Write ("CSidekickNet::Initialize", LogNotice, "Starting webserver.");
	m_WebServer = new CWebServer (m_Net, 80, KERNEL_MAX_SIZE + 2000, 0, this);
}


CString CSidekickNet::getLoggerStringForHost( CString hostname, int port){
	CString s = "http";
	s.Append( (port == 443) ? "s://" : "://" );
	s.Append(	hostname );
	if ( port != 443 && port != 80 )
	{
		CString Number;
		Number.Format ("%02d", port);
		s.Append(":");
		s.Append(Number);
	}
	s.Append("\%s");
	if (m_loglevel > 2)
		logger->Write ("getLoggerStringForHost", LogNotice, s, "/dummyPath");
	
	return s;	
}

boolean CSidekickNet::mountSDDrive()
{
	if ( m_isFSMounted )
	{
		if (m_loglevel > 1)
			logger->Write ("CSidekickNet::mountSDDrive", LogError,
					"Drive %s is already mounted. Skip remount.", DRIVE);
		return true;
	}
	if (f_mount (&m_FileSystem, DRIVE, 1) != FR_OK)
	{
		if (m_loglevel > 1)
			logger->Write ("CSidekickNet::Initialize", LogError,
					"Cannot mount drive: %s", DRIVE);
		m_isFSMounted = false;
	}
	else
		m_isFSMounted = true;
	return m_isFSMounted;
}

boolean CSidekickNet::unmountSDDrive()
{
	if (f_mount ( 0, DRIVE, 0) != FR_OK)
	{
		if (m_loglevel > 1)
			logger->Write ("CSidekickNet::unmountSDDrive", LogError,
					"Cannot unmount drive: %s", DRIVE);
		return false;
	}
	m_isFSMounted = false;
	return true;
}		

boolean CSidekickNet::RaspiHasOnlyWLAN()
{
	return (m_PiModel == MachineModel3APlus );
}

void CSidekickNet::checkForSupportedPiModel()
{
	if ( m_PiModel != MachineModel3APlus && m_PiModel != MachineModel3BPlus)
	{
		if (m_loglevel > 1)
			logger->Write( "CSidekickNet::Initialize", LogWarning, 
				"Warning: The model of Raspberry Pi you are using is not a model supported by Sidekick64/264!"
			);
	}
	if ( RaspiHasOnlyWLAN() && !m_useWLAN )
	{
		if (m_loglevel > 1)
			logger->Write( "CSidekickNet::Initialize", LogNotice, 
				"Your Raspberry Pi model (3A+) doesn't have an ethernet socket. This kernel is built for cable based network. Use WLAN kernel instead."
			);
	}
}

boolean CSidekickNet::isReturnToMenuRequired(){
	boolean result =  m_isRebootRequested || m_isReturnToMenuRequested;
	m_isReturnToMenuRequested = false;
	return result;
}

boolean CSidekickNet::isRebootRequested(){
	if (!m_isRebootRequested)
		return false;
	unsigned waitDuration = 5;
	signed secondsLeft = waitDuration - (m_pTimer->GetUptime() - m_timeoutCounterStart);
	if ( secondsLeft < 0 ) secondsLeft = 0;
	if ( secondsLeft <= 0){
		//unmountSDDrive(); //TODO: check if this needs FIQ to be off!
		return true;
	}
	else
	{
		if ( m_oldSecondsLeft != secondsLeft ){
			CString msg = "  Please wait, rebooting Sidekick in ";
			CString Number;
			Number.Format ("%01d", secondsLeft-2 > 0 ? secondsLeft-2 : 0 );
			msg.Append( Number );
			msg.Append("  ");
			const char * tmp = msg;
			setErrorMsgC64( (char*) tmp );
			m_isMenuScreenUpdateNeeded = true;// to update the waiting message instantly
			m_oldSecondsLeft = secondsLeft;
		}
		return false;
	}
}

void CSidekickNet::requestReboot(){
	m_timeoutCounterStart = m_pTimer->GetUptime();
	m_isRebootRequested = true;
	//setErrorMsgC64( (char*)"   Please wait, rebooting Sidekick...   " );
}


boolean CSidekickNet::disableActiveNetwork(){
	//FIXME THIS DOES NOT WORK
	m_isActive = false;
	m_isUSBPrepared = false;
	m_isPrepared = false;
	/*
	logger->Write( "CSidekickNet", LogNotice, 
		"disableActiveNetwork: Now deleting instance of CTLSSimpleSupport."
	);
	logger->Write ("CSidekickNet", LogNotice, getSysMonInfo(1));
	delete m_TLSSupport;
	m_TLSSupport = 0;
	
	/*
	logger->Write( "CSidekickNet", LogNotice, 
		"disableActiveNetwork: Now deleting instance of CDNSClient."
	);
	logger->Write ("CSidekickNet", LogNotice, getSysMonInfo(1));
	
	delete m_DNSClient;
	m_DNSClient = 0;

	logger->Write( "CSidekickNet", LogNotice, 
		"disableActiveNetwork: Now deleting instance of CNetSubSystem."
	);
	logger->Write ("CSidekickNet", LogNotice, getSysMonInfo(1));
	delete m_Net;
	m_Net = 0;
	
	logger->Write( "CSidekickNet", LogNotice, 
		"disableActiveNetwork: Now deleting instance of CUSBHCIDevice."
	);
	logger->Write ("CSidekickNet", LogNotice, getSysMonInfo(1));
	delete m_USBHCI;
	//m_USBHCI = 0;
*/
	m_isActive = false;
	m_isUSBPrepared = false;
	m_isPrepared = false;
	/*
	logger->Write( "CSidekickNet", LogNotice, 
		"disableActiveNetwork: Setting to null."
	);
	
	m_TLSSupport = 0;
	m_DNSClient = 0;
	m_Net = 0;
	m_USBHCI = 0;
	*/
	logger->Write( "CSidekickNet", LogNotice, 
		"disableActiveNetwork: EOM"
	);
	return false;
}

CUSBHCIDevice * CSidekickNet::getInitializedUSBHCIDevice()
{
		if ( !m_isUSBPrepared)
		{
			if ( !initializeUSBHCIDevice() )
				return 0;
		}
		return m_USBHCI;
}

boolean CSidekickNet::initializeUSBHCIDevice()
{
	if ( !m_isUSBPrepared)
	{
		m_USBHCI = new CUSBHCIDevice (m_pInterrupt, m_pTimer, TRUE);
		if (!m_USBHCI->Initialize ())
		{
			logger->Write( "CSidekickNet", LogNotice, 
				"Couldn't initialize instance of CUSBHCIDevice."
			);
			setErrorMsgC64((char*)"Error on USB init. Sorry.");
			return false;
		}
		m_isUSBPrepared = true;
	}
	return true;
}

boolean CSidekickNet::Prepare()
{
	if ( RaspiHasOnlyWLAN() && !m_useWLAN )
	{
		logger->Write( "CSidekickNet::Initialize", LogNotice, 
			"Your Raspberry Pi model (3A+) doesn't have an ethernet socket. Skipping init of CNetSubSystem."
		);
		//                 "012345678901234567890123456789012345XXXX"
		setErrorMsgC64((char*)" No WLAN support in this kernel. Sorry. ");
		return false;
	}

	if ( !initializeUSBHCIDevice())
	{
		setErrorMsgC64((char*)"Error on USB init. Sorry.");
		return false;
	}
	
	if ( !m_isFSMounted && !mountSDDrive())
	{
		setErrorMsgC64((char*)"Can't mount SD card. Sorry.");
		return false;
	}	
	#ifndef WITHOUT_STDLIB
	CGlueStdioInit (m_FileSystem);
	#endif
	
	if (m_useWLAN)
	{
	#ifdef WITH_WLAN
		m_WLAN = new CBcm4343Device (FIRMWARE_PATH);
		if (!m_WLAN->Initialize ())
		{
			logger->Write( "CSidekickNet::Initialize", LogNotice, 
				"Couldn't initialize instance of WLAN."
			);
			return false;
		}
	#endif
	}
	if ( strcmp( netSidekickHostname, "") == 0 )
		strncpy( netSidekickHostname, "sidekick64", 255 );
	m_Net = new CNetSubSystem (0, 0, 0, 0, netSidekickHostname, m_useWLAN ? NetDeviceTypeWLAN : NetDeviceTypeEthernet );
	if (!m_Net->Initialize (false))
	{
		logger->Write( "CSidekickNet::Initialize", LogNotice, 
			"Couldn't initialize instance of CNetSubSystem."
		);
		setErrorMsgC64((char*)"Can't initialize CNetSubSystem.");
		return false;
	}
	if (m_useWLAN)
	{
	#ifdef WITH_WLAN
		m_WPASupplicant = new CWPASupplicant (CONFIG_FILE);
		if (!m_WPASupplicant->Initialize ())
		{
			logger->Write( "CSidekickNet::Initialize", LogNotice, 
				"Couldn't initialize instance of CWPASupplicant."
			);
			setErrorMsgC64((char*)"Can't initialize WLAN/WPA. Sorry.");
			return false;
		}
		#endif
	}
	return true;
}

boolean CSidekickNet::IsRunning ()
{
	 return m_isActive; 
}

boolean CSidekickNet::IsConnecting ()
{
	 return m_isNetworkInitQueued; 
}

void CSidekickNet::queueNetworkInit()
{ 
	m_isNetworkInitQueued = true;
	//                                 "012345678901234567890123456789012345XXXX"
	m_networkActionStatusMsg = (char*) "    Trying to connect. Please wait.     ";
	m_queueDelay = 1;
}

/*
void CSidekickNet::queueKernelUpdate()
{ 
	m_isKernelUpdateQueued = true; 
	//                                 "012345678901234567890123456789012345XXXX"
	m_networkActionStatusMsg = (char*) "  Trying to update kernel. Please wait. ";
	m_queueDelay = 1;
}*/

//this is for kernel render
void CSidekickNet::queueFrameRequest()
{
 	m_isFrameQueued = true;
	m_queueDelay = 0;
}

void CSidekickNet::queueSktpKeypress( int key )
{
	if ( !isAnyNetworkActionQueued())
		m_queueDelay = 0; //this could lead to disturbing an already running queuedelay
	m_isSktpKeypressQueued = true;
	m_sktpKey = key;
	//logger->Write ("CSidekickNet::queueSktpKeypress", LogNotice, "Queuing keypress");
}

void CSidekickNet::queueSktpRefresh( unsigned timeout )
{
	//refesh when user didn't press a key
	//this has to be quick for multiplayer games (value 4)
	//and can be slow for csdb browsing (value 16)
	m_skipSktpRefresh++;
	if ( timeout == 0 || (m_skipSktpRefresh > timeout && !isAnyNetworkActionQueued()))
	{
		m_skipSktpRefresh = 0;
		queueSktpKeypress( 92 );
		//m_isMenuScreenUpdateNeeded = true;
	}
}

char * CSidekickNet::getCSDBDownloadFilename(){
	return m_CSDBDownloadFilename;
}

u8 CSidekickNet::getCSDBDownloadLaunchType(){
	u8 type = 0;
	if ( strcmp( m_CSDBDownloadExtension, "crt") == 0 )
	{
		type = 11;
		if (m_loglevel > 2)
			logger->Write ("CSidekickNet::getCSDBDownloadLaunchType", LogNotice, "CRT detected: >%s<",m_CSDBDownloadExtension);
	}
	else if ( strcmp( m_CSDBDownloadExtension, "d64" ) == 0)
	{
#ifndef IS264		
		//with the new d2ef approach available within Sidekick we try to launch the D64
		//as an dynamically created EF crt!
		type = 11;
		extern int createD2EF( unsigned char *diskimage, int imageSize, unsigned char *cart, int build, int mode, int autostart );
		unsigned char *cart = new unsigned char[ 1024 * 1025 ];
		u32 crtSize = createD2EF( prgDataLaunch, prgSizeLaunch, cart, 2, 0, true );
		memcpy( &prgDataLaunch[0], cart, crtSize );
		prgSizeLaunch = crtSize;
		m_CSDBDownloadFilename = (char *)"SD:C64/temp.crt"; //this is only an irrelevant dummy name ending wih crt
#else
		type = 0; //unused, we only save the file (on Sidekick 264)
#endif
		if (m_loglevel > 2)
			logger->Write ("CSidekickNet::getCSDBDownloadLaunchType", LogNotice, "D64 detected: >%s<",m_CSDBDownloadExtension);
	}
	else if ( strcmp( m_CSDBDownloadExtension, "sid" ) == 0)
	{
#ifndef IS264
		//FIXME: This was just copied from c64screen.cpp. Put this into a method.
		Psid64 *psid64 = new Psid64();

		psid64->setVerbose(false);
		psid64->setUseGlobalComment(false);
		psid64->setBlankScreen(false);
		psid64->setNoDriver(false);

		if ( !psid64->load( prgDataLaunch, prgSizeLaunch ) )
		{
			//return false;
		}
		//logger->Write( "exec", LogNotice, "psid loaded" );

		// convert the PSID file
		if ( !psid64->convert() ) 
		{
			//return false;
		}
		//logger->Write( "exec", LogNotice, "psid converted, prg size %d", psid64->m_programSize );

		memcpy( &prgDataLaunch[0], psid64->m_programData, psid64->m_programSize );
		prgSizeLaunch = psid64->m_programSize;

		delete psid64;
		//FIXME: End of copy
		type = 41;
#else
		type = 0; //unused, we only save the file (on Sidekick 264)
#endif

		if (m_loglevel > 2)
			logger->Write ("CSidekickNet::getCSDBDownloadLaunchType", LogNotice, "SID detected: >%s<",m_CSDBDownloadExtension);
	}
	else //prg
	{
		type = 40;
		if (m_loglevel > 2)
			logger->Write ("CSidekickNet::getCSDBDownloadLaunchType", LogNotice, "PRG detected: >%s<",m_CSDBDownloadExtension);
	}
	return type;
}

void CSidekickNet::handleQueuedNetworkAction()
{
	if ( m_isActive && (!isAnyNetworkActionQueued() || !usesWLAN()) )
	{
		
		if ( m_WebServer == 0 && netEnableWebserver ){
			EnableWebserver();
		}
		
		//every 10 seconds + seconds needed for request
		//log cpu temp + uptime + free memory
		//in wlan case do keep-alive request
		if ( (m_pTimer->GetUptime() - m_timestampOfLastWLANKeepAlive) > (netEnableWebserver ? 1:10))
		{
			#ifdef WITH_WLAN
			if (!netEnableWebserver)
			{
				//Circle42 offers experimental WLAN, but it seems to
				//disconnect very quickly if there is no traffic.
				//This can be very annoying.
				//As a WLAN keep-alive, we auto queue a network event
				//to avoid WLAN going into "zombie" disconnected mode
				if (m_loglevel > 1)
					logger->Write ("CSidekickNet", LogNotice, "Triggering WLAN keep-alive request...");
				if (m_SKTPServer.port != 0)
				{
					char pResponseBuffer[4097]; //TODO: can we reuse something else existing here?
					CString path = isSktpSessionActive() ? getSktpPath( 92 ) : "/givemea404response.html";
					HTTPGet ( m_SKTPServer, path, pResponseBuffer, m_sktpResponseLength);
				}
				else
					UpdateTime();
			}
			#endif
			m_timestampOfLastWLANKeepAlive = m_pTimer->GetUptime();
			if (m_loglevel > 3)
				logger->Write ("CSidekickNet", LogNotice, getSysMonInfo(1));
		}
	}
	else if (m_isActive && isAnyNetworkActionQueued() && usesWLAN())
		m_timestampOfLastWLANKeepAlive = m_pTimer->GetUptime();
	
	if (m_queueDelay > 0 )
	{
		m_queueDelay--;
		//logger->Write( "handleQueuedNetworkAction", LogNotice, "m_queueDelay: %i", m_queueDelay);		
		
		return;
	}

	if ( m_isNetworkInitQueued && !m_isActive )
	{
		assert (!m_isActive);
		if (Initialize())
		{
			unsigned tries = 0;
			while (!UpdateTime() && tries < 3){ tries++;};
		}
		m_isNetworkInitQueued = false;
		return;
	}
	else if (m_isActive)
	{
		//logger->Write( "handleQueuedNetworkAction", LogNotice, "Yield");
		if ( netEnableWebserver )
			m_pScheduler->Yield (); // this is needed for webserver

		if (m_isFrameQueued)
		{
			#ifdef WITH_RENDER
			updateFrame();
			#endif
			m_isFrameQueued = false;
		}
		else if (m_isCSDBDownloadQueued)
		{
			if (m_loglevel > 2){
				logger->Write( "handleQueuedNetworkAction", LogNotice, "m_CSDBDownloadPath: %s", m_CSDBDownloadPath);
			}
			m_isCSDBDownloadQueued = false;
			getCSDBBinaryContent();
			//m_isSktpKeypressQueued = false;
		}
		/*
	
		else if (m_isCSDBDownloadSavingQueued)
		{
			if (m_loglevel > 2)
				logger->Write( "handleQueuedNetworkAction", LogNotice, "sCSDBDownloadSavingQueued");		
			m_isCSDBDownloadSavingQueued = false;
			saveDownload2SD();
			m_isSktpKeypressQueued = false;
		}
*/		
		//handle keypress anyway even if we have downloaded or saved something
		else if (m_isSktpKeypressQueued)
		{
			updateSktpScreenContent();
			//m_isMenuScreenUpdateNeeded = true;
			m_isSktpKeypressQueued = false;
		}
	}
}

boolean CSidekickNet::checkForSaveableDownload(){
	if (m_isCSDBDownloadSavingQueued)
	{
		if (m_loglevel > 0)
			logger->Write( "checkForSaveableDownload", LogNotice, "sCSDBDownloadSavingQueued");		
		m_isCSDBDownloadSavingQueued = false;
		saveDownload2SD();
		m_isSktpKeypressQueued = false;
		return true;
	}
	return false;
}

boolean CSidekickNet::isAnyNetworkActionQueued()
{
	return
			m_isNetworkInitQueued || 
			m_isFrameQueued || 
			m_isSktpKeypressQueued || 
			m_isCSDBDownloadQueued || 
			m_isCSDBDownloadSavingQueued;
}

void CSidekickNet::saveDownload2SD()
{
	m_isCSDBDownloadSavingQueued = false;
	if (m_loglevel > 2)
	{
		CString downloadLogMsg = "Writing download to SD card, bytes to write: ";
		CString Number;
		Number.Format ("%02d", prgSizeLaunch);
		downloadLogMsg.Append( Number );
		downloadLogMsg.Append( " Bytes, path: '" );
		downloadLogMsg.Append( m_CSDBDownloadSavePath );
		downloadLogMsg.Append( "'" );
		logger->Write( "saveDownload2SD", LogNotice, downloadLogMsg);
	}
	requireCacheWellnessTreatment();
	writeFile( logger, DRIVE, m_CSDBDownloadSavePath, (u8*) prgDataLaunch, prgSizeLaunch );
	requireCacheWellnessTreatment();
	logger->Write( "saveDownload2SD", LogNotice, "Finished writing.");
	m_isDownloadReadyForLaunch = true;
}

void CSidekickNet::prepareLaunchOfUpload( char * ext ){
	m_isDownloadReadyForLaunch = true;
	m_CSDBDownloadExtension = ext;
	m_CSDBDownloadFilename = (char *)"http_upload";
  //TODO:
	//m_CSDBDownloadSavePath
	//if already in launcher kernel (l), leave it
	if ( strcmp( m_currentKernelRunning, "l" ) == 0){
		m_isReturnToMenuRequested = true;
		//logger->Write( "prepareLaunchOfUpload", LogNotice, "ReturnToMenuRequested.");
		
	}
}

void CSidekickNet::cleanupDownloadData()
{
	#ifndef WITH_RENDER
	  clearErrorMsg(); //on c64screen, kernel menu
	  redrawSktpScreen();
  #endif
	m_CSDBDownloadSavePath = "";
	m_CSDBDownloadPath = (char*)"";
	m_CSDBDownloadFilename = (char*)""; // this is used from kernel_menu to display name on screen
	m_bSaveCSDBDownload2SD = false;
	m_isDownloadReadyForLaunch = false;
	requireCacheWellnessTreatment();
}

boolean CSidekickNet::isDownloadReadyForLaunch()
{
	return m_isDownloadReadyForLaunch;
}

boolean CSidekickNet::checkForFinishedDownload()
{
	boolean bTemp = m_isDownloadReady;
	if ( m_isDownloadReady){
		m_isDownloadReady = false;
		m_isDownloadReadyForLaunch = false;
		if ( m_bSaveCSDBDownload2SD )
		{
			if (m_loglevel > 2)
				logger->Write( "isDownloadReady", LogNotice, "Download is ready and we want to save it.");
			m_isCSDBDownloadSavingQueued = true;
			m_queueDelay = 5;
			if ( strcmp( m_CSDBDownloadExtension, "d64" ) == 0)
			{
				//                    "012345678901234567890123456789012345XXXX"
				setErrorMsgC64((char*)"       Saving D64 file to SD card       ");
			}
			else if ( strcmp( m_CSDBDownloadExtension, "prg" ) == 0)
			{
				//                    "012345678901234567890123456789012345XXXX"
				setErrorMsgC64((char*)"     Saving and launching PRG file      ");
			}
			else if ( strcmp( m_CSDBDownloadExtension, "sid" ) == 0)
			{
				//                    "012345678901234567890123456789012345XXXX"
				setErrorMsgC64((char*)"     Saving and launching SID file      ");
			}
			else if ( strcmp( m_CSDBDownloadExtension, "crt" ) == 0)
			{
				//                    "012345678901234567890123456789012345XXXX"
				setErrorMsgC64((char*)"     Saving and launching CRT file      ");
				m_queueDelay = 15;
			}
			requireCacheWellnessTreatment();
		}
	}
	return bTemp;
}

char * CSidekickNet::getNetworkActionStatusMessage()
{
	return m_networkActionStatusMsg;
}

CString CSidekickNet::getTimeString()
{
	//the most complicated and ugly way to get the data into the right form...
	CString *pTimeString = m_pTimer->GetTimeString();
	CString Buffer;
	if (pTimeString != 0)
	{
		Buffer.Append (*pTimeString);
	}
	delete pTimeString;
	return Buffer;
}

CString CSidekickNet::getUptime()
{
	unsigned uptime = m_pTimer->GetUptime();
	unsigned hours = 0, minutes = 0, seconds = 0;
	hours = uptime / 3600;
	minutes = (uptime % 3600) / 60;
	seconds = (uptime % 3600) % 60;
	CString strUptime = " ";
	CString Number;
	Number.Format ("%02d", hours);
	strUptime.Append( Number );
	strUptime.Append( "h ");
	Number.Format ("%02d", minutes);
	strUptime.Append( Number );
	strUptime.Append( "m ");
	Number.Format ("%02d", seconds);
	strUptime.Append( Number );
	strUptime.Append( "s");
	return strUptime;
}


CString CSidekickNet::getRaspiModelName()
{
	return m_pMachineInfo->Get()->GetMachineName();
}

CNetConfig * CSidekickNet::GetNetConfig(){
	assert (m_isActive);
	return m_Net->GetConfig ();
}

CIPAddress CSidekickNet::getIPForHost( const char * host )
{
	assert (m_isActive);
	unsigned attempts = 0;
	CIPAddress ip;
	while ( attempts < 3)
	{
		attempts++;
		if (!m_DNSClient->Resolve (host, &ip))
		{
			if (m_loglevel > 2)
				logger->Write ("getIPForHost", LogWarning, "Cannot resolve: %s",host);
		}
		else
		{
			CString IPString;
			ip.Format (&IPString);
			if (m_loglevel > 2)
				logger->Write ("getIPForHost", LogNotice, "Resolved %s as %s",host, (const char* ) IPString);
			break;
		}
	}
	return ip;
}

boolean CSidekickNet::UpdateTime(void)
{
	assert (m_isActive);
	CNTPClient NTPClient (m_Net);
	unsigned nTime = NTPClient.GetTime (m_NTPServerIP);
	if (nTime == 0)
	{
		if (m_loglevel > 2)
			logger->Write ("CSidekickNet::UpdateTime", LogWarning, "Cannot get time from %s",
						(const char *) NTPServer);

		return false;
	}

	if (CTimer::Get ()->SetTime (nTime, FALSE))
	{
		if (m_loglevel > 2)
			logger->Write ("CSidekickNet::UpdateTime", LogNotice, "System time updated");
			
		if ( m_pTimer->GetTime() > bestBefore ){
			logger->Write( "handleQueuedNetworkAction", LogError, "This time-limited network test kernel has reached its end of life!");
			//                    "012345678901234567890123456789012345XXXX"
			setErrorMsgC64((char*)"   Kernel is outdated - please update!  ");
			m_isRebootRequested = true;
		}
			
		return true;
	}
	else
	{
		if (m_loglevel > 1)
			logger->Write ("CSidekickNet::UpdateTime", LogWarning, "Cannot update system time");
	}
	return false;
}

void CSidekickNet::getCSDBBinaryContent( ){
	assert (m_isActive);
	unsigned iFileLength = 0;
	unsigned char prgDataLaunchTemp[ 1025*1024 ]; // TODO do we need this?
	if ( HTTPGet ( m_CSDBDownloadHost, (char *) m_CSDBDownloadPath, (char *) prgDataLaunchTemp, iFileLength)){
		if (m_loglevel > 3)
			logger->Write( "getCSDBBinaryContent", LogNotice, "Got stuff via HTTPS, now doing memcpy");
		memcpy( prgDataLaunch, prgDataLaunchTemp, iFileLength);
		prgSizeLaunch = iFileLength;
		m_isDownloadReady = true;
		if (m_loglevel > 3)
			logger->Write( "getCSDBBinaryContent", LogNotice, "memcpy finished.");
		requireCacheWellnessTreatment();
	}
	else if (m_CSDBDownloadHost.port == 443)
		setErrorMsgC64((char*)"          HTTPS request failed          ", false);
		//                    "012345678901234567890123456789012345XXXX"
		
	else
		setErrorMsgC64((char*)"           HTTP request failed          ", false);
		//                    "012345678901234567890123456789012345XXXX"
	if (m_loglevel > 2)
		logger->Write( "getCSDBBinaryContent", LogNotice, "HTTPS Document length: %i", iFileLength);
}

//for kernel render example
#ifdef WITH_RENDER

void CSidekickNet::updateFrame(){
	if (!m_isActive || m_SKTPServer.port == 0)
	{
		return;
	}
	unsigned iFileLength = 0;
	if (m_videoFrameCounter < 1) m_videoFrameCounter = 1;
	
  //CString path = "/videotest.php?frame=";
	CString path = "/c64frames/big_buck_bunny_";
	CString Number; 
	Number.Format ("%05d", m_videoFrameCounter);
	path.Append( Number );
	path.Append( ".bin" );
	HTTPGet ( m_SKTPServer, path, (char*) logo_bg_raw, iFileLength);
	m_videoFrameCounter++;
	if (m_videoFrameCounter > 1500) m_videoFrameCounter = 1;
}
#endif

void CSidekickNet::resetSktpSession(){
	m_sktpSession	= 0;
}

void CSidekickNet::redrawSktpScreen(){
	if (m_sktpSession == 1)
		m_sktpSession	= 2;
}

boolean CSidekickNet::isSktpSessionActive(){
	return m_sktpSession > 0;
}

boolean CSidekickNet::launchSktpSession(){
	char * pResponseBuffer = new char[33];	// +1 for 0-termination
//  use hostname as username for testing purposes
//	CString urlSuffix = "/sktp.php?session=new&username=";
//	urlSuffix.Append(netSidekickHostname);
//	if (HTTPGet ( m_SKTPServer, urlSuffix, pResponseBuffer, m_sktpResponseLength))
	if (HTTPGet ( m_SKTPServer, "/sktp.php?session=new", pResponseBuffer, m_sktpResponseLength))	
	{
		if ( m_sktpResponseLength > 25 && m_sktpResponseLength < 34){
			m_sktpSessionID = pResponseBuffer;
			m_sktpSessionID[m_sktpResponseLength] = '\0';
			if (m_loglevel > 2)
				logger->Write( "launchSktpSession", LogNotice, "Got session id: %s", m_sktpSessionID);
		}
	}
	else{
		if (m_loglevel > 1)
			logger->Write( "launchSktpSession", LogError, "Could not get session id.");
		m_sktpSessionID = (char*) "";
		return false;
	}
	return true;
}

CString CSidekickNet::getSktpPath( unsigned key )
{
	CString path = "/sktp.php?";
	CString Number; 
	Number.Format ("%02X", key);
	path.Append( "&key=" );
	path.Append( Number );

	path.Append( "&sessionid=" );
	path.Append( m_sktpSessionID );
	
	if ( m_sktpSession == 2) //redraw
	{
		m_sktpSession = 1;
		path.Append( "&redraw=1" );
	}
	return path;
}

void CSidekickNet::enteringSktpScreen(){
	m_isSktpScreenActive = true;
}

void CSidekickNet::leavingSktpScreen(){
	m_isSktpScreenActive = false;
}

boolean CSidekickNet::isSKTPScreenActive(){
	return m_isSktpScreenActive;
}

boolean CSidekickNet::isMenuScreenUpdateNeeded(){
	boolean tmp = m_isMenuScreenUpdateNeeded;
	m_isMenuScreenUpdateNeeded = false;
	return tmp;
}

void CSidekickNet::updateSktpScreenContent(){
	if (!m_isActive || !m_isSktpScreenActive || m_SKTPServer.port == 0)
	{
		m_sktpScreenContent = (unsigned char *) msgNoConnection; //FIXME: there's a memory leak in here
		return;
	}
	
	if ( m_sktpSession < 1){
		if (!launchSktpSession())
			return;
		m_sktpSession = 1;
	}

	char pResponseBuffer[4097]; //maybe turn this into member var when creating new sktp class?
	if (HTTPGet ( m_SKTPServer, getSktpPath( m_sktpKey ), pResponseBuffer, m_sktpResponseLength))
	{
		if ( m_sktpResponseLength > 0 )
		{
			//logger->Write( "updateSktpScreenContent", LogNotice, "HTTP Document m_sktpResponseLength: %i", m_sktpResponseLength);
			m_sktpResponseType = pResponseBuffer[0];
			if ( m_sktpResponseType == 2) // url for binary download, e. g. csdb
			{
				u8 tmpUrlLength = pResponseBuffer[1];
				u8 tmpFilenameLength = pResponseBuffer[2];
				m_bSaveCSDBDownload2SD = ((int)pResponseBuffer[3] == 1);
				char CSDBDownloadPath[256];
				char CSDBFilename[256];
				char extension[3];
				char hostName[256];
				u8 pathStart;
				u8 pathStartSuffix = 0;
				//TODO add more sanity checks here
				if ( pResponseBuffer[ 4 + 4 ] == ':')      // ....http
					pathStartSuffix = 8+3;                   // ....http://
				else if ( pResponseBuffer[ 4 + 4 ] == 's') // ....https
					pathStartSuffix = 8+4;                   // ....https://
				else
					logger->Write( "updateSktpScreenContent", LogNotice, "Error: wrong char");

				for ( pathStart = pathStartSuffix; pathStart < tmpUrlLength; pathStart++ ){
					if ( pResponseBuffer[ pathStart ] == '/' || pResponseBuffer[ pathStart ] == ':')
						break;
					hostName[ pathStart - pathStartSuffix] = pResponseBuffer[ pathStart ];
				}
				hostName[pathStart - pathStartSuffix] = '\0';
				
				if ( pResponseBuffer[ pathStart ] == ':' ){
					while( pResponseBuffer[ pathStart ] != '/')
					{
						pathStart++;
					}
				}
				
				if (strcmp(hostName, m_CSDB.hostName) == 0){
					m_CSDBDownloadHost = m_CSDB;
					//logger->Write( "updateSktpScreenContent", LogNotice, "host is csdb");
				}
				else if ( strcmp(hostName, m_SKTPServer.hostName) == 0){
					m_CSDBDownloadHost = m_SKTPServer;
					//logger->Write( "updateSktpScreenContent", LogNotice, "host is sktpserver");
				}
				else{
					logger->Write( "updateSktpScreenContent", LogNotice, "Error: Unknown host: >%s<", hostName);
					m_CSDBDownloadHost = m_CSDB;
				}

				memcpy( CSDBDownloadPath, &pResponseBuffer[ pathStart ], tmpUrlLength - pathStart + 4);
				CSDBDownloadPath[tmpUrlLength - pathStart + 4] = '\0';
//				logger->Write( "updateSktpScreenContent", LogNotice, "download path: >%s<", CSDBDownloadPath);
				memcpy( CSDBFilename, &pResponseBuffer[ 4 + tmpUrlLength  ], tmpFilenameLength );
				CSDBFilename[tmpFilenameLength] = '\0';
//				logger->Write( "updateSktpScreenContent", LogNotice, "filename: >%s<", CSDBFilename);
				memcpy( extension, &pResponseBuffer[ m_sktpResponseLength -3 ], 3);
				extension[3] = '\0';
				#ifndef WITHOUT_STDLIB
				//workaround: tolower is only available with stdlib!
				//enforce lowercase for extension because we compare it a lot
				for(int i = 0; extension[i]; i++){
				  extension[i] = tolower(extension[i]);
				}
				#endif

//				logger->Write( "updateSktpScreenContent", LogNotice, "extension: >%s<", extension);

				CString savePath;
				if (m_bSaveCSDBDownload2SD)
				{
					savePath = "SD:";
					if ( strcmp(extension,"prg") == 0)
#ifndef IS264
						savePath.Append( (const char *) "PRG/" );
#else
						savePath.Append( (const char *) "PRG264/" );
#endif
					else if ( strcmp(extension,"crt") == 0)
#ifndef IS264
						savePath.Append( (const char *) "CART264/" );
#else
						savePath.Append( (const char *) "CRT/" );
#endif
					else if ( strcmp(extension,"d64") == 0)
#ifndef IS264
						savePath.Append( (const char *) "D64/" );
#else
						savePath.Append( (const char *) "D264/" );
#endif
					else if ( strcmp(extension,"sid") == 0)
						savePath.Append( (const char *) "SID/" );
					savePath.Append(CSDBFilename);
				}
				m_sktpResponseLength = 1;
				m_sktpScreenContent = (unsigned char * ) pResponseBuffer;
				m_sktpScreenPosition = 1;
				m_isCSDBDownloadQueued = true;
				m_queueDelay = 0;
				m_CSDBDownloadPath = CSDBDownloadPath;
				m_CSDBDownloadExtension = extension;
				m_CSDBDownloadFilename = CSDBFilename;
				m_CSDBDownloadSavePath = savePath;
			}
			/*
			if ( m_sktpResponseType == 3) // background and border color change
			{
			}
			if ( m_sktpResponseType == 4) // background and border color change
			{
			}
			*/
			else
			{
				m_sktpScreenContent = (unsigned char * ) pResponseBuffer;
				m_sktpScreenPosition = 1;
				//m_isMenuScreenUpdateNeeded = true;
			}
		}
		//logger->Write( "updateSktpScreenContent", LogNotice, "HTTP Document content: '%s'", m_sktpScreenContent);
		
	}
	else
	{
		m_sktpScreenContent = (unsigned char *) msgNotFound;
	}
	m_sktpKey = 0;
}

boolean CSidekickNet::IsSktpScreenContentEndReached()
{
	return m_sktpScreenPosition >= m_sktpResponseLength;
}

boolean CSidekickNet::IsSktpScreenToBeCleared()
{
	return m_sktpResponseType == 0;
}

boolean CSidekickNet::IsSktpScreenUnchanged()
{
	return m_sktpResponseType == 2;
}

void CSidekickNet::ResetSktpScreenContentChunks(){
	m_sktpScreenPosition = 1;
}

unsigned char * CSidekickNet::GetSktpScreenContentChunk( u16 & startPos, u8 &color, boolean &inverse )
{
	if ( m_sktpScreenPosition >= m_sktpResponseLength ){
		//logger->Write( "GetSktpScreenContentChunk", LogNotice, "End reached.");
		startPos = 0;
		color = 0;
		m_sktpScreenPosition = 1;
		return (unsigned char *) '\0';
	}
	u8 type      = m_sktpScreenContent[ m_sktpScreenPosition ];
	u8 scrLength = m_sktpScreenContent[ m_sktpScreenPosition + 1]; // max255
	u8 byteLength= 0;
	u8 startPosL = m_sktpScreenContent[ m_sktpScreenPosition + 2 ];//screen pos x/y
	u8 startPosM = m_sktpScreenContent[ m_sktpScreenPosition + 3 ];//screen pos x/y
	color        = m_sktpScreenContent[ m_sktpScreenPosition + 4 ]&15;//0-15, here we have some bits
	inverse    = m_sktpScreenContent[ m_sktpScreenPosition + 4 ]>>7;//test bit 8
	
	if ( type == 0)
	 	byteLength = scrLength;
	if ( type == 1) //repeat one character for scrLength times
	 	byteLength = 1;
	
	startPos = startPosM * 255 + startPosL;//screen pos x/y
	//logger->Write( "GetSktpScreenContentChunk", LogNotice, "Chunk parsed: length=%u, startPos=%u, color=%u ",length, startPos, color);
	if ( type == 0)
		memcpy( m_sktpScreenContentChunk, &m_sktpScreenContent[ m_sktpScreenPosition + 5], byteLength);
	if ( type == 1) //repeat single char
	{
		char fillChar = m_sktpScreenContent[ m_sktpScreenPosition + 5 ];
		for (unsigned i = 0; i < scrLength; i++)
			m_sktpScreenContentChunk[i] = fillChar;
	}
	m_sktpScreenContentChunk[scrLength] = '\0';
	m_sktpScreenPosition += 5+byteLength;//begin of next chunk
	
  return m_sktpScreenContentChunk;
}

boolean CSidekickNet::HTTPGet (remoteHTTPTarget & target, const char * path, char *pBuffer, unsigned & nLengthRead )
{
	assert (pBuffer != 0);
	unsigned nLength = nDocMaxSize;
	if (m_loglevel > 3)
		logger->Write( "HTTPGet", LogNotice, target.logPrefix, path );
#ifdef WITH_TLS	
	CHTTPClient client( m_TLSSupport, target.ipAddress, target.port, target.hostName, target.port == 443 );
	CircleMbedTLS::THTTPStatus Status = client.Get (path, (u8 *) pBuffer, &nLength);
	if (Status != CircleMbedTLS::HTTPOK)
#else
	CHTTPClient client( m_Net, target.ipAddress, target.port, target.hostName );
	::THTTPStatus Status = client.Get (path, (u8 *) pBuffer, &nLength);
	if (Status != ::HTTPOK)
	
#endif	

	{
		if (m_loglevel > 0)
			logger->Write( "HTTPGet", LogError, "Failed with status %u, >%s<", Status, path);
		return false;
	}
	assert (nLength <= nDocMaxSize);
	pBuffer[nLength] = '\0';
	nLengthRead = nLength;
	return true;
}

void CSidekickNet::updateSystemMonitor( size_t freeSpace, unsigned CpuTemp)
{
	m_sysMonHeapFree = freeSpace;
	m_sysMonCPUTemp = CpuTemp;
}

CString CSidekickNet::getSysMonInfo( unsigned details )
{
	CString m_sysMonInfo = "";
	m_sysMonInfo.Append("CPU ");
	CString Number; 
	Number.Format ("%02d", m_sysMonCPUTemp);
	m_sysMonInfo.Append( Number );
	m_sysMonInfo.Append("'C, Uptime:");
	m_sysMonInfo.Append( getUptime() );
	
	if ( details > 0 )
	{
		m_sysMonInfo.Append(", ");
		CString Number2; 
		Number2.Format ("%02d", m_sysMonHeapFree/1024);
		m_sysMonInfo.Append( Number2 );
		m_sysMonInfo.Append(" kb free");
	}
	return m_sysMonInfo;
}

void CSidekickNet::requireCacheWellnessTreatment(){
	m_kMenu->doCacheWellnessTreatment();
}

void CSidekickNet::getNetRAM( u8 * content, u32 * size){
	CString path = "/getNetRam.php";
	if (!HTTPGet ( m_SKTPServer, path, (char*) content, *size)){
		logger->Write( "getNetRAM", LogError, "Failed with path >%s<", path);
	}
}

void CSidekickNet::setCurrentKernel( char * r){
	m_currentKernelRunning = r;
}
