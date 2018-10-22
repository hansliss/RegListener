========================================================================
    CONSOLE APPLICATION : RegListener Project Overview
========================================================================

This is a Windows Service that can listen to a single registry value (ok,
listen to changes on the key and check for changes in value) and log the
changes to a file with timestamps.

Note that due to the way Windows implements registry functionality for
32-bit and 64-bit applications, you will need to know what you're doing!

The current case is a 32-bit service listening for key changes on a key
used by another 32-bit application, so the configuration parameters need
to be installed using the 32-bit regedit.

Register the service with "sc" in an elevated CMD.EXE. Something like this:
	sc create "UU Registry Listener" binpath= c:\users\hans\applications\RegListener.exe
Notice the space after "binpath="! Read up on command line parsers if you
want to take a guess on the reason for this.
You don't need to remove it and re-register to install a new version of
the exe file. If you do need to remove it, do it like this:
	sc delete "UU Registry Listener"

Configuration values, located under
HKEY_LOCAL_MACHINE\SOFTWARE\UU\Registry Listener:
Root:
	HKLM = HKEY_LOCAL_MACHINE
	HKU = HKEY_USERS
	HKCU = HKEY_CURRENT_USER
	HKCR = HKEY_CLASSES_ROOT
	HCC = HKEY_CURRENT_CONFIG
Key: The key on which to listen for changes to the given value
Value: The value under the given key that we are listening to
File: Full path to the log file

Note, there is an example ".reg" file included with this project, containing
the configuration values according to the above. However, this .reg file is
meant for the 64-bit version of RegEdit.

-------------------------------------------------
Here's some background. Hold on to something:

There are two "views" of the registry in Windows, one for 64-bit
applications and one for 32-bit applications. The 32-bit registry
keys are stored in the same registry hive as the 64-bit keys, but
under a "virtualization" node called "Wow6432Node". There are also
FOUR versions of regedit available, two of them called "regedt32.exe"
and two called "regedit.exe".

The "normal" regedit.exe is a 64-bit application, and will show
the Wow6432Node nodes. It is located under C:\Windows on a normal
Windows box.

One of the regedt32.exe instances lives in C:\Windows\System32,
the standard location for 64-bit applications (yes!). This version
of regedt32.exe is a 64-bit application and can see the Wow6432Node
keys.

Under c:\Windows\SysWOW64, the home of 32-bit applications (again,
yes!), there is one regedt32.exe and one regedit.exe. Those two do
*not* see the Wow6432Node keys, but instead they see the keys
located below those nodes as if they were located in their normal
places.

If you are writing a 32-bit program and for some weird reason need
to access non-virtualized 64-bit registry keys, you add the access
flag KEY_WOW64_64KEY to the RegOpenKeyEx() call. Two "64" in one
constant name? Yes, because this is a constant for "WoW64", the
inexplicably named 32-bit subsystem on Win64, and you want to
access a "64KEY".

If you are writing a 64-bit program and need to access 32-bit keys,
insert "Wow6432Node" as a path component in the appropriate place.