//
// webserver.cpp
//
// Circle - A C++ bare metal environment for Raspberry Pi
// Copyright (C) 2015  R. Stange <rsta2@o2online.de>
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
#include "webserver.h"
#include <circle/chainboot.h>
#include <circle/logger.h>
#include <circle/machineinfo.h>
#include <circle/string.h>
#include <circle/util.h>
#include <circle/version.h>
#include <assert.h>
#include "helpers.h"
#include "config.h"
#include "lowlevel_arm64.h"

#define MAX_CONTENT_SIZE	4000

extern u32 prgSizeLaunch;
extern unsigned char prgDataLaunch[ 1025*1024 ] AAA;

// our content
static const char s_Index[] =
#include "webcontent/index.h"
;

static const char s_Tuning[] =
#include "webcontent/tuning.h"
;

static const u8 s_Style[] =
#include "webcontent/style.h"
;

static const u8 s_Favicon[] =
{
#include "webcontent/favicon.h"
};

static const char FromWebServer[] = "webserver";

CWebServer::CWebServer (CNetSubSystem *pNetSubSystem, u16 nPort,
				  unsigned nMaxMultipartSize, CSocket *pSocket, CSidekickNet * pSidekickNet)
:	CHTTPDaemon (pNetSubSystem, pSocket, MAX_CONTENT_SIZE, nPort, nMaxMultipartSize),
	m_nPort (nPort),
	m_nMaxMultipartSize (nMaxMultipartSize),
	m_SidekickNet( pSidekickNet )
{
}

CWebServer::~CWebServer (void)
{
}

CHTTPDaemon *CWebServer::CreateWorker (CNetSubSystem *pNetSubSystem, CSocket *pSocket)
{
	return new CWebServer (pNetSubSystem, m_nPort, m_nMaxMultipartSize, pSocket, m_SidekickNet);
}

::THTTPStatus CWebServer::GetContent (const char  *pPath,
					 const char  *pParams,
					 const char  *pFormData,
					 u8	     *pBuffer,
					 unsigned    *pLength,
					 const char **ppContentType)
{
	assert (pPath != 0);
	assert (ppContentType != 0);

	CString String;
	const u8 *pContent = 0;
	unsigned nLength = 0;

	if (   strcmp (pPath, "/") == 0
	    || strcmp (pPath, "/index.html") == 0)
	{
		const char *pMsg = 0;

		const char *pPartHeader;
		const u8 *pPartData;
		unsigned nPartLength;
		if (GetMultipartFormPart (&pPartHeader, &pPartData, &nPartLength))
		{
			assert (pPartHeader != 0);
			if (   strstr (pPartHeader, "name=\"kernelimg\"") != 0
			    && ( strstr (pPartHeader, "filename=\"kernel") != 0 || strstr (pPartHeader, "filename=\"rpi4_kernel") != 0 )
			    && strstr (pPartHeader, ".img\"") != 0
			    && nPartLength > 0)
			{
				assert (pPartData != 0);
				
				m_SidekickNet->requireCacheWellnessTreatment();

#ifndef IS264				
				#if RASPI >= 4
  				const char * filename = m_SidekickNet->usesWLAN() ? "SD:rpi4_kernel_sk64_wlan.img" : "SD:rpi4_kernel_sk64_net.img";
				#else
  				const char * filename = m_SidekickNet->usesWLAN() ? "SD:kernel_sk64_wlan.img" : "SD:kernel_sk64_net.img";
				#endif
#else
				#if RASPI >= 4
					const char * filename = m_SidekickNet->usesWLAN() ? "SD:rpi4_kernel_sk264_wlan.img" : "SD:rpi4_kernel_sk264_net.img";
				#else
					const char * filename = m_SidekickNet->usesWLAN() ? "SD:kernel_sk264_wlan.img" : "SD:kernel_sk264_net.img";
				#endif
#endif			
				logger->Write( FromWebServer, LogNotice, "Saving kernel image to SD card, length: %u", nPartLength);
				//logger->Write( FromWebServer, LogNotice, "Unlink.");
				//f_unlink("SD:kernel_sk64_net.img.old");
				//logger->Write( FromWebServer, LogNotice, "Rename.");
				//f_rename("SD:kernel_sk64_net.img","SD:kernel_sk64_net.img.old");
				//logger->Write( FromWebServer, LogNotice, "Write.");
				//writeFile( logger, "SD:", (const char *) "SD:kernel_sk64_net.img", (u8*) pPartData, nPartLength );
				writeFile( logger, "SD:", filename, (u8*) pPartData, nPartLength );
				//logger->Write( FromWebServer, LogNotice, "Written, now reboot requested.");
				m_SidekickNet->requireCacheWellnessTreatment();
				m_SidekickNet->requestReboot();
				//logger->Write( FromWebServer, LogNotice, "Reboot was requested.");
				
				pMsg = "Now rebooting into new kernel...";
			}
			else
			{
				char * type = "";
				if ( strstr (pPartHeader, ".PRG\"") || strstr (pPartHeader, ".prg\""))
				 	type = "prg";
				if ( strstr (pPartHeader, ".d64\"") || strstr (pPartHeader, ".D64\""))
					type = "d64";
				if ( strstr (pPartHeader, ".crt\"") || strstr (pPartHeader, ".CRT\""))
					type = "crt";
				if ( strstr (pPartHeader, ".sid\"") || strstr (pPartHeader, ".SID\""))
					type = "sid";
					
				if (strcmp(type,"") != 0)
				{
					prgSizeLaunch = nPartLength;
					memcpy( prgDataLaunch, pPartData, nPartLength);
					m_SidekickNet->prepareLaunchOfUpload( type );
					pMsg = "Now launching PRG/SID/D64/CRT...";
				}
				else
				{
					pMsg = "Invalid request";
				}
			}
		}
		else
		{
			pMsg = "Select the kernel image file to be loaded "
			       "and press the boot button!";
		}

		assert (pMsg != 0);
		String.Format (s_Index, pMsg, CIRCLE_VERSION_STRING,
			       CMachineInfo::Get ()->GetMachineName ());

		pContent = (const u8 *) (const char *) String;
		nLength = String.GetLength ();
		*ppContentType = "text/html; charset=UTF-8";
	}
	else if (strcmp (pPath, "/tuning.html") == 0)
	{
		const char *pMsg = 0;

		const char *pPartHeader;
		const u8 *pPartData;
		unsigned nPartLength;
		if (strcmp (pFormData, "") != 0)
		{
			unsigned l = strlen(pFormData);
			pMsg = pFormData;
			unsigned mode = 0;
			unsigned key = 0;
			unsigned vl = 0;
			char *value;
			for (unsigned c = 0; c <= l; c++)
			{
				if (mode == 0 )
				{
					if (c+1 < l && pFormData[c+1] == '=')
					{
						key = atoi( & pFormData[c]);
						//logger->Write( FromWebServer, LogNotice, "Parsing form data: Key detected: %i", key);
						value = "";
						vl = 0;
						mode = 1;
					}
					else
					{
						logger->Write( FromWebServer, LogNotice, "Parsing form data: Error on finding key. %i / %i", c,l);
					}
				}
				if (mode == 1 && pFormData[c] == '=')
				{
					if ( c+1 < l)
					{
						//logger->Write( FromWebServer, LogNotice, "Parsing form data: Found = where we wanted it.");
						mode = 2;
					}
					else
					{
						logger->Write( FromWebServer, LogNotice, "Parsing form data: Error looking for =.");
						mode = 0;
						break; //end of string, no value left after key =
					}
				}
				else if (mode == 2)
				{
					if ( c == l || pFormData[c] == '&') //pFormData[c] == '\0')
					{
						logger->Write( FromWebServer, LogNotice, "Parsing form data: Assigning timingValue key=%i value=%i", key, atoi(value));
						
						if ( atoi(value) > 0)
						switch( key )
						{
							case 0:
								WAIT_FOR_SIGNALS = atoi(value);
								break;
							case 1:
								WAIT_CYCLE_READ = atoi(value);
								break;
							case 2:
								WAIT_CYCLE_WRITEDATA = atoi(value);
								break;
							case 3:
								WAIT_CYCLE_READ_BADLINE = atoi(value);
								break;
							case 4:
								WAIT_CYCLE_READ_VIC2 = atoi(value);
								break;
							case 5:
								WAIT_CYCLE_WRITEDATA_VIC2 = atoi(value);
								break;
							case 6:
								WAIT_CYCLE_MULTIPLEXER = atoi(value);
								break;
							case 7:
								WAIT_CYCLE_MULTIPLEXER_VIC2 = atoi(value);
								break;
							case 8:
								WAIT_TRIGGER_DMA = atoi(value);
								break;
							case 9:
								WAIT_RELEASE_DMA = atoi(value);
								break;
						}
						value = "";
						vl = 0;
						mode = 0;
					}
					else
					{
						value[vl] = pFormData[c];
						value[++vl] = '\0';
						mode = 2;
					}
				}
			}
		}
		else
		{
			pMsg = "Please change params to your needs.";
		}

		
		int timingValues[ 10 ] = { 40, 475, 470, 400, 425, 505, 200, 265, 600, 600 };

		timingValues[ 0 ] = WAIT_FOR_SIGNALS;
		timingValues[ 1 ] = WAIT_CYCLE_READ;
		timingValues[ 2 ] = WAIT_CYCLE_WRITEDATA;
		timingValues[ 3 ] = WAIT_CYCLE_READ_BADLINE;
		timingValues[ 4 ] = WAIT_CYCLE_READ_VIC2;
		timingValues[ 5 ] = WAIT_CYCLE_WRITEDATA_VIC2;
		timingValues[ 6 ] = WAIT_CYCLE_MULTIPLEXER;
		timingValues[ 7 ] = WAIT_CYCLE_MULTIPLEXER_VIC2;
		timingValues[ 8 ] = WAIT_TRIGGER_DMA;
		timingValues[ 9 ] = WAIT_RELEASE_DMA;

		CString formMarkup = "";
		formMarkup.Append("<pre>");
		formMarkup.Append(pMsg);
		formMarkup.Append("</pre>");
		for ( int i = 0; i < 10; i++ )
		{
			formMarkup.Append("<tr><td>");
			formMarkup.Append(timingNames[ i ]);
			formMarkup.Append("</td><td><input type=\"text\" name=\"");
			CString Number;
			Number.Format ("%01d", i);
			formMarkup.Append( Number );
			formMarkup.Append("\" value=\"");
			Number.Format ("%02d", timingValues[ i ]);
			formMarkup.Append(Number);
			formMarkup.Append("\"/></td></tr>\n");
		}

		assert (formMarkup != 0);
		String.Format (s_Tuning, (const char *) formMarkup );

		pContent = (const u8 *) (const char *) String;
		nLength = String.GetLength ();
		*ppContentType = "text/html; charset=UTF-8";
	}
	else if (strcmp (pPath, "/style.css") == 0)
	{
		pContent = s_Style;
		nLength = sizeof s_Style-1;
		*ppContentType = "text/css";
	}
	else if (strcmp (pPath, "/favicon.ico") == 0)
	{
		pContent = s_Favicon;
		nLength = sizeof s_Favicon;
		*ppContentType = "image/x-icon";
	}
	else
	{
		return ::HTTPNotFound;
	}

	assert (pLength != 0);
	if (*pLength < nLength)
	{
		CLogger::Get ()->Write (FromWebServer, LogError,
					"Increase MAX_CONTENT_SIZE to at least %u", nLength);

		return ::HTTPInternalServerError;
	}

	assert (pBuffer != 0);
	assert (pContent != 0);
	assert (nLength > 0);
	memcpy (pBuffer, pContent, nLength);

	*pLength = nLength;

	return ::HTTPOK;
}
