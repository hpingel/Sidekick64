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
#include <circle-mbedtls/httpclient.h>

#include <circle-mbedtls/tlssimpleclientsocket.h>
#include <circle/net/in.h>
#include <circle/types.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <mbedtls/error.h>

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
static const char KERNEL_IMG_NAME[] = "kernel8.img";
static const char KERNEL_SAVE_LOCATION[] = "SD:kernel8.img";
static const char RPIMENU64_SAVE_LOCATION[] = "SD:C64/rpimenu.prg";
static const char msgNoConnection[] = "Sorry, no network connection!";
static const char msgNotFound[]     = "Message not found. :(";
static const char * prgUpdatePath[2] = { "/sidekick64/rpimenu.prg", "/sidekick264/rpimenu.prg"};

static const char CSDB_HOST[] = "csdb.dk";

#ifdef WITH_WLAN
#define DRIVE		"SD:"
#define FIRMWARE_PATH	DRIVE "/firmware/"		// firmware files must be provided here
#define CONFIG_FILE	DRIVE "/wpa_supplicant.conf"
static const char * kernelUpdatePath[2] = { "/sidekick64/kernel8.wlan.img", "/sidekick264/kernel8.wlan.img"};
#else
static const char * kernelUpdatePath[2] = { "/sidekick64/kernel8.img", "/sidekick264/kernel8.img"};
#endif

//temporary hack

extern u32 prgSizeLaunch;
extern unsigned char prgDataLaunch[ 1025*1024 ];
#ifdef WITH_RENDER
  extern unsigned char logo_bg_raw[32000];
#endif

CSidekickNet::CSidekickNet( CInterruptSystem * pInterruptSystem, CTimer * pTimer, CScheduler * pScheduler, CEMMCDevice * pEmmcDevice  )
		:m_USBHCI (pInterruptSystem, pTimer),
		m_pScheduler(pScheduler),
		m_pTimer (pTimer),
		m_EMMC ( *pEmmcDevice),
#ifdef WITH_WLAN		
		m_WLAN (FIRMWARE_PATH),
		m_Net (0, 0, 0, 0, DEFAULT_HOSTNAME, NetDeviceTypeWLAN),
		m_WPASupplicant (CONFIG_FILE),
#endif
		m_DNSClient(&m_Net),
		m_TLSSupport (&m_Net),
		m_useWLAN (false),
		m_isActive( false ),
		m_isPrepared( false ),
		m_isNetworkInitQueued( false ),
		m_isKernelUpdateQueued( false ),
		m_isFrameQueued( false ),
		m_isSktxKeypressQueued( false ),
		m_isCSDBDownloadQueued( false ),
		m_isPRGDownloadReady( false ),
		//m_tryFilesystemRemount( false ),
		m_networkActionStatusMsg( (char * ) ""),
		m_sktxScreenContent( (unsigned char * ) ""),
		m_sktxSessionID( (char * ) ""),
		m_CSDBDownloadPath( (char * ) ""),
		m_CSDBDownloadExtension( (char * ) ""),
		m_CSDBDownloadFilename( (char * ) ""),
		m_CSDBDownloadSavePath( (char *)"" ),
		m_bSaveCSDBDownload2SD( false ),
		m_PiModel( m_pMachineInfo->Get()->GetMachineModel () ),
		m_DevHttpHost(0),
		m_DevHttpHostPort(0),
		m_PlaygroundHttpHost(0),
		m_PlaygroundHttpHostPort(0),
		m_SidekickKernelUpdatePath(0),
		m_queueDelay(0),
		m_effortsSinceLastEvent(0),
		m_skipSktxRefresh(0),
		m_sktxScreenPosition(0),
		m_sktxResponseLength(0),
		m_sktxResponseType(0),
		m_sktxKey(0),
		m_sktxSession(0),
		m_videoFrameCounter(1),
		//m_sysMonInfo(""),
		m_sysMonHeapFree(0),
		m_sysMonCPUTemp(0)
		
{
	assert (m_pTimer != 0);
	assert (& m_pScheduler != 0);
	assert (& m_USBHCI != 0);

	#ifdef WITH_WLAN
		m_useWLAN = true;
	#else
		m_useWLAN = false;
	#endif

	//timezone is not really related to net stuff, it could go somewhere else
	m_pTimer->SetTimeZone (nTimeZone);
}

void CSidekickNet::setErrorMsgC64( char * msg ){ 
	#ifndef WITH_RENDER
	setErrorMsg( msg ); 
	#endif
};

void CSidekickNet::setSidekickKernelUpdatePath( unsigned type)
{
	m_SidekickKernelUpdatePath = type;
};

boolean CSidekickNet::ConnectOnBoot (){
	return netConnectOnBoot;
}


boolean CSidekickNet::Initialize()
{
	const unsigned sleepLimit = 100 * (m_useWLAN ? 10:1);
	unsigned sleepCount = 0;
	
	if (m_isActive)
	{
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
	while (!m_Net.IsRunning () && sleepCount < sleepLimit)
	{
		m_pScheduler->MsSleep (100);
		sleepCount ++;
	}

	if (!m_Net.IsRunning () && sleepCount >= sleepLimit){
		logger->Write( "CSidekickNet::Initialize", LogNotice, 
			"Network connection is not running - is ethernet cable not attached?"
		);
		//                 "012345678901234567890123456789012345XXXX"
		setErrorMsgC64((char*)"    Is the network cable plugged in?    ");
		return false;
	}

	//net connection is up and running now
	m_isActive = true;

	//TODO: the resolves could be postponed to the moment where the first 
	//actual access takes place
	if ( strcmp(netUpdateHostName,"") != 0)
	{
		m_DevHttpHostPort = netUpdateHostPort != 0 ? netUpdateHostPort: HTTP_PORT;
	  m_DevHttpHost = (const char *) netUpdateHostName;
		m_DevHttpServerIP = getIPForHost(m_DevHttpHost);
	}
	else
	  m_DevHttpHost = 0;
	
	if (strcmp(netSktxHostName,"") != 0)
	{
		m_PlaygroundHttpHostPort = netSktxHostPort != 0 ? netSktxHostPort: HTTP_PORT;
		m_PlaygroundHttpHost = (const char *) netSktxHostName;
		if ( strcmp(m_DevHttpHost, m_PlaygroundHttpHost) != 0)
			m_PlaygroundHttpServerIP = getIPForHost(m_PlaygroundHttpHost);
		else
			m_PlaygroundHttpServerIP = m_DevHttpServerIP;
	}
	else
		m_PlaygroundHttpHost = 0;
			
	m_CSDBServerIP = getIPForHost( CSDB_HOST );
	m_NTPServerIP  = getIPForHost( NTPServer );
	#ifndef WITH_RENDER
	 clearErrorMsg(); //on c64screen, kernel menu
  #endif
	return true;
}

boolean CSidekickNet::mountSDDrive()
{
	if (f_mount (&m_FileSystem, DRIVE, 1) != FR_OK)
	{
		logger->Write ("CSidekickNet::Initialize", LogError,
				"Cannot mount drive: %s", DRIVE);
		return false;
	}
	return true;
}

boolean CSidekickNet::unmountSDDrive()
{
	if (f_mount ( 0, DRIVE, 0) != FR_OK)
	{
		logger->Write ("CSidekickNet::Initialize", LogError,
				"Cannot unmount drive: %s", DRIVE);
		return false;
	}
	return true;
}		

boolean CSidekickNet::RaspiHasOnlyWLAN()
{
	return (m_PiModel == MachineModel3APlus );
}

boolean CSidekickNet::Prepare()
{
	if ( m_PiModel != MachineModel3APlus && m_PiModel != MachineModel3BPlus)
	{
		logger->Write( "CSidekickNet::Initialize", LogWarning, 
			"Warning: The model of Raspberry Pi you are using is not a model supported by Sidekick64/264!"
		);
	}
	
	if ( RaspiHasOnlyWLAN() && !m_useWLAN )
	{
		logger->Write( "CSidekickNet::Initialize", LogNotice, 
			"Your Raspberry Pi model (3A+) doesn't have an ethernet socket. Skipping init of CNetSubSystem."
		);
		//                 "012345678901234567890123456789012345XXXX"
		setErrorMsgC64((char*)" No WLAN support in this kernel. Sorry. ");
		return false;
	}
	
	if (!m_USBHCI.Initialize ())
	{
		logger->Write( "CSidekickNet::Initialize", LogNotice, 
			"Couldn't initialize instance of CUSBHCIDevice."
		);
		setErrorMsgC64((char*)"Error on USB init. Sorry.");
		return false;
	}
	if (!mountSDDrive())
	{
		setErrorMsgC64((char*)"Can't mount SD card. Sorry.");
		return false;
	}	

	CGlueStdioInit (m_FileSystem);

	if (m_useWLAN)
	{
		#ifdef WITH_WLAN
		if (!m_WLAN.Initialize ())
		{
			logger->Write( "CSidekickNet::Initialize", LogNotice, 
				"Couldn't initialize instance of WLAN."
			);
			return false;
		}
		#endif
	}

	if (!m_Net.Initialize (false))
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
		if (!m_WPASupplicant.Initialize ())
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

void CSidekickNet::queueKernelUpdate()
{ 
	m_isKernelUpdateQueued = true; 
	//                                 "012345678901234567890123456789012345XXXX"
	m_networkActionStatusMsg = (char*) "  Trying to update kernel. Please wait. ";
	m_queueDelay = 1;
}


void CSidekickNet::queueFrameRequest()
{
 	m_isFrameQueued = true;
	m_queueDelay = 0;
}

void CSidekickNet::queueSktxKeypress( int key )
{
	m_isSktxKeypressQueued = true;
	m_sktxKey = key;
	m_queueDelay = 0;
}

void CSidekickNet::queueSktxRefresh()
{
	//refesh when user didn't press a key
	//this has to be quick for multiplayer games (value 4)
	//and can be slow for csdb browsing (value 16)
	m_skipSktxRefresh++;
	if ( m_skipSktxRefresh > 16 )
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
		type = 10;
		logger->Write ("CSidekickNet::getCSDBDownloadLaunchType", LogNotice, "CRT detected: >%s<",m_CSDBDownloadExtension);
	}
//	else if ( strcmp( m_CSDBDownloadExtension, "d64" ) == 0)
//			type = 9999;
	else //prg
	{
		type = 40;
		logger->Write ("CSidekickNet::getCSDBDownloadLaunchType", LogNotice, "PRG detected: >%s<",m_CSDBDownloadExtension);
	}
	//type=9;
	return type;
}

void CSidekickNet::handleQueuedNetworkAction()
{
	#ifdef WITH_WLAN
	if (m_isActive && !isAnyNetworkActionQueued())
	{
		m_effortsSinceLastEvent++;
		if ( m_effortsSinceLastEvent > 200)
		{
			//Circle42 offers experimental WLAN, but it seems to
			//disconnect very quickly if there is not traffic.
			//This can be very annoying.
			//As a WLAN keep-alive, we auto queue a network event
			//to avoid WLAN going into "zombie" disconnected mode
			UpdateTime();
			m_effortsSinceLastEvent = 0;
		}
	}
	#endif
	
	if (m_queueDelay > 0 )
	{
		m_queueDelay--;
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
		m_effortsSinceLastEvent = 0;
		return;
	}
	else if (m_isActive)
	{
		if ( m_isKernelUpdateQueued )
		{
			CheckForSidekickKernelUpdate();
			m_isKernelUpdateQueued = false;
			m_effortsSinceLastEvent = 0;
			#ifndef WITH_RENDER
			clearErrorMsg(); //on c64screen, kernel menu
			#endif
		}
		
		else if (m_isFrameQueued)
		{
			#ifdef WITH_RENDER
			updateFrame();
			#endif
			m_isFrameQueued = false;
			m_effortsSinceLastEvent = 0;
		}

		else if (m_isCSDBDownloadQueued)
		{
			logger->Write( "handleQueuedNetworkAction", LogNotice, "m_CSDBDownloadPath: %s", m_CSDBDownloadPath);		
			m_isCSDBDownloadQueued = false;
			getCSDBBinaryContent( m_CSDBDownloadPath );
			m_effortsSinceLastEvent = 0;
			m_isSktxKeypressQueued = false;
		}
		
		else if (m_isSktxKeypressQueued)
		{
			updateSktxScreenContent();
			m_isSktxKeypressQueued = false;
			m_effortsSinceLastEvent = 0;
		}
	}
}

boolean CSidekickNet::isAnyNetworkActionQueued()
{
	return m_isNetworkInitQueued || m_isKernelUpdateQueued || m_isFrameQueued || m_isSktxKeypressQueued;
}

boolean CSidekickNet::isPRGDownloadReady()
{
	boolean bTemp = m_isPRGDownloadReady;
	if ( m_isPRGDownloadReady){
		m_isPRGDownloadReady = false;
		if ( m_bSaveCSDBDownload2SD )
		{
			logger->Write( "isPRGDownloadReady", LogNotice, 
				"Now trying to write downloaded file to SD card, bytes to write: %i", prgSizeLaunch
			);
			writeFile( logger, DRIVE, m_CSDBDownloadSavePath, (u8*) prgDataLaunch, prgSizeLaunch );
			m_CSDBDownloadSavePath = "";
			m_CSDBDownloadPath = (char*)"";
			m_CSDBDownloadFilename = (char*)"";
			m_bSaveCSDBDownload2SD = false;
		}
		//unmount - this is only necessary as long as we don't have the concept
		//to mount the filesystem once and for all during runtime
		unmountSDDrive();
		//m_tryFilesystemRemount = true;
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
	//logger->Write( "getTimeString", LogDebug, "%s ", Buffer);
	return Buffer;
}

CString CSidekickNet::getRaspiModelName()
{
	return m_pMachineInfo->Get()->GetMachineName();
}

CNetConfig * CSidekickNet::GetNetConfig(){
	assert (m_isActive);
	return m_Net.GetConfig ();
}

CIPAddress CSidekickNet::getIPForHost( const char * host )
{
	assert (m_isActive);
	unsigned attempts = 0;
	CIPAddress ip;
	while ( attempts < 3)
	{
		attempts++;
		if (!m_DNSClient.Resolve (host, &ip))
			logger->Write ("CSidekickNet::getIPForHost", LogWarning, "Cannot resolve: %s",host);
		else
		{
			logger->Write ("CSidekickNet::getIPForHost", LogNotice, "DNS resolve ok for: %s",host);
			break;
		}
	}
	return ip;
}

boolean CSidekickNet::UpdateTime(void)
{
	assert (m_isActive);
	CNTPClient NTPClient (&m_Net);
	unsigned nTime = NTPClient.GetTime (m_NTPServerIP);
	if (nTime == 0)
	{
		logger->Write ("CSidekickNet::UpdateTime", LogWarning, "Cannot get time from %s",
					(const char *) NTPServer);

		return false;
	}

	if (CTimer::Get ()->SetTime (nTime, FALSE))
	{
		logger->Write ("CSidekickNet::UpdateTime", LogNotice, "System time updated");
		return true;
	}
	else
	{
		logger->Write ("CSidekickNet::UpdateTime", LogWarning, "Cannot update system time");
	}
	return false;
}

//looks for the presence of a file on a pre-defined HTTP Server
//file is being read and stored on the sd card
boolean CSidekickNet::CheckForSidekickKernelUpdate()
{
	if ( strcmp(m_DevHttpHost, "") == 0 )
	{
		logger->Write( "CSidekickNet::CheckForSidekickKernelUpdate", LogNotice, 
			"Skipping check: Update server is not defined."
		);
		return false;
	}
	/*
	if ( strcmp( m_SidekickKernelUpdatePath,"") == 0 )
	{
		logger->Write( "CSidekickNet::CheckForSidekickKernelUpdate", LogNotice, 
			"Skipping check: HTTP update path is not defined."
		);
		return false;
	}*/
	assert (m_isActive);
	unsigned iFileLength = 0;
	char pFileBuffer[nDocMaxSize+1];	// +1 for 0-termination
	unsigned type = 0;
	if ( m_SidekickKernelUpdatePath == 264 ) type = 1;
		 
	if ( HTTPGet ( m_DevHttpServerIP, m_DevHttpHost, m_DevHttpHostPort, kernelUpdatePath[type], pFileBuffer, iFileLength))
	{
		logger->Write( "SidekickKernelUpdater", LogNotice, 
			"Now trying to write kernel file to SD card, bytes to write: %i", iFileLength
		);
		writeFile( logger, DRIVE, KERNEL_SAVE_LOCATION, (u8*) pFileBuffer, iFileLength );
		m_pScheduler->MsSleep (500);
		logger->Write( "SidekickKernelUpdater", LogNotice, "Finished writing kernel to SD card");
	}
	if ( HTTPGet ( m_DevHttpServerIP, m_DevHttpHost, m_DevHttpHostPort, prgUpdatePath[type], pFileBuffer, iFileLength))
	{
		logger->Write( "SidekickKernelUpdater", LogNotice, 
			"Now trying to write rpimenu.prg file to SD card, bytes to write: %i", iFileLength
		);
		writeFile( logger, DRIVE, RPIMENU64_SAVE_LOCATION, (u8*) pFileBuffer, iFileLength );
		m_pScheduler->MsSleep (500);
		logger->Write( "SidekickKernelUpdater", LogNotice, "Finished writing file rpimenu.prg to SD card");
	}
	
	return true;
}

void CSidekickNet::getCSDBContent( const char * fileName, const char * filePath){
	assert (m_isActive);
	unsigned iFileLength = 0;
	char * pFileBuffer = new char[nDocMaxSize+1];	// +1 for 0-termination
	HTTPGet ( m_CSDBServerIP, CSDB_HOST, 443, filePath, pFileBuffer, iFileLength);
	logger->Write( "getCSDBContent", LogNotice, "HTTPS Document length: %i", iFileLength);
}

void CSidekickNet::getCSDBBinaryContent( char * filePath ){
	assert (m_isActive);
	unsigned iFileLength = 0;
	unsigned char prgDataLaunchTemp[ 1025*1024 ]; // TODO do we need this?
	if (HTTPGet ( m_CSDBServerIP, CSDB_HOST, 443, (char *) filePath, (char *) prgDataLaunchTemp, iFileLength)){
		m_isPRGDownloadReady = true;
		prgSizeLaunch = iFileLength;
		memcpy( prgDataLaunch, prgDataLaunchTemp, iFileLength);
	}
	else{
		setErrorMsgC64((char*)"https request failed (press d).");
	}
	
	logger->Write( "getCSDBBinaryContent", LogNotice, "HTTPS Document length: %i", iFileLength);
}

void CSidekickNet::getCSDBLatestReleases(){
	getCSDBContent( "latestreleases.php", "/rss/latestreleases.php" );
}

//for kernel render example
#ifdef WITH_RENDER

void CSidekickNet::updateFrame(){
	if (!m_isActive || m_PlaygroundHttpHost == 0)
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
		
	HTTPGet ( m_PlaygroundHttpServerIP, m_PlaygroundHttpHost, m_PlaygroundHttpHostPort, path, (char*) logo_bg_raw, iFileLength);
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

void CSidekickNet::launchSktxSession(){
	char * pResponseBuffer = new char[33];	// +1 for 0-termination
	CString path = "/sktx.php?session=new";
	if (HTTPGet ( m_PlaygroundHttpServerIP, m_PlaygroundHttpHost, m_PlaygroundHttpHostPort, path, pResponseBuffer, m_sktxResponseLength))
	{
		if ( m_sktxResponseLength > 25 && m_sktxResponseLength < 34){
			m_sktxSessionID = pResponseBuffer;
			m_sktxSessionID[m_sktxResponseLength] = '\0';
			logger->Write( "launchSktxSession", LogNotice, "Got session id: %s", m_sktxSessionID);
		}
	}
}

void CSidekickNet::updateSktxScreenContent(){
	if (!m_isActive || m_PlaygroundHttpHost == 0)
	{
		m_sktxScreenContent = (unsigned char *) msgNoConnection; //FIXME: there's a memory leak in here
		return;
	}
	char pResponseBuffer[4097]; //maybe turn this into member var when creating new sktx class?
  CString path = "/sktx.php?";
	
	if ( m_sktxSession < 1){
		launchSktxSession();
		m_sktxSession = 1;
	}
	
	CString Number; 
	Number.Format ("%02X", m_sktxKey);
	path.Append( "&key=" );
	path.Append( Number );

	path.Append( "&sessionid=" );
	path.Append( m_sktxSessionID );
	
	if ( m_sktxSession == 2) //redraw
	{
		m_sktxSession = 1;
		path.Append( "&redraw=1" );
	}
	m_sktxKey = 0;
	if (HTTPGet ( m_PlaygroundHttpServerIP, m_PlaygroundHttpHost, m_PlaygroundHttpHostPort, path, pResponseBuffer, m_sktxResponseLength))
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
				logger->Write( "updateSktxScreenContent", LogNotice, "download path: >%s<", CSDBDownloadPath);
				memcpy( CSDBFilename, &pResponseBuffer[ 4 + tmpUrlLength  ], tmpFilenameLength );
				logger->Write( "updateSktxScreenContent", LogNotice, "filename: >%s<", CSDBFilename);
				memcpy( extension, &pResponseBuffer[ m_sktxResponseLength -3 ], 3);
				logger->Write( "updateSktxScreenContent", LogNotice, "extension: >%s<", extension);

				logger->Write( "updateSktxScreenContent", LogNotice, "filename: >%s<", CSDBFilename);

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
					savePath.Append( CSDBFilename);
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

unsigned char * CSidekickNet::GetSktxScreenContentChunk( u16 & startPos, u8 &color )
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
	color        = m_sktxScreenContent[ m_sktxScreenPosition + 4 ];//0-15, here we have some bits
	
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

boolean CSidekickNet::HTTPGet ( CIPAddress ip, const char * pHost, int port, const char * pFile, char *pBuffer, unsigned & nLengthRead)
{
	assert (m_isActive);
	assert (pBuffer != 0);
	
	//this remount stuff is only temporarily necessary as long as we don't have 
	//the sd filesystem mounted consistently all the time during runtime
	//it became necessary when we started to implement https requests
	//as in case of https some certificate files are requested from sd
	//so we can limit the remount hack to those requests where port 443 is used.
	boolean isHTTPS = port == 443;
	//if (m_tryFilesystemRemount){
		//m_tryFilesystemRemount = false;
		if (isHTTPS)
		{
			logger->Write( "HTTPGet", LogNotice, "Trying to remount filesystem");
			mountSDDrive();
		}
	//}
	
	CString IPString;
	ip.Format (&IPString);
	if (isHTTPS )
		logger->Write( "HTTPGet", LogNotice, 
			"GET: https://%s%s (%s)", pHost, pFile, (const char *) IPString);
	else if (port == 80)
		logger->Write( "HTTPGet", LogNotice, 
			"GET: http://%s%s (%s)", pHost, pFile, (const char *) IPString);
	else
		logger->Write( "HTTPGet", LogNotice, 
			"GET to http://%s:%i%s (%s)", pHost, port, pFile, (const char *) IPString);
	unsigned nLength = nDocMaxSize;
	CHTTPClient Client (&m_TLSSupport, ip, port, pHost, isHTTPS); //TODO put this into member var?
	THTTPStatus Status = Client.Get (pFile, (u8 *) pBuffer, &nLength);
	if (Status != HTTPOK)
	{
		logger->Write( "HTTPGet", LogError, "HTTP request failed (status %u)", Status);
		return false;
	}
	logger->Write( "HTTPGet", LogDebug, "%u bytes received", nLength);
	assert (nLength <= nDocMaxSize);
	pBuffer[nLength] = '\0';
	nLengthRead = nLength;
	return true;
}

void CSidekickNet::updateSystemMonitor( size_t freeSpace, unsigned CpuTemp)
{
	m_sysMonHeapFree = freeSpace;
	m_sysMonCPUTemp = CpuTemp;
//	if ( m_effortsSinceLastEvent > 50)
//		logger->Write( "Net/SystemMonitor", LogNotice, "Free Heap Space: %i KB, CPU temperature: %i", freeSpace/1024, CpuTemp);
}

CString CSidekickNet::getSysMonInfo()
{
	CString m_sysMonInfo = "";
	m_sysMonInfo.Append("rbpi: cpu temp: ");
	CString Number; 
	Number.Format ("%02d", m_sysMonCPUTemp);
	m_sysMonInfo.Append( Number );
	m_sysMonInfo.Append("'c, ");
	CString Number2; 
	Number2.Format ("%02d", m_sysMonHeapFree/1024);
	m_sysMonInfo.Append( Number2 );
	m_sysMonInfo.Append(" kb free");
	return m_sysMonInfo;
}
