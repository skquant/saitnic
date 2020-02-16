//////////////////////////////////////////////////////////////////////////////////////////////////
// debug.c
//
// Debug Print 처리를 위한 루틴.

#include "pch.h"
#ifdef SAIT_WMI_TRACE
#include "debug.tmh"
#else

// debug level and debug area mask
ULONG   g_DebugLevel =DBG_MYTRACE;// DBG_NONE;
ULONG   g_DebugArea = DBG_ALL;

///////////////////////////////////////////////////////////////////////////////////////////////////
//  SaitDebugPrint
//      디버그 메세지 출력 루틴.
//
//  Arguments:
//      IN  Area
//              Debug area (DBG_PNP, DBG_POWER, etc..)
//
//      IN  Level
//              Debug Level (DBG_ERR, DBG_INFO, etc..)
//
//      IN  Format
//              Debug Message Format
//
//  Return Value:
//      None.
//
VOID SaitDebugPrint(
    IN ULONG    Area,
    IN ULONG    Level,
    IN PCCHAR   Format,
    IN          ...
    )
{
    va_list     vaList;
    CHAR        buffer[1024];
    NTSTATUS    status;

    va_start(vaList, Format);

    // check mask for debug area and debug level
    if ((g_DebugArea & Area) && (Level <= g_DebugLevel))
    {
        status = RtlStringCbVPrintfA(
                    buffer, 
                    sizeof(buffer),
                    Format,
                    vaList
                    );

        if (Level == DBG_ERR)
        {
            DbgPrint("SAIT: ERROR %s !!!!!\n", buffer); 
        }
        else if (Level == DBG_WARN)
        {
            DbgPrint("SAIT: WARNING %s\n", buffer); 
        }
        else if (Level == DBG_INFO)
        {
            DbgPrint("SAIT:     %s\n", buffer); 
        }
        else
        {
            DbgPrint("SAIT: %s\n", buffer); 
        }
    }

    va_end(vaList);

    return;
}

#endif

