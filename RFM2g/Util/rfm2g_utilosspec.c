/*
===============================================================================
                            COPYRIGHT NOTICE

    Copyright (C) 2004, 2006-10 GE Intelligent Platforms Embedded Systems, Inc.
    International Copyright Secured.  All Rights Reserved.

-------------------------------------------------------------------------------

    $Workfile: rfm2g_utilosspec.c $
    $Revision: 9 $
    $Modtime: 3/19/10 11:06a $

-------------------------------------------------------------------------------
    Description: Diagnostic Utility for the PCI RFM2g Device Driver
-------------------------------------------------------------------------------

===============================================================================
*/
#include "rfm2g_api.h"
#include "rfm2gdll_stdc.h"

static RFM2G_INT32  doReturn( DIAGCMD *cmd, RFM2G_UINT8 usage );
static RFM2G_INT32  doRegisters( DIAGCMD *cmd, RFM2G_UINT8 usage);
static RFM2G_INT32  doStatistics( DIAGCMD *cmd, RFM2G_UINT8 usage );
static int Rfm2gConsoleHandler( DWORD CEvent);


/* A list of driver specific commands. */
static CMDINFO cmdlistdrvspec[] = 
{	
	/* name               description                 function    parcount repeatable display */
    { "help            ", "[command]",                doHelp,           1, 0, 1 },
	{ "registers       ", "show most device regs",	  doRegisters,		0, 0, 1 },
    { "repeat          ", "[-p] count cmd [arg...]",  doRepeat,         4, 0, 1 },
    { "return          ", " ",                        doReturn,         0, 0, 1 },
	{ "statistics      ", "[clear|reset|zero]",		  doStatistics,     1, 1, 1 }
};
   
static DIAGCMD *loccmd;


/* These values are defined in wincon.h and are for reference purposes only
CTRL_C_EVENT        0
CTRL_BREAK_EVENT    1
CTRL_CLOSE_EVENT    2
// 3 is reserved!
// 4 is reserved!
CTRL_LOGOFF_EVENT   5
CTRL_SHUTDOWN_EVENT 6
*/


/**************************************************************************
*
* doDrvSpecific - Change to the driver specific menu
* 
* Change to the driver specific menu
* cmd: (I) Command code and command parameters
* usage: (I) If TRUE, usage message is displayed
*
* RETURNS: 0 on Success, -1 on Error
*/

static RFM2G_INT32 doDrvSpecific
    ( 
    DIAGCMD *cmd, 
    RFM2G_UINT8 usage 
    )
    {
    if( usage || cmd->parmcount != cmd->pCmdInfoList[cmd->command].parmcount )
        {
        printf( "\n%s:  Go to driver specific menu.\n\n", 
                cmd->pCmdInfoList[cmd->command].name );
        printf( "Usage:  %s\n\n", cmd->pCmdInfoList[cmd->command].name );

        return(0);
        }

    printf("Welcome to the driver specific menu\n");

    /* Place driver specific initialization here */

    /* Change menu to driver specific menu */
    cmd->pCmdInfoList = cmdlistdrvspec;
    cmd->NumEntriesCmdInfoList = (sizeof(cmdlistdrvspec) / sizeof(CMDINFO));
    sprintf(cmd->pUtilPrompt,"UTILDRVSPEC%d > ", cmd->deviceNum);

    /* return to menu control */

    return(0);
    }

/**************************************************************************
*
* doReturnDrvSpecific - Driver Specific cleanup on return to main menu
* 
* Driver Specific cleanup on return to main menu
* cmd: (I) Command code and command parameters, unused
*
* RETURNS: 0 on Success, -1 on Error
*/

static RFM2G_INT32 doReturnDrvSpecific
    ( 
    DIAGCMD *cmd, 
    RFM2G_UINT8 usage 
    )
    {

    /* 
     * Driver specific cleanup here, if any.  This function is called by 
     * doReturn.
     */

    return(0);
    }   

/**************************************************************************
*
* doExitDrvSpecific - Driver Specific cleanup on exit of utility
* 
* Driver Specific cleanup on exit of utility
* cmd: (I) Command code and command parameters, unused
*
* RETURNS: 0 on Success, -1 on Error
*/

static RFM2G_INT32 doExitDrvSpecific
    ( 
    DIAGCMD *cmd, 
    RFM2G_UINT8 usage 
    )
    {

    /* 
     * Driver specific cleanup here, if any.  This function is called by 
     * doExit.
     */

    return(0);
    }   

/**************************************************************************
*
* doInitDrvSpecific - Driver specific initialization on start of utility.
* 
* Driver specific initialization on start of utility.
* cmd: (I) Command code and command parameters, unused
*
* RETURNS: 0 on Success, -1 on Error
*/

static RFM2G_INT32 doInitDrvSpecific
    ( 
    DIAGCMD *cmd, 
    RFM2G_UINT8 usage 
    )
    {


	loccmd = cmd; // stash away the cmd pointer for local use

    /* 
     * Driver specific initialization here, if any.  This function is called by 
     * rfm2gUtil.
     */

	// Register the console handler


	if (SetConsoleCtrlHandler(
		(PHANDLER_ROUTINE)Rfm2gConsoleHandler,TRUE) == FALSE)
		{
			printf("\nUnable to install handler!\n");
			// status = RFM2G_DRIVER_ERROR;
		}

    return(0);
    }   


static int Rfm2gConsoleHandler( DWORD CEvent)
{
	int				RetVal = TRUE;
	RFM2G_STATUS	retstatus = -1;

#ifdef RFM2G_GET_PID
	DWORD	currPID = 0;
	HANDLE	hProcess;

	currPID = GetCurrentProcessId();
	hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE,currPID); 
#endif

	switch(CEvent)
	{
		case CTRL_C_EVENT:
		{
			printf("Got CTRL+C\n");
			loccmd->running = 0;
			break;
		}
		case CTRL_BREAK_EVENT:
		{
			printf("Got CTRL+BREAK\n");
			loccmd->running = 0;
			break;
		}
		case CTRL_CLOSE_EVENT:
		{
			printf("Got Program closing..\n");
			loccmd->running = 0;
			RFM2gClose(&(loccmd->Handle));
			RetVal = FALSE;
			break;
		}
		case CTRL_LOGOFF_EVENT:
		{
			loccmd->running = 0;
			break;
		}
		case CTRL_SHUTDOWN_EVENT:  
		{
			loccmd->running = 0;
			break;
		}
	}

#ifdef RFM2G_GET_PID
	// We are done with it
	CloseHandle(hProcess);
#endif

	fflush(stdout);

	return(RetVal);
}

/*
 *------------------------------------------------------------------------
 * doRegisters: show RFM2g registers
 *------------------------------------------------------------------------
 */

typedef struct registerset_s	{
	char		*rs_name;	/* Name of the register		 */
	char		*rs_desc;	/* Description			 */
	unsigned	rs_offset;	/* Offset of register		 */
	unsigned	rs_size;	/* Size of register (in bytes)	 */
} registerset_t, *REGISTERSET;

static registerset_t	pmc5565[] =	{
	{ "BID",  "Board ID" },
	{ "NID",  "Node ID" },
	{ "LCSR1","Local Control and Status 1"},
	{ "LISR", "Local Interrupt Status " },
	{ "LIER", "Local Interrupt Enable" },
	{ "NTARGD","Network Target Data" },
	{ "NTNODE", "Network Target Node" },
	{ "NINTCMD","Network Interrupt Command"	},
	{ "ISD1", "Interrupt 1 Sender Data" },
	{ "SID1", "Interrupt 1 Sender ID" },
	{ "ISD2", "Interrupt 2 Sender Data" },
	{ "SID2", "Interrupt 2 Sender ID" },
	{ "ISD3", "Interrupt 3 Sender Data" },
	{ "SID3", "Interrupt 3 Sender ID" },
	{ "INITNODEDATA","Initialized Node Data" },
	{ "INITNODEID","Initialized Node ID" },
	{ 0 }				/* End of table marker		 */
};

typedef struct inventory_s	{
	unsigned char	iv_bid;		/* Board ID			 */
	REGISTERSET	iv_rset;	/* Register set address		 */
} inventory_t, *INVENTORY;

static inventory_t	inventories[] =	{
	{ RFM_BID_PMC5565,		pmc5565	}
};
static int		Ninventories =
	sizeof(inventories)/sizeof(inventories[0]);



//	int		argc,		/* Number of command line tokens */
//	char		*argv[]		/* Table of command line tokens	 */

//static int
static RFM2G_INT32
doRegisters(DIAGCMD *cmd, RFM2G_UINT8 usage)
{
	unsigned char	bid;
	RFM2GHANDLE rh;
	INVENTORY	iv;
	INVENTORY	liv;

	rh = loccmd->Handle;
	// get the board ID
    RFM2gBoardID(rh, &bid);

	for( iv = inventories, liv = iv + Ninventories;
	iv < liv; ++iv )	{
		if( bid == iv->iv_bid )	{
			REGISTERSET	rset;
			char		v[30];
			for( rset = iv->iv_rset; rset->rs_name; ++rset ) {
				/* Must know register offset and size	 */
				if( rset->rs_offset == 0 )	{
					char	fullName[32];
					strcpy( fullName, "rfm3_" ); // yes keep it with the rfm3 prefix
					strcat( fullName, rset->rs_name );
					rset->rs_offset = RFM2gRegOffset(
						rh, fullName
					);
					if( rset->rs_offset == 0 )	{
						continue;
					}
					rset->rs_size = RFM2gRegSize(
						rh, fullName
					);
					if( rset->rs_size == 0 )	{
						continue;
					}
				}
				switch( rset->rs_size )	{
				default: continue;
				case 1:
					sprintf( v, "0x%02X",
						RFM2gPeekReg8( rh, rset->rs_offset ) );
					break;
				case 2:
					sprintf( v, "0x%04X",
						RFM2gPeekReg16( rh, rset->rs_offset ) );
					break;
				case 4:
					sprintf( v, "0x%08X",
						RFM2gPeekReg32( rh, rset->rs_offset ) );
					break;
				}
				printf(
					"%10s <-- %-4s (0x%02X) %s\n",
					v,
					rset->rs_name,
					rset->rs_offset,
					rset->rs_desc
				);
			}
			return( 0 );
		}
	}
	printf( "Board ID 0x%02X not supported.\n", bid );
	return( -1 );
}


/*
 *------------------------------------------------------------------------
 * doStatistics: display and maybe reset interrupt event counts
 *------------------------------------------------------------------------
 */

static RFM2G_INT32 
doStatistics( DIAGCMD *cmd, RFM2G_UINT8 usage )
{
	static char	fmt[] = "%-8lu\tEVENTID_%s\n";
	rfmGetNevents_t	rge;
	ULONG32		    clearThem = 0; /* default is to not clear the stats */
	RFM2GHANDLE     rh;
    RFM2G_INT32     index;  /* Selects appropriate nomenclature      */
    char *          name;   /* Local pointer to command name         */
    char *          desc;   /* Local pointer to command description  */
    int             operation = 0;

    index = cmd->command;
	name  = cmd->pCmdInfoList[index].name;
	desc  = cmd->pCmdInfoList[index].desc;
	
	rh = loccmd->Handle;
	
	/* Determine if we should clear the counts or not	 */
	if(cmd->parmcount > 0) {
		char	*arg = cmd->parm[0];
		int	Larg = (int)strlen( arg );
		
		if( !strncmp( arg, "clear", Larg )	||
			!strncmp( arg, "reset", Larg ) 	||
			!strncmp( arg, "zero", Larg ) )	{
			clearThem = 1;
		}
		else{
			printf( "usage: %s  %s \n", name, desc);
			return( -1 );
		}
		
	} /*  end of  if(cmd->parmcount > 0) */

	/* Try to update the event counts				 */
	if( RFM2gNevents( rh, &rge, clearThem ) != RFM2G_SUCCESS )	{
		/* We failed to get them				 */
		printf( "COULD NOT UPDATE EVENT COUNTS.\n" );
		return( -1 );
	}

	/* Display the values we get					 */
    printf( "\nCount\t\tEvent\n" );
    printf( "------------------------------------------------------------------\n");
	printf( fmt, rge.rge_nEvents[RFM2GEVENT_RESET], "RESET");
	printf( fmt, rge.rge_nEvents[RFM2GEVENT_INTR1], "1");
	printf( fmt, rge.rge_nEvents[RFM2GEVENT_INTR2], "2");
	printf( fmt, rge.rge_nEvents[RFM2GEVENT_INTR3], "3");
	printf( fmt, rge.rge_nEvents[RFM2GEVENT_INTR4], "4");
	printf( fmt, rge.rge_nEvents[RFM2GEVENT_BAD_DATA], "BADDATA");
	printf( fmt, rge.rge_nEvents[RFM2GEVENT_RXFIFO_FULL], "RXFIFOFULL");
	printf( fmt, rge.rge_nEvents[RFM2GEVENT_ROGUE_PKT], "ROGUE_PACKET");
	printf( fmt, rge.rge_nEvents[RFM2GEVENT_RXFIFO_AFULL], "RX_FIFO_ALMOST_FULL");
	printf( fmt, rge.rge_nEvents[RFM2GEVENT_SYNC_LOSS], "SYNC_LOSS");
	printf( fmt, rge.rge_nEvents[RFM2GEVENT_MEM_WRITE_INHIBITED], "MEMWRTINHIBITED");
	printf( fmt, rge.rge_nEvents[RFM2GEVENT_LOCAL_MEM_PARITY_ERR], "PARITY_ERROR");
	printf( fmt, rge.rge_nDma,"DMA Done");
	printf( fmt, rge.rge_nDmaErr,"DMA Errors");
	
    printf("\n\n"); 
	
	/* Fini								 */
	return( 0 );
}
