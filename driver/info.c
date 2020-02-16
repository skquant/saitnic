//////////////////////////////////////////////////////////////////////////////////////////////////
// info.c
//
// NDIS OID 처리 루틴.

#include "pch.h"
#ifdef SAIT_WMI_TRACE
#include "info.tmh"
#endif

CHAR SaitVendorDescription[] = SAIT_VENDOR_DESCRIPTION;

NDIS_OID SaitSupportedOids[] =
{
    OID_GEN_SUPPORTED_LIST, 
    OID_GEN_HARDWARE_STATUS, 
    OID_GEN_MEDIA_SUPPORTED, 
    OID_GEN_MEDIA_IN_USE, 
    OID_GEN_MAXIMUM_LOOKAHEAD, 
    OID_GEN_MAXIMUM_FRAME_SIZE, 
    OID_GEN_LINK_SPEED, 
    OID_GEN_TRANSMIT_BUFFER_SPACE, 
    OID_GEN_RECEIVE_BUFFER_SPACE, 
    OID_GEN_TRANSMIT_BLOCK_SIZE, 
    OID_GEN_RECEIVE_BLOCK_SIZE, 
    OID_GEN_VENDOR_ID, 
    OID_GEN_VENDOR_DESCRIPTION, 
    OID_GEN_CURRENT_PACKET_FILTER, 
    OID_GEN_CURRENT_LOOKAHEAD, 
    OID_GEN_DRIVER_VERSION, 
    OID_GEN_MAXIMUM_TOTAL_SIZE, 
    OID_GEN_PROTOCOL_OPTIONS, 
    OID_GEN_MAC_OPTIONS, 
    OID_GEN_MEDIA_CONNECT_STATUS, 
    OID_GEN_MAXIMUM_SEND_PACKETS, 
    OID_GEN_VENDOR_DRIVER_VERSION, 
#ifdef NDIS51_MINIPORT
    OID_GEN_MACHINE_NAME,
#endif
    OID_GEN_XMIT_OK, 
    OID_GEN_RCV_OK, 
    OID_GEN_XMIT_ERROR, 
    OID_GEN_RCV_ERROR, 
    OID_GEN_RCV_NO_BUFFER,
    OID_GEN_RCV_CRC_ERROR,
    OID_GEN_TRANSMIT_QUEUE_LENGTH,
    OID_802_3_PERMANENT_ADDRESS,
    OID_802_3_CURRENT_ADDRESS,
    OID_802_3_MULTICAST_LIST,
    OID_802_3_MAXIMUM_LIST_SIZE,
    OID_802_3_RCV_ERROR_ALIGNMENT,
    OID_802_3_XMIT_ONE_COLLISION,
    OID_802_3_XMIT_MORE_COLLISIONS,
    OID_802_3_XMIT_DEFERRED,
    OID_802_3_XMIT_MAX_COLLISIONS,
    OID_802_3_RCV_OVERRUN,
    OID_802_3_XMIT_UNDERRUN,
    OID_802_3_XMIT_HEARTBEAT_FAILURE,
    OID_802_3_XMIT_TIMES_CRS_LOST,
    OID_802_3_XMIT_LATE_COLLISIONS,
    OID_PNP_CAPABILITIES,
    OID_PNP_SET_POWER,
    OID_PNP_QUERY_POWER,
    OID_PNP_ADD_WAKE_UP_PATTERN,
    OID_PNP_REMOVE_WAKE_UP_PATTERN,
    OID_PNP_ENABLE_WAKE_UP,
};

///////////////////////////////////////////////////////////////////////////////////////////////////
//  SaitQueryInformation
//      returns information about NIC
//
//  Arguments:
//      IN  MiniportAdapterContext
//              our adapter object
//
//      IN  Oid
//              OID_XXX code
//
//      IN  InformationBuffer
//              buffer to hold returned information
//
//      IN  InformationBufferLength
//              Information buffer length
//
//      IN  BytesWritten
//              number of bytes returned in InformationBuffer
//
//      IN  BytesNeeded
//              number of bytes required to return all information
//
//  Return Value:
//      Status
//
NDIS_STATUS SaitQueryInformation(
    IN  NDIS_HANDLE     MiniportAdapterContext,
    IN  NDIS_OID        Oid,
    IN  PVOID           InformationBuffer,
    IN  ULONG           InformationBufferLength,
    OUT PULONG          BytesWritten,
    OUT PULONG          BytesNeeded
    )
{
    PSAIT_ADAPTER    adapter;
    NDIS_STATUS                 status;
    PVOID                       info;
    ULONG                       infoLength;
    NDIS_HARDWARE_STATUS        hardwareStatus;
    NDIS_MEDIUM                 medium;
    ULONG                       temp;
    USHORT                      stemp;

    SaitDebugPrint(DBG_IO, DBG_TRACE, __FUNCTION__"++");

    adapter = (PSAIT_ADAPTER)MiniportAdapterContext;

    status = NDIS_STATUS_SUCCESS;

    *BytesWritten = 0;
    *BytesNeeded = 0;
    infoLength = 0;
    info = NULL;

    switch(Oid)
    {
    case OID_GEN_SUPPORTED_LIST:
        SaitDebugPrint(DBG_IO, DBG_INFO, __FUNCTION__": OID_GEN_SUPPORTED_LIST");
        info = SaitSupportedOids;
        infoLength = sizeof(SaitSupportedOids);
        break;

    case OID_GEN_HARDWARE_STATUS:
        SaitDebugPrint(DBG_IO, DBG_INFO, __FUNCTION__": OID_GEN_HARDWARE_STATUS");
        hardwareStatus = NdisHardwareStatusReady;
        info = &hardwareStatus;
        infoLength = sizeof(NDIS_HARDWARE_STATUS);
        break;

    case OID_GEN_MEDIA_SUPPORTED:
    case OID_GEN_MEDIA_IN_USE:
        SaitDebugPrint(DBG_IO, DBG_INFO, __FUNCTION__": OID_GEN_MEDIA_SUPPORTED | OID_GEN_MEDIA_IN_USE");
        medium = SAIT_MEDIUM_TYPE;
        info = &medium;
        infoLength = sizeof(NDIS_MEDIUM);
        break;

    case OID_GEN_CURRENT_LOOKAHEAD:
    case OID_GEN_MAXIMUM_LOOKAHEAD:
        SaitDebugPrint(DBG_IO, DBG_INFO, __FUNCTION__": OID_GEN_CURRENT_LOOKAHEAD | OID_GEN_MAXIMUM_LOOKAHEAD");
        temp = SAIT_MAX_LOOKAHEAD;
        info = &temp;
        infoLength = sizeof(ULONG);
        break;

    case OID_GEN_MAXIMUM_FRAME_SIZE:
        SaitDebugPrint(DBG_IO, DBG_INFO, __FUNCTION__": OID_GEN_MAXIMUM_FRAME_SIZE");
        temp = SAIT_MAX_FRAMESIZE;
        info = &temp;
        infoLength = sizeof(ULONG);
        break;

    case OID_GEN_MAXIMUM_TOTAL_SIZE:
    case OID_GEN_TRANSMIT_BLOCK_SIZE:
    case OID_GEN_RECEIVE_BLOCK_SIZE:
        SaitDebugPrint(DBG_IO, DBG_INFO, __FUNCTION__": OID_GEN_MAXIMUM_TOTAL_SIZE | OID_GEN_TRANSMIT_BLOCK_SIZE | OID_GEN_RECEIVE_BLOCK_SIZE");
        temp = SAIT_MAX_TOTAL_SIZE;
        info = &temp;
        infoLength = sizeof(ULONG);
        break;

    case OID_GEN_MAC_OPTIONS:
        SaitDebugPrint(DBG_IO, DBG_INFO, __FUNCTION__": OID_GEN_MAC_OPTIONS");
        temp = NDIS_MAC_OPTION_TRANSFERS_NOT_PEND | NDIS_MAC_OPTION_COPY_LOOKAHEAD_DATA | NDIS_MAC_OPTION_NO_LOOPBACK;
        info = &temp;
        infoLength = sizeof(ULONG);
        break;

    case OID_GEN_LINK_SPEED:
        SaitDebugPrint(DBG_IO, DBG_INFO, __FUNCTION__": OID_GEN_LINK_SPEED");
        temp = SAIT_MAX_LINKSPEED;
        info = &temp;
        infoLength = sizeof(ULONG);
        break;

    case OID_GEN_MEDIA_CONNECT_STATUS:
        SaitDebugPrint(DBG_IO, DBG_INFO, __FUNCTION__": OID_GEN_MEDIA_CONNECT_STATUS");
        info = &adapter->MediaState;
        infoLength = sizeof(adapter->MediaState);
        break;

    case OID_GEN_TRANSMIT_BUFFER_SPACE:
        SaitDebugPrint(DBG_IO, DBG_INFO, __FUNCTION__": OID_GEN_TRANSMIT_BUFFER_SPACE");
        temp = SAIT_MAX_TOTAL_SIZE * SAIT_MAX_SEND_PACKETS;
        info = &temp;
        infoLength = sizeof(ULONG);
        break;

    case OID_GEN_RECEIVE_BUFFER_SPACE:
        SaitDebugPrint(DBG_IO, DBG_INFO, __FUNCTION__": OID_GEN_RECEIVE_BUFFER_SPACE");
        temp = SAIT_MAX_TOTAL_SIZE * SAIT_MAX_RECV_PACKETS;
        info = &temp;
        infoLength = sizeof(ULONG);
        break;

    case OID_GEN_VENDOR_ID:
        SaitDebugPrint(DBG_IO, DBG_INFO, __FUNCTION__": OID_GEN_VENDOR_ID");
        info = adapter->PermanentAddress;
        infoLength = 3;
        break;

    case OID_GEN_VENDOR_DESCRIPTION:
        SaitDebugPrint(DBG_IO, DBG_INFO, __FUNCTION__": OID_GEN_VENDOR_DESCRIPTION");
        info = SaitVendorDescription;
        infoLength = sizeof(SAIT_VENDOR_DESCRIPTION);
        break;

    case OID_GEN_VENDOR_DRIVER_VERSION:
        SaitDebugPrint(DBG_IO, DBG_INFO, __FUNCTION__": OID_GEN_VENDOR_DRIVER_VERSION");
        temp = 0x00010000;
        info = &temp;
        infoLength = sizeof(ULONG);
        break;

    case OID_GEN_DRIVER_VERSION:
        SaitDebugPrint(DBG_IO, DBG_INFO, __FUNCTION__": OID_GEN_DRIVER_VERSION");

#ifdef NDIS51_MINIPORT
        stemp = 0x0501;
#else
        stemp = 0x0500;
#endif
        info = &stemp;
        infoLength = sizeof(USHORT);
        break;

    case OID_GEN_CURRENT_PACKET_FILTER:
        SaitDebugPrint(DBG_IO, DBG_INFO, __FUNCTION__": OID_GEN_CURRENT_PACKET_FILTER");
        info = &adapter->CurrentPacketFilter;
        infoLength = sizeof(adapter->CurrentPacketFilter);
        break;

    case OID_GEN_MAXIMUM_SEND_PACKETS:
        SaitDebugPrint(DBG_IO, DBG_INFO, __FUNCTION__": OID_GEN_MAXIMUM_SEND_PACKETS");
        temp = SAIT_MAX_SEND_PACKETS;
        info = &temp;
        infoLength = sizeof(ULONG);
        break;

    case OID_802_3_PERMANENT_ADDRESS:
        SaitDebugPrint(DBG_IO, DBG_INFO, __FUNCTION__": OID_802_3_PERMANENT_ADDRESS");
        info = adapter->PermanentAddress;
        infoLength = SAIT_LENGTH_OF_ADDRESS;
        break;

    case OID_802_3_CURRENT_ADDRESS:
        SaitDebugPrint(DBG_IO, DBG_INFO, __FUNCTION__": OID_802_3_CURRENT_ADDRESS");
        info = adapter->CurrentAddress;
        infoLength = SAIT_LENGTH_OF_ADDRESS;
        break;

    case OID_802_3_MULTICAST_LIST:
        SaitDebugPrint(DBG_IO, DBG_INFO, __FUNCTION__": OID_802_3_MULTICAST_LIST");
        status = NDIS_STATUS_NOT_SUPPORTED;
        break;

    case OID_802_3_MAXIMUM_LIST_SIZE:
        SaitDebugPrint(DBG_IO, DBG_INFO, __FUNCTION__": OID_802_3_MAXIMUM_LIST_SIZE");
        temp = 1;
        info = &temp;
        infoLength = sizeof(ULONG);
        break;

    case OID_802_3_RCV_ERROR_ALIGNMENT:
        SaitDebugPrint(DBG_IO, DBG_INFO, __FUNCTION__": OID_802_3_RCV_ERROR_ALIGNMENT");
        info = &adapter->RcvErrAlign;
        infoLength = sizeof(adapter->RcvErrAlign);
        break;

    case OID_802_3_XMIT_ONE_COLLISION:
        SaitDebugPrint(DBG_IO, DBG_INFO, __FUNCTION__": OID_802_3_XMIT_ONE_COLLISION");
        info = &adapter->XmitOneCollision;
        infoLength = sizeof(adapter->XmitOneCollision);
        break;

    case OID_802_3_XMIT_MORE_COLLISIONS:
        SaitDebugPrint(DBG_IO, DBG_INFO, __FUNCTION__": OID_802_3_XMIT_MORE_COLLISIONS");
        info = &adapter->XmitMoreCollisions;
        infoLength = sizeof(adapter->XmitMoreCollisions);
        break;

    case OID_802_3_XMIT_DEFERRED:
        SaitDebugPrint(DBG_IO, DBG_INFO, __FUNCTION__": OID_802_3_XMIT_DEFERRED");
        info = &adapter->XmitDeferred;
        infoLength = sizeof(adapter->XmitDeferred);
        break;

    case OID_802_3_XMIT_MAX_COLLISIONS:
        SaitDebugPrint(DBG_IO, DBG_INFO, __FUNCTION__": OID_802_3_XMIT_MAX_COLLISIONS");
        info = &adapter->XmitMaxCollisions;
        infoLength = sizeof(adapter->XmitMaxCollisions);
        break;

    case OID_802_3_RCV_OVERRUN:
        SaitDebugPrint(DBG_IO, DBG_INFO, __FUNCTION__": OID_802_3_RCV_OVERRUN");
        info = &adapter->RcvOverrun;
        infoLength = sizeof(adapter->RcvOverrun);
        break;

    case OID_802_3_XMIT_UNDERRUN:
        SaitDebugPrint(DBG_IO, DBG_INFO, __FUNCTION__": OID_802_3_XMIT_UNDERRUN");
        info = &adapter->XmitUnderrun;
        infoLength = sizeof(adapter->XmitUnderrun);
        break;

    case OID_802_3_XMIT_HEARTBEAT_FAILURE:
        SaitDebugPrint(DBG_IO, DBG_INFO, __FUNCTION__": OID_802_3_XMIT_HEARTBEAT_FAILURE");
        info = &adapter->XmitHearbeatFailure;
        infoLength = sizeof(adapter->XmitHearbeatFailure);
        break;

    case OID_802_3_XMIT_TIMES_CRS_LOST:
        SaitDebugPrint(DBG_IO, DBG_INFO, __FUNCTION__": OID_802_3_XMIT_TIMES_CRS_LOST");
        info = &adapter->XmitTimesCrsLost;
        infoLength = sizeof(adapter->XmitTimesCrsLost);
        break;

    case OID_802_3_XMIT_LATE_COLLISIONS:
        SaitDebugPrint(DBG_IO, DBG_INFO, __FUNCTION__": OID_802_3_XMIT_LATE_COLLISIONS");
        info = &adapter->XmitLateCollisions;
        infoLength = sizeof(adapter->XmitLateCollisions);
        break;

    case OID_GEN_XMIT_OK:
        SaitDebugPrint(DBG_IO, DBG_INFO, __FUNCTION__": OID_GEN_XMIT_OK");
        info = &adapter->XmitOk;
        infoLength = sizeof(adapter->XmitOk);
        break;

    case OID_GEN_RCV_OK:
        SaitDebugPrint(DBG_IO, DBG_INFO, __FUNCTION__": OID_GEN_RCV_OK");
        info = &adapter->RcvOk;
        infoLength = sizeof(adapter->RcvOk);
        break;

    case OID_GEN_XMIT_ERROR:
        SaitDebugPrint(DBG_IO, DBG_INFO, __FUNCTION__": OID_GEN_XMIT_ERROR");
        info = &adapter->XmitError;
        infoLength = sizeof(adapter->XmitError);
        break;

    case OID_GEN_RCV_ERROR:
        SaitDebugPrint(DBG_IO, DBG_INFO, __FUNCTION__": OID_GEN_RCV_ERROR");
        info = &adapter->RcvError;
        infoLength = sizeof(adapter->RcvError);
        break;

    case OID_GEN_RCV_NO_BUFFER:
        SaitDebugPrint(DBG_IO, DBG_INFO, __FUNCTION__": OID_GEN_RCV_NO_BUFFER");
        info = &adapter->RcvNoBuffer;
        infoLength = sizeof(adapter->RcvNoBuffer);
        break;

    case OID_PNP_CAPABILITIES:
        SaitDebugPrint(DBG_IO, DBG_INFO, __FUNCTION__": OID_PNP_CAPABILITIES");
        status = NDIS_STATUS_NOT_SUPPORTED;
        break;

    case OID_PNP_QUERY_POWER:
        SaitDebugPrint(DBG_IO, DBG_INFO, __FUNCTION__": OID_PNP_QUERY_POWER");
        break;

    case OID_PNP_ENABLE_WAKE_UP:
        SaitDebugPrint(DBG_IO, DBG_INFO, __FUNCTION__": OID_PNP_ENABLE_WAKE_UP");
        break;

    default:
        status = NDIS_STATUS_NOT_SUPPORTED;
        SaitDebugPrint(DBG_IO, DBG_WARN, __FUNCTION__": Unsupported OID %x", Oid);
        break;
    }

    if (status == NDIS_STATUS_SUCCESS)
    {
        *BytesNeeded = infoLength;

        if (infoLength <= InformationBufferLength)
        {
            *BytesWritten = infoLength;

            if (infoLength != 0)
            {
                NdisMoveMemory(InformationBuffer, info, infoLength);
            }
        }
        else
        {
            status = NDIS_STATUS_BUFFER_TOO_SHORT;
        }
    }

    SaitDebugPrint(DBG_IO, DBG_TRACE, __FUNCTION__"--. STATUS %x", status);

    return status;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
//  SaitSetInformation
//      sets NIC information
//
//  Arguments:
//      IN  MiniportAdapterContext
//              our adapter object
//
//      IN  Oid
//              OID_XXX code
//
//      IN  InformationBuffer
//              buffer with information
//
//      IN  InformationBufferLength
//              Information buffer length
//
//      IN  BytesRead
//              number of bytes read from InformationBuffer
//
//      IN  BytesNeeded
//              number of bytes required to process OID request
//
//  Return Value:
//      Status
//
NDIS_STATUS SaitSetInformation(
    IN  NDIS_HANDLE     MiniportAdapterContext,
    IN  NDIS_OID        Oid,
    IN  PVOID           InformationBuffer,
    IN  ULONG           InformationBufferLength,
    OUT PULONG          BytesRead,
    OUT PULONG          BytesNeeded
    )
{
    PSAIT_ADAPTER    adapter;
    NDIS_STATUS         status;

    SaitDebugPrint(DBG_IO, DBG_TRACE, __FUNCTION__"++");

    adapter = (PSAIT_ADAPTER)MiniportAdapterContext;
    status = NDIS_STATUS_SUCCESS;

    *BytesRead = 0;
    *BytesNeeded = 0;

    switch(Oid)
    {
    case OID_GEN_CURRENT_LOOKAHEAD:
        SaitDebugPrint(DBG_IO, DBG_INFO, __FUNCTION__": OID_GEN_CURRENT_LOOKAHEAD");
        if (InformationBufferLength >= sizeof(ULONG))
        {
            // TODO: If the driver uses partial rcv indications save the value and
            //       honor it when indicating. Drivers indicating full packets shouldn't care.

            *BytesRead = sizeof(ULONG);
        }
        else
        {
            *BytesNeeded = sizeof(ULONG);
            status = NDIS_STATUS_INVALID_LENGTH;
        }
        break;

    case OID_GEN_CURRENT_PACKET_FILTER:
        SaitDebugPrint(DBG_IO, DBG_INFO, __FUNCTION__": OID_GEN_CURRENT_PACKET_FILTER");
        if (InformationBufferLength == sizeof(ULONG))
        {
            *BytesRead = sizeof(ULONG);

            NdisMoveMemory(&adapter->CurrentPacketFilter, InformationBuffer, sizeof(ULONG));
        }
        else
        {
            status = NDIS_STATUS_INVALID_LENGTH;
        }
        break;

    case OID_GEN_PROTOCOL_OPTIONS:
        SaitDebugPrint(DBG_IO, DBG_INFO, __FUNCTION__": OID_GEN_PROTOCOL_OPTIONS");
        status = NDIS_STATUS_NOT_SUPPORTED;
        break;

    case OID_802_3_MULTICAST_LIST:
        SaitDebugPrint(DBG_IO, DBG_INFO, __FUNCTION__": OID_802_3_MULTICAST_LIST");
        status = NDIS_STATUS_NOT_SUPPORTED;
        break;

    case OID_PNP_SET_POWER:
        SaitDebugPrint(DBG_IO, DBG_INFO, __FUNCTION__": OID_PNP_SET_POWER");
        if (InformationBufferLength == sizeof(NDIS_DEVICE_POWER_STATE))
        {
            //*****************************************************************
            //*****************************************************************
            // TODO: set device to new power state
            //*****************************************************************
            //*****************************************************************

            adapter->CurrentPowerState = *(PNDIS_DEVICE_POWER_STATE)InformationBuffer;

            *BytesRead = sizeof(NDIS_DEVICE_POWER_STATE);
        }
        else
        {
            status = NDIS_STATUS_INVALID_LENGTH;
        }
        break;

    case OID_PNP_ADD_WAKE_UP_PATTERN:
        SaitDebugPrint(DBG_IO, DBG_INFO, __FUNCTION__": OID_PNP_ADD_WAKE_UP_PATTERN");
        status = NDIS_STATUS_NOT_SUPPORTED;
        break;

    case OID_PNP_REMOVE_WAKE_UP_PATTERN:
        SaitDebugPrint(DBG_IO, DBG_INFO, __FUNCTION__": OID_PNP_REMOVE_WAKE_UP_PATTERN");
        status = NDIS_STATUS_NOT_SUPPORTED;
        break;

    case OID_PNP_ENABLE_WAKE_UP:
        SaitDebugPrint(DBG_IO, DBG_INFO, __FUNCTION__": OID_PNP_ENABLE_WAKE_UP");
        status = NDIS_STATUS_NOT_SUPPORTED;
        break;

    default:
        SaitDebugPrint(DBG_IO, DBG_WARN, __FUNCTION__": Unsupported OID %x", Oid);
        status = NDIS_STATUS_NOT_SUPPORTED;
        break;
    }

    SaitDebugPrint(DBG_IO, DBG_TRACE, __FUNCTION__"--. STATUS %x", status);

    return status;
}

