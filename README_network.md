# Network features for Sidekick64
## Introduction
The main goal of this fork of Sidekick64 is to find out if it is possible to use the network capabilities of a Raspberry Pi in parallel with the emulation modes of Sidekick64 without meddling too much with any timings needed for reliably emulating C64 carts. Further goals were for me to learn how to use the Circle bare metal framework and to learn how Sidekick64 does its magic by trying to understand its source code.

Once network features were squeezed into the "clockwork" of Sidekick64 without disturbing it too much, I started to add some network demos and tried to implement modem emulation to make use of the newly gained network capabilities. Without the excellent network examples of the Circle bare metal framework this would have been much harder to achieve.
## Summary: Network features in a nutshell
Currently the following network related features are offered by the experimental Sidekick64 network kernel:
* Join your network, obtain IP address and stuff via DHCP (mandatory)
* Both cable based LAN (RPI 3B+ only) and WLAN are possible but require their own kernel images. There is one kernel image for WLAN and one image for cable based ethernet.
* WLAN-Kernel: Store WLAN SSID and passphrase in configuration file on SD card
* System date and time will be set via NTP (UTC)
* A network connection can be established by the user via keypress at the Sidekick menu when needed or can be configured to be automatically done during each Sidekick64 boot.
* Web interface
 	- A web interface can be activated for Sidekick64 that offers an upload form to other devices on the local network. This allows a Sidekick64 kernel update without removing the SD card or it allows to upload and launch PRGs, SIDs, CRTs, D64, BIN or the like remotely from a different machine.
 	- The web interface can be reached via the IP address or by hostname (default is "sidekick64" if no custom hostname was configured)
* Modem emulation
	- Only compatible with terminal software in PRG format (CCGMS, etc.)
	- Userport modem emulation is possible via a little hardware extension
	- (highly experimental) Swiftlink emulation
* Experimental SKTP browser
	- Load screen content to C64 via HTTP or HTTPS via a simple binary protocol from a web application called "SKTP server". 
	- Trigger download and launch of remote binary files like PRG, D64, SID, CRT, etc. 
	- Example apps:
		- Browse and download stuff from CSDb via webservice provided by CSDb.
		- Access to RSS-Feeds.
		- Join a simple text chat to chat with other SKTP users.
* Changes to the Sidekick64 menu
	- Added network menu page entry to main menu (open with key @, also allow access to with subpages "SKTP browser" and System information
	- Added SKTP browser: .... Control screen content and user key presses remotely via "SKTP server" (HTTP server with web application). Ability to download, launch and save files fetched via HTTP(S): PRG, SID, CRT, D64
	- Added system information page: Allows to see the current RPi CPU temperature, basic network connection information and also meta info like date and time and information about the Sidekick64 kernel running
* Outlook: Sidekick264 network support is also implemented in a highly experimental state. Due to the many variants that have to be tested Sidekick264 network has not been tested a lot.

## Joining a network
### Basics
Sidekick64 can connect to a local area network as long as a DHCP server is present on the network that provides Sidekick64 with an IP address and DNS server. In case of WLAN the SSID and passphrase are stored in an unencrypted text file on the SD card. The default hostname of a Sidekick64 is "sidekick64" - this can be changed - see section [Configuration parameters](#configuration-parameters) for details. On a successful connect, the date and time will be set automatically via NTP (at the moment this information is not readable for C64 applications but it is accessible in the Sidekick64 menu). The time zone will most likely still be incorrect so that the time is displayed a couple of hours off your local time. (A configurable offset needs to be implemented at some point and daylight saving times are to be considered then too.)

### Network on demand
The Sidekick64 kernel with network features doesn't force the user to always activate the network. Establishing a network connection on Sidekick64 means that the user has to wait for a couple of seconds until all the steps necessary - including turning on the USB stack - are finished. Besides the waiting time, an active network will currently lead to a higher CPU temperature and energy consumption of the Raspberry Pi (discussed in detail in the [hardware section](#usb-stack-power-consumption-and-cpu-temperature) below).

Additionally, a user might not need network connectivity with Sidekick64 on a daily basis. Because of this it seemed to make sense to make the network connection optional and offer a way to activate the network connection on demand via the Sidekick64 menu. This means that network only has to be turned on when it is really needed and wanted.

### Network on boot
On the other hand it also seemed to make sense to offer a configuration option to always directly establish a network connection during boot time of Sidekick64 if desired. Therefore, a setting can be added to enforce network on boot - see section [Configuration parameters](#configuration-parameters) for details. Enabling this will mean that the Sidekick64 will need several seconds longer to boot until it shows its normal menu screen on the C64.

## Web interface and web server
One way to transfer files from a PC, tablet or smartphone to Sidekick64 is to use the web interface that is provided by the built-in webserver. The web interface allows to upload Sidekick64 kernel images (overwrites the current Sidekick kernel on SD card and reboots), PRG files, SID files, CRT files, D64 files or BIN files (C128 custom roms). The file types targeted at the C64/C128 platform will be launched directly via Sidekick64 after the upload is finished.

The webserver can be launched manually from the Sidekick64 menu's network page by pressing "w" on the keyboard or it may also become active straight after a network connection is established by adding a configuration parameter - see section [Configuration parameters](#configuration-parameters) for details. In combination with network on boot this might be helpful for developers who want to test their own cross-developed C64 software sending it from a PC over to Sidekick64 to be executed on the real machine.

The web interface is currently only available unencrypted via HTTP (on port 80) and doesn't come with authentication or password protection.

The web interface UI is currently based on Bootstrap loaded via CDN.

## Stand-alone Sidekick mode (via web interface)
In theory it is possible to have the Sidekick64 cartridge lying on a table without being plugged-in to the expansion port of a Commodore computer and still being able to use the web interface to upload data to the SD card of Sidekick64. (This feature has not been tested very well so it is likely that it will not work completely at the moment.)

## Modem emulation
Two types of modems may be emulated as long as Sidekick is not busy with emulating something very demanding like an EasyFlash or a Freezer cartridge. As long as Sidekick is in launcher mode or in the BASIC prompt mode, modem emulation is possible with PRGs loaded via Sidekick or from a floppy disk. Terminal software used has to be in PRG format which is not a problem as CCGMS2021 and other tools all are available as PRGs.

Besides implementing two modem types a basic command line interface also had to be implemented to set the baud rate and connect to a BBS via hostname and port. The default baud rate can also be changed by setting the configuration parameter `NET_MODEM_DEFAULT_BAUDRATE` to a desired value. Check section [Configuration parameters](#configuration-parameters) for further details.

When it comes to the type of modem being emulated, there is a choice:
### Userport modem
A userport based UP9600 modem may be emulated with relatively simple cabling or with a helper PCB that plugs into the userport and is connected to Sidekick 64 via USB via a cheap FTDI-USB-adapter, the emulation currently works with a baud rate up to 4800.

For a reliable baud rate of 9600 flow control has to be implemented in Circle. The Sidekick kernel must be compiled with Circle option USE_USB_SOF_INTR to allow using the FTDI-USB-adapter.

### Swiftlink/Link232 modem (highly experimental)
A Swiftlink modem may be emulated (which normally would be connected to the expansion port). This is still highly experimental at the moment and will crash Sidekick64 after a couple of seconds.

## SKTP browser
SKTP jokingly stands for "Sidekick64 Transfer Protocol" and is a very simple and experimental binary protocol on top of HTTP that is so unspectacular that doesn't really deserve a name. Its main purpose is to allow the C64 to send keypresses or other events to a web application and in response get screen updates from a web application. Basically, the C64 works like a terminal and presents screen content that was arranged by a web application.
This enables us to do the following:

* Applications / features available through Sidekick64 don't have to run on Sidekick64 but can be running in a hosted or cloud environment. This means for example that changes and updates required to such an application will not force the Sidekick64 kernel to be updated.
* In theory it allows interactive multi user applications like simple games or chat apps.
* It allows interaction with most services on the World Wide Web
* In addition to screen updates SKTP allows to request Sidekick64 to download a payload from a web adress (via HTTP or HTTPS), store and/or execute it on the C64. This means, files like PRG, SID, CRT, etc. may be downloaded from the internet and directly launched on the C64. This is tightly coupled to the Sidekick64 menu code as the possibility to launch is essential.

Currently three example applications exist that make use of SKTP:
### CSDb Launcher
Allows to browse latest releases and a couple of selected top lists to easily access attractive releases from the world of the C64 demo scene.
### Forum64 RSS Viewer
Allows to launch a dynamically generated PRG (available in different flavours for C64, C128@80columns, C16/Plus/4) that displays the latest posts from the RSS feed of Forum64.
### Simple text chat
To test and demonstrate multiuser capabilities a simple text chat is available.
## Network configuration via SD card
You may change some network default settings by editing configuration files on the SD card via an SD card reader plugged into your Desktop/Notebook/PC/MAC/Pi/etc.

### Booting the right Sidekick64 kernel image

It should in theory be possible to switch between the normal Sidekick64 kernel (vanilla), the network kernel (Ethernet cable based) and the WLAN kernel while everything is stored on one SD card.

There might be problems with the Raspberry Pi firmware files which exist in older and newer revisions. The network kernels tend to use the latest Raspberry Pi firmware files while the vanilla kernel uses older ones.

The vanilla kernel is called `kernel_sk64.img` and is always in the root folder of the SD card. Network kernels have different file names (`kernel_sk64_net.img`, `kernel_sk64_wlan.img`) and should also be stored in the root folder.

Within the file `sidekick64.txt` in the root folder of the SD card you can clarify which kernel image should be booted when Sidekick64 is powered up. The vanilla kernel is booted by the line `kernel=kernel_sk64.img`. You can add lines below this line like `kernel=kernel_sk64_net.img` to boot the cable based network kernel instead. You may also disable lines by prefixing it with `#`. But there should always be one line that is set to active. Otherwise Sidekick64 will fail to boot.

### WLAN, SSID and passphrase

If you use the WLAN kernel, you have to check if you have a folder `wlan` on your SD card containing the Raspberry Pi firmware files for WLAN networking.

You also have to edit the file `wlan/wpa_supplicant.conf` and add SSID and passphrase in cleartext. For security and privacy reasons, you should not share this file with anybody and it is recommended that you use a "playground WLAN" (a separate WLAN access point to your normal one which may have restricted rights or may have a disposable/easily changeable passphrase).

### Configuration parameters

The following options may be added to the file `C64/sidekick64.cfg` but are not mandatory. A vanilla Sidekick64 kernel (without network features) will ignore these options if present.

* **NET_CONNECT_ON_BOOT**: Add the line `NET_CONNECT_ON_BOOT "1"` to the configuration if you always want to establish a network connection during boot when you power up your Sidekick64. The boot time will be much longer if enabled (at least 10 seconds longer). The C64 may be switched on even before Sidekick64 boot is finished. You can also revert to manual network activation via keypress in the network menu like this: `NET_CONNECT_ON_BOOT "0"`. Default value: "0"

* **NET_ENABLE_WEBSERVER**: If you want to automatically activate the web server/web interface each time a network connection was established, add the line `NET_ENABLE_WEBSERVER "1"` to the configuration. The web server listens on port 80 while the Sidekick64 menu is active or the PRG launcher is active (display says "Launcher"). You can also revert to manual web server start via keypress in the network menu like this: `NET_ENABLE_WEBSERVER "0"`. Default value: "0"

* **NET_SIDEKICK_HOSTNAME**: If you want to customize the hostname of your Sidekick64 you may do so by providing the hostname in a line like this: `NET_SIDEKICK_HOSTNAME "mySidekickNumberOne"`. This hostname may be used to access the web interface without the need to type in the IP address (example: http://mySidekickNumberOne). Default value: "sidekick64"

* **NET_MODEM_DEFAULT_BAUDRATE**: The modem emulation baud rate may be changed in a terminal program like CCGMS by using the command `atb`. But you can also configure your favourite baud rate by adding a line with this parameter like this: `NET_MODEM_DEFAULT_BAUDRATE "4800"`. Possible values are 300, 1200, 2400, 4800, 9600. Default value: "1200"

* **NET_SKTPHOST_NAME**: To use the SKTP browser it needs to know which external SKTP server to connect to. Provide an SKTP server address like in this example: `NET_SKTPHOST_NAME "example.com"`. Default value: none

* **NET_SKTPHOST_PORT**: If you have specified an SKTP host (see above) you may also provide a port number (only needed if this should differ from 80). For example, the line `NET_SKTPHOST_PORT "443"` will enforce HTTPS access. Default: "80"


## Differences to vanilla Sidekick64
I would like to point out some of the differences between the normal Sidekick64 release kernel and the network kernel so that people can inform themselves before testing the network kernel.
### USB stack, power consumption and CPU temperature
To be able to use the network devices of the Raspberry Pi we have to tell Circle to turn on the USB stack of the Raspberry Pi. Handling the USB stack during Sidekick64's runtime will mean that the single CPU core used by Sidekick64 is a little bit more busy than before. Because the CPU core is forced to run at a fixed maximum speed this will result in an increased CPU temperature and power consumption.

The power consumption change depends on the Raspberry Pi model used.  While a model 3A+ will probably have a very similar power consumption even with USB stack and WLAN turned on, a model 3B+ will double it's power consumption from around 350mA to around 700mA when ethernet is enabled. This has to be taken into account when considering if it is feasible to power Sidekick64 from the expansion port of the C64 and therefore from the C64 PSU.

Now for the CPU temperature: In an everyday use case the Raspberry Pi would throttle CPU cores once it detects that the CPU  is getting "hot" but in the case of Sidekick64 the reliability of the communication with the expansion port of the C64 is a priority and therefore throttling the Raspberry's CPU is not an option as it would destabilize the emulation which would lead to crashes or flickering menu screens. This may sound dangerous, but in fact there is not much harm done except for an emulation that was stable before suddenly becoming unstable.
While the CPU temperature of a running Sidekick64 would normally be below 55° Celsius, turning on network features and enabling the USB stack with it may eventually make the temperature hit 60° Celsius. This is not a problem as such but has consequences that need to be known and understood to circumvent problems.
With default settings, a Raspberry Pi will automatically start to throttle its CPU once the 60° C are reached to prevent overheating (although the CPU would be capable of running with more than 80°C without damage). If you observe a CPU temperature of 59°C on the system info screen of the network menu then your Sidekick64 emulation may appear to become unstable very soon. Nothing can break, nothing can be damaged, as the Raspberry is still running fine and just behaving as it should behave. Only the emulation appears unstable. 
If you detect that the Raspberry's CPU temperature is likely to reach the 60° C there are two directions in which you can go to solve the problem: Either improve the air circulation around the CPU to stop it from reaching 60°C or increase the default throttling temperature limit to something higher - like for example 62°C - and hope that the Raspberry's CPU will not reach the new limit.
How quickly your Sidekick's Raspberry CPU is heating up may depend on different factors:
*    Inside a case: Does the case allow for some airflow?
*    Raspberry Pi Model: The Raspberry Pi 3B+ is getting hotter than an 3A+. A model 3A+ will probably stay at around 57-58°C and not reach the 60° unless it is inside of a case.
*    PCB form factor: A Sidekick64 PCB v0.3 (outdated) offers more airflow to the Pi so that a 3B+ will stay beneath 60°C.

The most simple solution is to improve the airflow by using a little tiny active fan for 1€ that gets powered by the 3.3V pin on the Sidekick PCB. This is a custom hack that can even be done if the Sidekick is inside a case. Just let the fan blow air into the gap between Sidekick PCB and the Raspberry Pi from the side. (TODO: Add photos to illustrate)

### Changes to the Sidekick64 menu
#### New submenu "Network"
The network menu allows to check if a connection is established, to start a connection and afterwards to launch the web server or select the modem emulation type. It also provides another submenu called "System information" containing details like the IP address, CPU temperature and system time.
#### Enforced screen refreshes
Sidekick64 ships with a C64 assembler program that is responsible for fetching the menu screen content of Sidekick64 and displaying it via VIC2 on the C64 video output. This program called rpimenu.prg had to be modified for the network kernel to perform a C64 screen content redraw not only when a user presses a key on the keyboard but also when something important happens on the Raspberry Pi's side when the Sidekick64 cartridge will trigger a non-maskable interrupt (NMI) to tell the C64 program that new screen content needs to be fetched and displayed. This enforced refresh is necessary for screens that need to be redrawn regularly like the system info screen displaying the current date and time and CPU temperature of the Pi.

To avoid the hassle of constantly replacing the rpimenu.prg on the SD card when switching between vanilla kernel and network kernel, the modified rpimenu.prg is currently baked directly into the network kernel image so that there is no possibility to have the wrong version on the SD card and no need to replace any PRG files.
### Framework related differences
#### Bigger kernel image size
Although it doesn't present any problems given todays sizes of SD cards, the Sidekick64 network kernel images are generally bigger in file size than the original kernel (TODO: Add example numbers). Although the rpimenu.prg file is currently baked into the network kernel image to avoid confusion when switching between vanilla kernel and network kernel, these are only a couple of additional bytes. The majority of the gain in size is only explainable by highlighting that circle-stdlib is normally being used during compilation for the network kernel mainly for the reason to offer the option to use HTTPS (with the help of mbedtls and newlib).
#### Bare metal framework Circle and circle-stdlib
The Sidekick kernel software is based on the bare metal Raspberry Pi C++ framework Circle created by Rene Stange. While the official Sidekick software v0.48 (at the time of writing the latest release) uses Circle Step 43.1, the network enabled Sidekick fork is based on the latest available version - Circle Step 44.1. The network fork is also using the GCC 10 based toolchain version and Raspberry Pi firmware versions that are recommended to be used together with Circle Step 44.1.

Because it seemed to make a lot of sense to be able to make use of HTTPS requests  with a network enabled Sidekick, the network fork is commonly compiled against the project circle-stdlib that is maintained by Stephan Mühlstrasser. It combines Circle together with newlib as C++ standard library and already includes mbedtls. This means, the network fork is being compiled directly against circle-stdlib (at the time of writing circle-stdlib v15.8 including Circle Step 44.1).

There are multiple variants of the Makefile available within the network branch as sometimes we want to test something just with Circle and sometimes with circle-stdlib.
#### Network kernel variants
Because WLAN and Ethernet can not be supported at the same time with the same drivers by circle, there is the need to compile two different network kernel variants. One for WLAN, one for Ethernet based network.

This also means that at compile time some compilation flags have to be set that are different from vanilla Sidekick64:
*  Enabling USE_SD_HOST for the WLAN kernel (which conflicts with REALTIME)
*  Enabling slow USB devices (omit NO_USB_SOFT_INTR)
*  ARM_ALLOW_MULTI_CORE (at the moment it is unclear if this is beneficial)

### SD card: Mount state, potentially more write operations

During the runtime of Sidekick64 the SD card is normally only mounted for a short while it is being accessed. Right after an access it will be unmounted. With the network kernel enabled the SD card will stay mounted during the whole runtime of Sidekick64 as requests to the SD card may occur more frequently (for example in case of HTTPS requests where certificates may be searched for on the SD card). Write operations to the SD card may happen more often as network features may allow to download and store files.

## Known problems
### Raspberry Pi 3B+ needs a cooling airflow
This is explained in detail in the section [USB stack, power consumption and CPU temperature](#usb-stack-power-consumption-and-cpu-temperature). A Raspberry Pi 3A+ is less affected unless it is inside of an enclosure without any airflow.

### Recommended setting for jumper "A13-BTN"
Until the following is resolved by a software change it is recommended to leave the jumper A13-BTN open or set to BTN.

The Sidekick64 PCB can be configured to listen to the signal A13 of the C64/C128 by closing the "A13-BTN" jumper in position A13. The main purpose of this setting is to emulate the presence of a C128 function ROM in software (while the real socket U36 stays empty). If you are not running Sidekick64 with a C128 or while you are not trying to emulate a function ROM at a C128, the jumper setting A13 is not mandatory.

Background: During tests of the network kernel we have observed that the menu screen refresh might be affected if the signal jumper is set to A13. This is most obvious in the network setting subscreen of the Sidekick menu where the CPU temperature is displayed and the screen will automatically refresh itself regularly. The Sidekick64 network enabled kernel uses an NMI based screen refresh while the menu is active. It seems that the A13 signal influences the triggering of NMIs.

### Network reliability 
Depending on the current emulation type Sidekick64 may be too busy to handle network traffic in parallel without instabilities in the emulation of the current payload. This typically applies to cartridge emulation like EasyFlash, NeoRAM or Freezer cartridges. This means, network connectivity will hibernate while Sidekick64 emulates a cartridge. Pressing the Sidekick64 reset button for a more than one second will stop the emulation, bring the Sidekick menu back up and also continue network connectivity.

Cable-based ethernet with the Raspberry Pi 3B+ works reliable after hibernation but WLAN needs to reconnect to the Access point and may freeze the Sidekick64 if it fails to do so. In a lucky situation the reconnect only takes two or three seconds.

Emulating PRGs via the Launcher mode is more easy-going as an average PRG (game, music, demo) may not be interested at all to communicate to a expansion port cartridge. In those cases Sidekick64 doesn't  need to pay attention to what is going on the address and data lines of the expansion port.

This also means that during Launcher mode the WLAN connection will not be lost and WLAN can be enjoyed reliably over a longer period of time.

Uploading a "large" file via the web interface may take much longer via WLAN as compared to cable based network.

To wrap it up, WLAN is less convenient to use than ethernet via cable. In exchange a Raspberry Pi 3B+ with active network has the inconvenience of needing airflow to keep the CPU temperature steady.
### CRT files in root folder
While there is no problem with storing CRT files (cartridge images) in their destined locations on the SD card it is not recommended to put any CRT files into the root folder of the SD card as  mbedtls will likely look for certificate files in the root folder when HTTPS is being used.
### Git branch will regularly be rebased and history will be rewritten
This git repository is rebased frequently on top of Frenetic's latest Sidekick64 releases to make it as easy as possible to be merged into Frenetic's main git repository should it become stable enough for Frenetic to decide to integrate it with the mainline code. Regular rebases are necessary in this fork and will rewrite git history of this repository so it might be irritating to try to pull from this branch from time to time as local and remote history will not match.
# Credits
I would like to thank 
* Frenetic for releasing exciting open source retro projects like Sidekick64 and for his help on pointing me to the areas in his source code where I could safely try to add some network stuff. 
* Rene Stange for creating, sharing and documenting Circle with attention to detail and Stephan Mühlstrasser for creating and continuously updating circle-stdlib with the latest upstream components. 
* My network testers for their valuable feedback! (TODO: Add nickname list)
* I would also like to thank the CSDb admins for offering the CSDb web service and last but not least the very active C64 demo scene for still releasing exciting demos, music and other stuff.
