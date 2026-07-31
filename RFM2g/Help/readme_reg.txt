
!!! Notice !!!     !!! Notice !!!

This folder contains custom, restricted registry entries.
Do not install any of these files in the system registry unless instructed to 
do so by GE Intelligent Platforms Embedded Systems, Inc. Improper use of these files will result in
system malfunction. These files must be renamed from the *.rege to *.reg suffix
in order for the registry editor to interpret them properly.

Description:

1) rfm2g_marbr24.rege
   This modification is for system motherboards that are using:

     A) Serverworks "Grand Champion HE" or also known as GC-HE chipset
	This is currently in the Dell 6600 series servers
     B) Intel 7505
 	 This is currently in the Dell 650 servers
     C) Intel 7500
         On the Gigabyte GA-8IPXDR motherboard. Other motherboards containing
         the Intel RG82861 Host Controller and FW82801CA I/O Controller Hub
         may require this patch 

2) rfm2g_marbr28.rege
   PCI PIO Prefetch

3) rfm2g_lbrd1.rege
   Prefetch

4) rfm2g_debug.rege
   DebugPrint messages at driver initialization time.


/* 
 * File Version 1.02 - Oct 20, 2004
 */
