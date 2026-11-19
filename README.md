# PS3LoadPlugin

This application is a companion tool for [webMAN MOD](https://github.com/aldostools/webman-mod) 
enabling SELF applications to be launched over the network using the ps3load client. 

This tool runs as a background service enabling SELF applications to be 
launched from the XMB, in-game, etc.

Please note that PS3LoadPlugin is still experimental at this time.

## Requirements

I am targeting Evilnat 4.93 CEX Cobra 8.5 with webMAN MOD [FULL] as my development environment. 
It is untested on any other firmware or version at this time. 
I am confident it will work with webMAN MOD [LITE].

```★ app_home/PS3_GAME``` (aka ```★ PlayStation```) must be enabled in ```category_game.xml``` (aka Game column on XMB menu)

## Installation

1) Download ```ps3load_prx.sprx``` from the [releases](https://github.com/tbwcjw/ps3loadplugin/releases/latest) page.
2) Using FTP, copy the SPRX to ```/dev_hdd0/plugins/ps3load_prx.sprx```
3) At this point, you will need to add ```/dev_hdd0/plugins/ps3load_prx.sprx```
   to boot plugins. There are two ways to achieve this:

   Manual:
   - Navigate to ```/dev_hdd0/``` and open ```boot_plugins.txt```
   - Append ```/dev_hdd0/plugins/ps3load_prx.sprx``` to the bottom of the file.
   - Restart your system to take effect.

   Using webMAN MOD:
   - Navigate to the ```PS3MAPI``` page.
   - Scroll to ```VSH Plugins```.
   - In the first empty slot, enter ```/dev_hdd0/plugins/ps3load_prx.sprx```, and press ```Load```.
   - Press ```Save > boot_plugins.txt```.
   - No restart is required.

## Usage

To send .SELF (or zipped SELF file) from the client to the PC you will need the ```ps3load``` PC client.
ps3load/ps3load.exe is bundled with [PSL1GHT](https://github.com/ps3dev/PSL1GHT) tools. 

PS3LoadPlugin should always be running in the background awaiting a client. It will exit
any application to launch the .SELF that has been sent.

**Windows:**
```
set PS3LOAD = tcp:xxx.xxx.xxx.xxx			<< PS3 IP
ps3load.exe *.self/*.zip					<< * or name of .self or .zip file
```

**Linux:**
```
export PS3LOAD=tcp:xxx.xxx.xxx.xxx			<< PS3 IP
ps3load *.self/*.zip						<< * or name of .self or .zip file
```

The uploaded file lives at ```/dev_hdd0/tmp/ps3load.self```, or ```/dev_hdd0/tmp/ps3load/*.self``` (if zip).
These files are overwritten with each launch.

Due to a limitation of pkglaunch and the method in which this application mounts, at this time,
all arguments after the file are ignored.

## Common Errors

 - ```webMAN MOD not found, exiting```: Is webMAN MOD installed, and running? If it is and should be; restart the system.
 - ```Listen socket failed to bind```: The required port (4299) is probably in use by another application/process.

## Building

You will need the official Playstation 3 SDK 475, with the Visual Studio (2010/2013) extensions.

## Credits
 
 - [**miniz**](https://github.com/richgel999/miniz): Copyright 2013-2014 RAD Game Tools and Valve Software. Copyright 2010-2014 Rich Geldreich and Tenacious Software LLC. All Rights Reserved.
 - [**webMAN MOD**](https://github.com/aldostools/webman-mod): for the export stubs.

## License

 - This software is licensed under the [MIT License](https://github.com/tbwcjw/ps3loadplugin/LICENSE.md)
