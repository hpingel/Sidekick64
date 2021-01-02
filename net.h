/*
  _________.__    .___      __   .__        __        _________   ________   _____  
 /   _____/|__| __| _/____ |  | _|__| ____ |  | __    \_   ___ \ /  _____/  /  |  | 
 \_____  \ |  |/ __ |/ __ \|  |/ /  |/ ___\|  |/ /    /    \  \//   __  \  /   |  |_
 /        \|  / /_/ \  ___/|    <|  \  \___|    <     \     \___\  |__\  \/    ^   /
/_______  /|__\____ |\___  >__|_ \__|\___  >__|_ \     \______  /\_____  /\____   | 
        \/         \/    \/     \/       \/     \/            \/       \/      |__| 
 
 net.h

 RasPiC64 - A framework for interfacing the C64 and a Raspberry Pi 3B/3B+
          - misc code and lots of macros
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

//to use SidekickNet without circle-stdlib and without HTTPS,
//this define WITH_TLS may be removed
#define WITH_TLS

#include <circle/interrupt.h>
#include <circle/timer.h>
#include <circle/logger.h>
#include <circle/machineinfo.h>
#include <circle/sched/scheduler.h>
#include <circle/usb/usbhcidevice.h>
#include <circle/util.h>
#include <SDCard/emmc.h>

#include "webserver.h"

#include <circle_glue.h>
#ifdef WITH_TLS
#include <circle-mbedtls/tlssimplesupport.h>
#include <circle-mbedtls/httpclient.h>
#endif

#ifdef WITH_WLAN
#include <wlan/bcm4343.h>
#include <wlan/hostap/wpa_supplicant/wpasupplicant.h>
#endif

#include <circle/net/netsubsystem.h>
#include <circle/net/dnsclient.h>

#include "lowlevel_arm64.h"
class CKernelMenu;
#include "kernel_menu.h"

#ifndef _sidekicknet_h
#define _sidekicknet_h

extern CLogger *logger;

#ifdef WITH_TLS
using namespace CircleMbedTLS;
#endif

class CSidekickNet
{
public:
	CSidekickNet( CInterruptSystem *, CTimer *, CScheduler *, CEMMCDevice *, CKernelMenu * );
	~CSidekickNet( void )
	{
	};
	CSidekickNet * GetPointer(){ return this; };
	boolean ConnectOnBoot (void);
	boolean usesWLAN (void);
	boolean Initialize ( void );
	boolean IsRunning ( void );
	//boolean CheckForSidekickKernelUpdate ();
	boolean UpdateTime (void);
	#ifdef WITH_RENDER
	void updateFrame();
	#endif
	void checkForSupportedPiModel();
	void updateSktpScreenContent();
	void queueNetworkInit();
	void queueFrameRequest();
	void queueSktpKeypress( int );
	void queueSktpRefresh();
	void handleQueuedNetworkAction();
	void getCSDBBinaryContent();
	u8 getCSDBDownloadLaunchType();
	boolean isAnyNetworkActionQueued();
	void saveDownload2SD();
	void cleanupDownloadData();
	boolean checkForFinishedDownload();
	boolean checkForSaveableDownload();
	boolean isDownloadReadyForLaunch();
	boolean isWireless(){ return m_useWLAN;};
	boolean RaspiHasOnlyWLAN();
	boolean IsSktpScreenContentEndReached();
	boolean IsSktpScreenToBeCleared();
	boolean IsSktpScreenUnchanged();
	char * getNetworkActionStatusMessage();
  unsigned char * getSktpScreenContent(){ return m_sktpScreenContent; };
	unsigned char * GetSktpScreenContentChunk( u16 & startPos, u8 &color, boolean & inverse );
	CString getTimeString();
	CString getUptime();
	CNetConfig * GetNetConfig();
	CString getRaspiModelName();
	CString getSysMonInfo( unsigned );
	void ResetSktpScreenContentChunks();
	void setErrorMsgC64( char * msg );
	void resetSktpSession();
	boolean launchSktpSession();
	void redrawSktpScreen();
	boolean isSktpSessionActive();
	CString getSktpPath( unsigned key );
	void updateSystemMonitor( size_t, unsigned);
	char * getCSDBDownloadFilename();
	boolean mountSDDrive();
	boolean unmountSDDrive();
	CUSBHCIDevice * getInitializedUSBHCIDevice();
	boolean initializeUSBHCIDevice();
	boolean disableActiveNetwork(); //this doesn't work
	boolean isReturnToMenuRequired();
	boolean isRebootRequested();
	void requestReboot();
	void requireCacheWellnessTreatment();
	void getNetRAM( u8 *, u32 *);
	void prepareLaunchOfUpload( char * );
	CString getLoggerStringForHost( CString hostname, int port);

private:

	typedef struct  {
		CString hostName;
		int port;
		CIPAddress ipAddress;
		CString logPrefix;
	} remoteHTTPTarget;
	
	boolean Prepare ();
	CIPAddress getIPForHost( const char * );
	boolean HTTPGet (remoteHTTPTarget & target, const char * path, char *pBuffer, unsigned & nLengthRead);
	
	CUSBHCIDevice     * m_USBHCI;
	CMachineInfo      * m_pMachineInfo; //used for c64screen to display raspi model name
	CScheduler        * m_pScheduler;
	CInterruptSystem	* m_pInterrupt;
	CTimer            * m_pTimer;
	CEMMCDevice		    m_EMMC;
	CKernelMenu       * m_kMenu;
	CNetSubSystem     * m_Net;
#ifdef WITH_WLAN
	CBcm4343Device    * m_WLAN;
#endif
	FATFS             m_FileSystem;
	CIPAddress        m_NTPServerIP;
#ifdef WITH_WLAN
	CWPASupplicant    * m_WPASupplicant;	
#endif
  CDNSClient        * m_DNSClient;
#ifdef WITH_TLS	
	CTLSSimpleSupport * m_TLSSupport;
#endif	
	CActLED							m_ActLED;
	CWebServer        * m_WebServer;
	
	boolean m_useWLAN;
	boolean m_isFSMounted;
	boolean m_isActive;
	boolean m_isPrepared;
	boolean m_isUSBPrepared;
	boolean m_isNetworkInitQueued;
	//boolean m_isKernelUpdateQueued;
	boolean m_isFrameQueued;
	boolean m_isSktpKeypressQueued;
	boolean m_isCSDBDownloadQueued;
	boolean m_isCSDBDownloadSavingQueued;
	boolean m_isDownloadReady;
	boolean m_isDownloadReadyForLaunch;
	boolean m_isRebootRequested;
	boolean m_isReturnToMenuRequested;
	char * m_networkActionStatusMsg;
	unsigned char * m_sktpScreenContent;
	char * m_sktpSessionID;
	char * m_CSDBDownloadPath;
	char * m_CSDBDownloadExtension;
	char * m_CSDBDownloadFilename;
	remoteHTTPTarget m_CSDBDownloadHost;
	CString m_CSDBDownloadSavePath;
	boolean m_bSaveCSDBDownload2SD;
	TMachineModel m_PiModel;
	unsigned char m_sktpScreenContentChunk[8192];
	//unsigned m_SidekickKernelUpdatePath;
	unsigned m_queueDelay;
	unsigned m_timestampOfLastWLANKeepAlive;
	unsigned m_timeoutCounterStart;
	unsigned m_skipSktpRefresh;
	unsigned m_sktpScreenPosition;
	char * m_sktpResponseBuffer[8193];
	unsigned m_sktpResponseLength;
	unsigned m_sktpResponseType;
	unsigned m_sktpKey;
	unsigned m_sktpSession;
	unsigned m_videoFrameCounter;
	size_t m_sysMonHeapFree;
	unsigned m_sysMonCPUTemp;
	unsigned m_loglevel;
	
	remoteHTTPTarget m_Playground;
	remoteHTTPTarget m_CSDB;
	
};

#endif
