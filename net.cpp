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

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#ifdef WITH_TLS
#include <mbedtls/error.h>
#endif

#include "PSID/psid64/psid64.h"


// Network configuration
#ifndef WITH_WLAN
#define USE_DHCP
#endif
// Time configuration
static const char NTPServer[]    = "pool.ntp.org";
static const int nTimeZone       = 2*60;		// minutes diff to UTC
static const char DRIVE[] = "SD:";
//nDocMaxSize reserved 2 MB as the maximum size of the kernel file
static const unsigned nDocMaxSize = 2000*1024;
//static const char KERNEL_IMG_NAME[] = "kernel8.img";
//static const char KERNEL_SAVE_LOCATION[] = "SD:kernel8.img";
//static const char RPIMENU64_SAVE_LOCATION[] = "SD:C64/rpimenu_net.prg";
static const char msgNoConnection[] = "Sorry, no network connection!";
static const char msgNotFound[]     = "Message not found. :(";
//static const char * prgUpdatePath[2] = { "/sidekick64/rpimenu_net.prg", "/sidekick264/rpimenu_net.prg"};

static const char CSDB_HOST[] = "csdb.dk";

#ifdef WITH_WLAN
#define DRIVE		"SD:"
#define FIRMWARE_PATH	DRIVE "/firmware/"		// firmware files must be provided here
#define CONFIG_FILE	DRIVE "/wpa_supplicant.conf"
//static const char * kernelUpdatePath[2] = { "/sidekick64/kernel8.wlan.img", "/sidekick264/kernel8.wlan.img"};
//#else
//static const char * kernelUpdatePath[2] = { "/sidekick64/kernel8.img", "/sidekick264/kernel8.img"};
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
		m_isFSMounted( false ),
		m_isActive( false ),
		m_isPrepared( false ),
		m_isUSBPrepared( false ),
		m_isNetworkInitQueued( false ),
		//m_isKernelUpdateQueued( false ),
		m_isFrameQueued( false ),
		m_isSktxKeypressQueued( false ),
		m_isCSDBDownloadQueued( false ),
		m_isCSDBDownloadSavingQueued( false ),
		m_isDownloadReady( false ),
		m_isDownloadReadyForLaunch( false ),
		m_isRebootRequested( false ),
		m_networkActionStatusMsg( (char * ) ""),
		m_sktxScreenContent( (unsigned char * ) ""),
		m_sktxSessionID( (char * ) ""),
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
		m_skipSktxRefresh(0),
		m_sktxScreenPosition(0),
		m_sktxResponseLength(0),
		m_sktxResponseType(0),
		m_sktxKey(0),
		m_sktxSession(0),
		m_videoFrameCounter(1),
		//m_sysMonInfo(""),
		m_sysMonHeapFree(0),
		m_sysMonCPUTemp(0),
		m_loglevel(1)
		
{
	assert (m_pTimer != 0);
	assert (& m_pScheduler != 0);

	//timezone is not really related to net stuff, it could go somewhere else
	m_pTimer->SetTimeZone (nTimeZone);
}

void CSidekickNet::setErrorMsgC64( char * msg ){ 
	#ifndef WITH_RENDER
	setErrorMsg( msg ); 
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
			//                 "012345678901234567890123456789012345XXXX"
			setErrorMsgC64((char*)"    Is the network cable plugged in?    ");
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
	/*
	if ( strcmp(netUpdateHostName,"") != 0)
	{
		int port = netUpdateHostPort != 0 ? netUpdateHostPort: HTTP_PORT;
		m_devServer.hostName = netUpdateHostName;
		m_devServer.port = port;
		m_devServer.ipAddress = getIPForHost( netUpdateHostName );
		m_devServer.logPrefix = getLoggerStringForHost( netUpdateHostName, port);
	}
	else
	*/
	{
		m_devServer.hostName = "";
		m_devServer.port = 0;
	}
	
	if (strcmp(netSktxHostName,"") != 0)
	{

		//if ( strcmp(netUpdateHostName, netSktxHostName) != 0)
		{
			int port = netSktxHostPort != 0 ? netSktxHostPort: HTTP_PORT;
			m_Playground.hostName = netSktxHostName;
			m_Playground.port = port;
			m_Playground.ipAddress = getIPForHost( netSktxHostName );
			m_Playground.logPrefix = getLoggerStringForHost( netSktxHostName, port);
		}
		//else
		//	m_Playground = m_devServer;
	}
	else
	{
		m_Playground.hostName = "";
		m_Playground.port = 0;
	}
	
	if ( netEnableWebserver ){
		if (m_loglevel > 2)
			logger->Write ("CSidekickNet::Initialize", LogNotice, "Starting webserver.");
		m_WebServer = new CWebServer (m_Net, 80, KERNEL_MAX_SIZE + 2000, 0, this);
	}
				
	#ifndef WITH_RENDER
	 clearErrorMsg(); //on c64screen, kernel menu
  #endif
	return true;
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
	return m_isRebootRequested;
}

boolean CSidekickNet::isRebootRequested(){
	if (!m_isRebootRequested)
		return false;
	unsigned waitDuration = 8;
	signed secondsLeft = waitDuration - (m_pTimer->GetUptime() - m_timeoutCounterStart);
	if ( secondsLeft < 0 ) secondsLeft = 0;
	CString msg = "  Please wait, rebooting Sidekick in ";
	CString Number;
	Number.Format ("%01d", secondsLeft-2 > 0 ? secondsLeft-2 : 0 );
	msg.Append( Number );
	msg.Append("  ");
	const char * tmp = msg;
	setErrorMsgC64( (char*) tmp );
	if ( secondsLeft <= 0)
		return true;
	else
		return false;
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
	CGlueStdioInit (m_FileSystem);

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

void CSidekickNet::queueSktxKeypress( int key )
{
	if ( !isAnyNetworkActionQueued())
		m_queueDelay = 0; //this could lead to disturbing an already running queuedelay
	m_isSktxKeypressQueued = true;
	m_sktxKey = key;
	//logger->Write ("CSidekickNet::queueSktxKeypress", LogNotice, "Queuing keypress");
}

void CSidekickNet::queueSktxRefresh()
{
	//refesh when user didn't press a key
	//this has to be quick for multiplayer games (value 4)
	//and can be slow for csdb browsing (value 16)
	m_skipSktxRefresh++;
	if ( m_skipSktxRefresh >8 && !isAnyNetworkActionQueued())
	{
		m_skipSktxRefresh = 0;
		queueSktxKeypress( 92 );
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
		//with the new d2ef approach available within Sidekick we try to launch the D64
		//as an dynamically created EF crt!
		type = 11;
		extern int createD2EF( unsigned char *diskimage, int imageSize, unsigned char *cart, int build, int mode, int autostart );
		unsigned char *cart = new unsigned char[ 1024 * 1025 ];
		u32 crtSize = createD2EF( prgDataLaunch, prgSizeLaunch, cart, 2, 0, true );
		memcpy( &prgDataLaunch[0], cart, crtSize );
		prgSizeLaunch = crtSize;
		m_CSDBDownloadFilename = "SD:C64/temp.crt"; //this is only an irrelevant dummy name ending wih crt
		
		//type = 0; //unused, we only save the file
		if (m_loglevel > 2)
			logger->Write ("CSidekickNet::getCSDBDownloadLaunchType", LogNotice, "D64 detected: >%s<",m_CSDBDownloadExtension);
	}
	else if ( strcmp( m_CSDBDownloadExtension, "sid" ) == 0)
	{
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
		//every 10 seconds + seconds needed for request
		//log cpu temp + uptime + free memory
		//in wlan case do keep-alive request
		if ( m_pTimer->GetUptime() - m_timestampOfLastWLANKeepAlive > 10)
		{
			#ifdef WITH_WLAN
			//if (!netEnableWebserver)
			{
				//Circle42 offers experimental WLAN, but it seems to
				//disconnect very quickly if there is not traffic.
				//This can be very annoying.
				//As a WLAN keep-alive, we auto queue a network event
				//to avoid WLAN going into "zombie" disconnected mode
				if (m_loglevel > 3)
					logger->Write ("CSidekickNet", LogNotice, "Triggering WLAN keep-alive request...");
				if (m_Playground.port != 0)
				{
					char pResponseBuffer[4097]; //TODO: can we reuse something else existing here?
					CString path = isSktxSessionActive() ? getSktxPath( 92 ) : "/givemea404response.html";
					HTTPGet ( m_Playground, path, pResponseBuffer, m_sktxResponseLength);
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
		if ( netEnableWebserver )
			m_pScheduler->Yield (); // this is needed for webserver
/*
		if ( m_isKernelUpdateQueued )
		{
			//CheckForSidekickKernelUpdate();
			m_isKernelUpdateQueued = false;
			#ifndef WITH_RENDER
			clearErrorMsg(); //on c64screen, kernel menu
			#endif
		}
*/		
		if (m_isFrameQueued)
		{
			#ifdef WITH_RENDER
			updateFrame();
			#endif
			m_isFrameQueued = false;
		}
		else if (m_isCSDBDownloadQueued)
		{
			if (m_loglevel > 2)
				logger->Write( "handleQueuedNetworkAction", LogNotice, "m_CSDBDownloadPath: %s", m_CSDBDownloadPath);		
			m_isCSDBDownloadQueued = false;
			getCSDBBinaryContent( m_CSDBDownloadPath );
			//m_isSktxKeypressQueued = false;
		}
		/*
	
		else if (m_isCSDBDownloadSavingQueued)
		{
			if (m_loglevel > 2)
				logger->Write( "handleQueuedNetworkAction", LogNotice, "sCSDBDownloadSavingQueued");		
			m_isCSDBDownloadSavingQueued = false;
			saveDownload2SD();
			m_isSktxKeypressQueued = false;
		}
*/		
		//handle keypress anyway even if we have downloaded or saved something
		else if (m_isSktxKeypressQueued)
		{
			updateSktxScreenContent();
			m_isSktxKeypressQueued = false;
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
		m_isSktxKeypressQueued = false;
		return true;
	}
	return false;
}

boolean CSidekickNet::isAnyNetworkActionQueued()
{
	return
			m_isNetworkInitQueued || 
			//m_isKernelUpdateQueued || 
			m_isFrameQueued || 
			m_isSktxKeypressQueued || 
			m_isCSDBDownloadQueued || 
			m_isCSDBDownloadSavingQueued;
}

void CSidekickNet::saveDownload2SD()
{
	//if when we save a prg we can be sure that we are in the launcher kernel atm
	//we need a waiting popup that locks the sktx screen...
	if ( strcmp( m_CSDBDownloadExtension, "prg" ) == 0)
	{
		m_isSktxKeypressQueued = false;
	}
	
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
	writeFile( logger, DRIVE, m_CSDBDownloadSavePath, (u8*) prgDataLaunch, prgSizeLaunch );
	logger->Write( "saveDownload2SD", LogNotice, "Finished writing.");
	m_isDownloadReadyForLaunch = true;
}

void CSidekickNet::cleanupDownloadData()
{
	#ifndef WITH_RENDER
	  clearErrorMsg(); //on c64screen, kernel menu
	  redrawSktxScreen();
  #endif
	m_CSDBDownloadSavePath = "";
	m_CSDBDownloadPath = (char*)"";
	m_CSDBDownloadFilename = (char*)""; // this is used from kernel_menu to display name on screen
	m_bSaveCSDBDownload2SD = false;
	m_isDownloadReadyForLaunch = false;
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
		return true;
	}
	else
	{
		if (m_loglevel > 1)
			logger->Write ("CSidekickNet::UpdateTime", LogWarning, "Cannot update system time");
	}
	return false;
}

//looks for the presence of a file on a pre-defined HTTP Server
//file is being read and stored on the sd card
/*
boolean CSidekickNet::CheckForSidekickKernelUpdate()
{
	if ( m_devServer.port == 0 )
	{
		logger->Write( "CSidekickNet::CheckForSidekickKernelUpdate", LogNotice, 
			"Skipping check: Update server is not defined."
		);
		return false;
	}
	
	if ( strcmp( m_SidekickKernelUpdatePath,"") == 0 )
	{
		logger->Write( "CSidekickNet::CheckForSidekickKernelUpdate", LogNotice, 
			"Skipping check: HTTP update path is not defined."
		);
		return false;
	}
	assert (m_isActive);
	unsigned iFileLength = 0;
	char pFileBuffer[nDocMaxSize+1];	// +1 for 0-termination
	unsigned type = 0;
	if ( m_SidekickKernelUpdatePath == 264 ) type = 1;
	if ( HTTPGet ( m_devServer, kernelUpdatePath[type], pFileBuffer, iFileLength))
	{
		logger->Write( "SidekickKernelUpdater", LogNotice, 
			"Now trying to write kernel file to SD card, bytes to write: %i", iFileLength
		);
		writeFile( logger, DRIVE, KERNEL_SAVE_LOCATION, (u8*) pFileBuffer, iFileLength );
		m_pScheduler->MsSleep (500);
		logger->Write( "SidekickKernelUpdater", LogNotice, "Finished writing kernel to SD card");
	}
	if ( HTTPGet ( m_devServer, prgUpdatePath[type], pFileBuffer, iFileLength))
	{
		logger->Write( "SidekickKernelUpdater", LogNotice, 
			"Now trying to write rpimenu.prg file to SD card, bytes to write: %i", iFileLength
		);
		writeFile( logger, DRIVE, RPIMENU64_SAVE_LOCATION, (u8*) pFileBuffer, iFileLength );
		m_pScheduler->MsSleep (500);
		logger->Write( "SidekickKernelUpdater", LogNotice, "Finished writing file rpimenu_net.prg to SD card");
	}
	
	return true;
}*/

/*
void CSidekickNet::getCSDBContent( const char * fileName, const char * filePath){
	assert (m_isActive);
	unsigned iFileLength = 0;
	char * pFileBuffer = new char[nDocMaxSize+1];	// +1 for 0-termination
	HTTPGet ( m_CSDB, filePath, pFileBuffer, iFileLength);
	logger->Write( "getCSDBContent", LogNotice, "HTTPS Document length: %i", iFileLength);
}
*/

void CSidekickNet::getCSDBBinaryContent( char * filePath ){
	assert (m_isActive);
	unsigned iFileLength = 0;


	unsigned char prgDataLaunchTemp[ 1025*1024 ]; // TODO do we need this?
	if ( HTTPGet ( m_CSDB, (char *) filePath, (char *) prgDataLaunchTemp, iFileLength)){
		if (m_loglevel > 3)
			logger->Write( "getCSDBBinaryContent", LogNotice, "Got stuff via HTTPS, now doing memcpy");
		memcpy( prgDataLaunch, prgDataLaunchTemp, iFileLength);
		prgSizeLaunch = iFileLength;
		m_isDownloadReady = true;
		if (m_loglevel > 3)
			logger->Write( "getCSDBBinaryContent", LogNotice, "memcpy finished.");
	}
	else{
		setErrorMsgC64((char*)"    HTTPS request failed (press D).");		
	}
	if (m_loglevel > 2)
		logger->Write( "getCSDBBinaryContent", LogNotice, "HTTPS Document length: %i", iFileLength);
}

/*
void CSidekickNet::getCSDBLatestReleases(){
	getCSDBContent( "latestreleases.php", "/rss/latestreleases.php" );
}
*/

//for kernel render example
#ifdef WITH_RENDER

void CSidekickNet::updateFrame(){
	if (!m_isActive || m_Playground.port == 0)
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
	HTTPGet ( m_Playground, path, (char*) logo_bg_raw, iFileLength);
	m_videoFrameCounter++;
	if (m_videoFrameCounter > 1500) m_videoFrameCounter = 1;
}
#endif

void CSidekickNet::resetSktxSession(){
	m_sktxSession	= 0;
}

void CSidekickNet::redrawSktxScreen(){
	if (m_sktxSession == 1)
		m_sktxSession	= 2;
}

boolean CSidekickNet::isSktxSessionActive(){
	return m_sktxSession > 0;
}

boolean CSidekickNet::launchSktxSession(){
	char * pResponseBuffer = new char[33];	// +1 for 0-termination
//  use hostname as username for testing purposes
//	CString urlSuffix = "/sktx.php?session=new&username=";
//	urlSuffix.Append(netSidekickHostname);
//	if (HTTPGet ( m_Playground, urlSuffix, pResponseBuffer, m_sktxResponseLength))
	if (HTTPGet ( m_Playground, "/sktx.php?session=new", pResponseBuffer, m_sktxResponseLength))	
	{
		if ( m_sktxResponseLength > 25 && m_sktxResponseLength < 34){
			m_sktxSessionID = pResponseBuffer;
			m_sktxSessionID[m_sktxResponseLength] = '\0';
			if (m_loglevel > 2)
				logger->Write( "launchSktxSession", LogNotice, "Got session id: %s", m_sktxSessionID);
		}
	}
	else{
		if (m_loglevel > 1)
			logger->Write( "launchSktxSession", LogError, "Could not get session id.");
		m_sktxSessionID = (char*) "";
		return false;
	}
	return true;
}

CString CSidekickNet::getSktxPath( unsigned key )
{
	CString path = "/sktx.php?";
	CString Number; 
	Number.Format ("%02X", key);
	path.Append( "&key=" );
	path.Append( Number );

	path.Append( "&sessionid=" );
	path.Append( m_sktxSessionID );
	
	if ( m_sktxSession == 2) //redraw
	{
		m_sktxSession = 1;
		path.Append( "&redraw=1" );
	}
	return path;
}

//void CSidekickNet::parseSktxDownloadURL(){}


void CSidekickNet::updateSktxScreenContent(){
	if (!m_isActive || m_Playground.port == 0)
	{
		m_sktxScreenContent = (unsigned char *) msgNoConnection; //FIXME: there's a memory leak in here
		return;
	}
	
	if ( m_sktxSession < 1){
		if (!launchSktxSession())
			return;
		m_sktxSession = 1;
	}

	char pResponseBuffer[4097]; //maybe turn this into member var when creating new sktx class?
	if (HTTPGet ( m_Playground, getSktxPath( m_sktxKey ), pResponseBuffer, m_sktxResponseLength))
	{
		if ( m_sktxResponseLength > 0 )
		{
			//logger->Write( "updateSktxScreenContent", LogNotice, "HTTP Document m_sktxResponseLength: %i", m_sktxResponseLength);
			m_sktxResponseType = pResponseBuffer[0];
			if ( m_sktxResponseType == 2) // url for binary download, e. g. csdb
			{
				
				u8 tmpUrlLength = pResponseBuffer[1];
				u8 tmpFilenameLength = pResponseBuffer[2];
				m_bSaveCSDBDownload2SD = ((int)pResponseBuffer[3] == 1);
				char CSDBDownloadPath[256];
				char CSDBFilename[256];
				char extension[3];
				//cut off first 14 chars of URL: http://csdb.dk
				//TODO add sanity checks here
				memcpy( CSDBDownloadPath, &pResponseBuffer[ 4 + 14  ], tmpUrlLength-14 );//m_sktxResponseLength -14 +1);
//				logger->Write( "updateSktxScreenContent", LogNotice, "download path: >%s<", CSDBDownloadPath);
				CSDBDownloadPath[tmpUrlLength-14] = '\0';
				memcpy( CSDBFilename, &pResponseBuffer[ 4 + tmpUrlLength  ], tmpFilenameLength );
				CSDBFilename[tmpFilenameLength] = '\0';
//				logger->Write( "updateSktxScreenContent", LogNotice, "filename: >%s<", CSDBFilename);
				memcpy( extension, &pResponseBuffer[ m_sktxResponseLength -3 ], 3);
				extension[3] = '\0';
//				logger->Write( "updateSktxScreenContent", LogNotice, "extension: >%s<", extension);


				CString savePath;
				if (m_bSaveCSDBDownload2SD)
				{
					savePath = "SD:";
					if ( strcmp(extension,"prg") == 0)
						savePath.Append( (const char *) "PRG/" );
					else if ( strcmp(extension,"crt") == 0)
						savePath.Append( (const char *) "CRT/" );
					else if ( strcmp(extension,"d64") == 0)
						savePath.Append( (const char *) "D64/" );
					else if ( strcmp(extension,"sid") == 0)
						savePath.Append( (const char *) "SID/" );
					savePath.Append(CSDBFilename);
				}
				m_sktxResponseLength = 1;
				m_sktxScreenContent = (unsigned char * ) pResponseBuffer;
				m_sktxScreenPosition = 1;
				m_isCSDBDownloadQueued = true;
				m_queueDelay = 0;
				m_CSDBDownloadPath = CSDBDownloadPath;
				m_CSDBDownloadExtension = extension;
				m_CSDBDownloadFilename = CSDBFilename;
				m_CSDBDownloadSavePath = savePath;
			}
			if ( m_sktxResponseType == 4) // background and border color change
			{
			}
			else
			{
				m_sktxScreenContent = (unsigned char * ) pResponseBuffer;
				m_sktxScreenPosition = 1;
			}
		}
		//logger->Write( "updateSktxScreenContent", LogNotice, "HTTP Document content: '%s'", m_sktxScreenContent);
		
	}
	else
	{
		m_sktxScreenContent = (unsigned char *) msgNotFound;
	}
	m_sktxKey = 0;
}

boolean CSidekickNet::IsSktxScreenContentEndReached()
{
	return m_sktxScreenPosition >= m_sktxResponseLength;
}

boolean CSidekickNet::IsSktxScreenToBeCleared()
{
	return m_sktxResponseType == 0;
}

boolean CSidekickNet::IsSktxScreenUnchanged()
{
	return m_sktxResponseType == 2;
}

void CSidekickNet::ResetSktxScreenContentChunks(){
	m_sktxScreenPosition = 1;
}

unsigned char * CSidekickNet::GetSktxScreenContentChunk( u16 & startPos, u8 &color, boolean &inverse )
{
	if ( m_sktxScreenPosition >= m_sktxResponseLength ){
		//logger->Write( "GetSktxScreenContentChunk", LogNotice, "End reached.");
		startPos = 0;
		color = 0;
		m_sktxScreenPosition = 1;
		return (unsigned char *) '\0';
	}
	u8 type      = m_sktxScreenContent[ m_sktxScreenPosition ];
	u8 scrLength = m_sktxScreenContent[ m_sktxScreenPosition + 1]; // max255
	u8 byteLength= 0;
	u8 startPosL = m_sktxScreenContent[ m_sktxScreenPosition + 2 ];//screen pos x/y
	u8 startPosM = m_sktxScreenContent[ m_sktxScreenPosition + 3 ];//screen pos x/y
	color        = m_sktxScreenContent[ m_sktxScreenPosition + 4 ]&15;//0-15, here we have some bits
	inverse    = m_sktxScreenContent[ m_sktxScreenPosition + 4 ]>>7;//test bit 8
	
	if ( type == 0)
	 	byteLength = scrLength;
	if ( type == 1) //repeat one character for scrLength times
	 	byteLength = 1;
	
	startPos = startPosM * 255 + startPosL;//screen pos x/y
	//logger->Write( "GetSktxScreenContentChunk", LogNotice, "Chunk parsed: length=%u, startPos=%u, color=%u ",length, startPos, color);
	if ( type == 0)
		memcpy( m_sktxScreenContentChunk, &m_sktxScreenContent[ m_sktxScreenPosition + 5], byteLength);
	if ( type == 1) //repeat single char
	{
		char fillChar = m_sktxScreenContent[ m_sktxScreenPosition + 5 ];
		for (unsigned i = 0; i < scrLength; i++)
			m_sktxScreenContentChunk[i] = fillChar;
	}
	m_sktxScreenContentChunk[scrLength] = '\0';
	m_sktxScreenPosition += 5+byteLength;//begin of next chunk
	
  return m_sktxScreenContentChunk;
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
		if (m_loglevel > 1)
			logger->Write( "HTTPGet", LogError, "Failed with status %u", Status);
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
