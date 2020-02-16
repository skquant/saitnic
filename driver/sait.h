#ifndef __SAIT_H__
#define __SAIT_H__

// Memory allocation pool tag
#define SAIT_POOL_TAG 'tiaS'

#define SAIT_MEDIUM_TYPE             NdisMedium802_3 
#define SAIT_802_3_MAX_LIST_SIZE     32

// OID Constants
#define SAIT_MAX_LOOKAHEAD           256
#define SAIT_MAX_FRAMESIZE           1500
#define SAIT_MAX_TOTAL_SIZE          1514//1518
#define SAIT_HEADER_SIZE             14

#define SAIT_MAX_SEND_PACKETS        4
#define SAIT_MAX_RECV_PACKETS        4

#define SAIT_LENGTH_OF_ADDRESS       6

#define SAIT_MAX_LINKSPEED           1000000L //100Mbps 80000L //8M BPS
#define SAIT_VENDOR_DESCRIPTION      "SAIT NDIS Miniport Driver"

// bulk transfer context type
typedef struct _SAIT_IO_CONTEXT
{
    struct _SAIT_ADAPTER*            Adapter;

    PUCHAR              NextVirtualAddress;
    ULONG               RemainingLength;
    ULONG               TotalTransferCount;
    PMDL                Mdl;
    PURB                Urb;
    PNDIS_PACKET        Packet;
} SAIT_IO_CONTEXT, *PSAIT_IO_CONTEXT;

typedef struct _SAIT_WRITE_CONTEXT
{
	LIST_ENTRY                   ListEntry;
	struct _SAIT_ADAPTER*      Adapter;
	PURB                         pUrb;
	PVOID                        VirtualAddress; 
	UINT                         BufferLen;
	BOOLEAN                      NullPacket;
} SAIT_WRITE_CONTEXT, *PSAIT_WRITE_CONTEXT;

typedef struct _SAIT_READ_CONTEXT
{
	struct _SAIT_ADAPTER*      Adapter;
	PVOID                        VirtualAddress;
	PURB                         Urb;
	UINT                         BufferLen;
} SAIT_READ_CONTEXT, *PSAIT_READ_CONTEXT;

// used as receive context to handle
// hardware and software contexts
typedef struct _SAIT_RECEIVE_DESCRIPTOR
{
    LIST_ENTRY                  ListEntry;
    PNDIS_PACKET                Packet;
    PNDIS_BUFFER                Buffer;
    PVOID                       VirtualAddress;
} SAIT_RECEIVE_DESCRIPTOR, *PSAIT_RECEIVE_DESCRIPTOR;

// miniport adapter structure
typedef struct _SAIT_ADAPTER
{
    NDIS_HANDLE                 AdapterHandle;          // miniport adapter handle, saved in InitializeHandler

    PDEVICE_OBJECT              DeviceObject;           // pointer to the DeviceObject
    PDEVICE_OBJECT              PhysicalDeviceObject;   // underlying PDO
    PDEVICE_OBJECT              LowerDeviceObject;      // top of the device stack

    LONG                        RemoveCount;            // 1-based reference count
    NDIS_EVENT                  RemoveEvent;            // event to sync device removal

    BOOLEAN                     HaltPending;            // true, if SaitHalt was called

    UCHAR                       CurrentAddress[SAIT_LENGTH_OF_ADDRESS];      // currently used hardware address
    UCHAR                       PermanentAddress[SAIT_LENGTH_OF_ADDRESS];    // hardware address from permanent storage

    NDIS_DEVICE_POWER_STATE     CurrentPowerState;
    NDIS_MEDIA_STATE            MediaState;

    ULONG                       CurrentPacketFilter;

    // Send resources
    BOOLEAN                     bSendAllocated;
    NDIS_SPIN_LOCK              SendLock;
    LIST_ENTRY                  SendQueue;                  // queue of in progress send packets 
    ULONG                       SendCount;                  // count of in progress send packets
    LIST_ENTRY                  SendPendingQueue;           // queue of waiting for resources send packets
    ULONG                       SendPendingCount;           // count of waiting for resources send packets
    NDIS_EVENT                  SendIdleEvent;              // send is stopped
	LIST_ENTRY                  SendDescriptorListHead;

    // Receive resources
    BOOLEAN                     bReceiveAllocated;
    NPAGED_LOOKASIDE_LIST       ReceiveDescriptorLookaside;
    LIST_ENTRY                  ReceiveDescriptorListHead;
    NDIS_HANDLE                 ReceivePacketPool;
    NDIS_HANDLE                 ReceiveBufferPool;
    NDIS_SPIN_LOCK              ReceiveLock;
    ULONG                       ReceiveCount;
	ULONG                       ReceiveUsbPacket;
    NDIS_EVENT                  ReceiveIdleEvent;
	NDIS_EVENT                  ReceiveCloseEvent;

	HANDLE                      hReceiveThread;
	BOOLEAN                     bRunningPollingThread;

    PUSB_CONFIGURATION_DESCRIPTOR   ConfigDescriptor;   // configuration descriptor
    USBD_CONFIGURATION_HANDLE       ConfigHandle;       // configuration handle
    PUSBD_INTERFACE_INFORMATION     InterfaceInformation;

	HANDLE                      hUsbInPipe;
	HANDLE                      hUsbOutPipe;

	KEVENT                       ReceiveEvent;

// Statistics 
    ULONG                       XmitOk;                 // OID_GEN_XMIT_OK
    ULONG                       RcvOk;                  // OID_GEN_RCV_OK
    ULONG                       XmitError;              // OID_GEN_XMIT_ERROR
    ULONG                       RcvError;               // OID_GEN_RCV_ERROR
    ULONG                       RcvNoBuffer;            // OID_GEN_RCV_NO_BUFFER

    ULONG64                     DirBytesXmit;           // OID_GEN_DIRECTED_BYTES_XMIT
    ULONG                       DirFramesXmit;          // OID_GEN_DIRECTED_FRAMES_XMIT
    ULONG64                     McastBytesXmit;         // OID_GEN_MULTICAST_BYTES_XMIT
    ULONG                       McastFramesXmit;        // OID_GEN_MULTICAST_FRAMES_XMIT
    ULONG64                     BcastBytesXmit;         // OID_GEN_BROADCAST_BYTES_XMIT
    ULONG                       BcastFramesXmit;        // OID_GEN_BROADCAST_FRAMES_XMIT
    ULONG64                     DirBytesRcv;            // OID_GEN_DIRECTED_BYTES_RCV
    ULONG                       DirFramesRcv;           // OID_GEN_DIRECTED_FRAMES_RCV
    ULONG64                     McastBytesRcv;          // OID_GEN_MULTICAST_BYTES_RCV
    ULONG                       McastFramesRcv;         // OID_GEN_MULTICAST_FRAMES_RCV
    ULONG64                     BcastBytesRcv;          // OID_GEN_BROADCAST_BYTES_RCV
    ULONG                       BcastFramesRcv;         // OID_GEN_BROADCAST_FRAMES_RCV
    ULONG                       RcvCrcError;            // OID_GEN_RCV_CRC_ERROR
    ULONG                       XmitQueueLength;        // OID_GEN_TRANSMIT_QUEUE_LENGTH

    ULONG                       RcvErrAlign;            // OID_802_3_RCV_ERROR_ALIGNMENT
    ULONG                       XmitOneCollision;       // OID_802_3_XMIT_ONE_COLLISION
    ULONG                       XmitMoreCollisions;     // OID_802_3_XMIT_MORE_COLLISIONS
    ULONG                       XmitDeferred;           // OID_802_3_XMIT_DEFERRED
    ULONG                       XmitMaxCollisions;      // OID_802_3_XMIT_MAX_COLLISIONS
    ULONG                       RcvOverrun;             // OID_802_3_RCV_OVERRUN
    ULONG                       XmitUnderrun;           // OID_802_3_XMIT_UNDERRUN
    ULONG                       XmitHearbeatFailure;    // OID_802_3_XMIT_HEARTBEAT_FAILURE
    ULONG                       XmitTimesCrsLost;       // OID_802_3_XMIT_TIMES_CRS_LOST
    ULONG                       XmitLateCollisions;     // OID_802_3_XMIT_LATE_COLLISIONS
} SAIT_ADAPTER, *PSAIT_ADAPTER;

NDIS_STATUS DriverEntry(
    IN  PDRIVER_OBJECT  DriverObject,
    IN  PUNICODE_STRING RegistryPath
    );

BOOLEAN SaitCheckForHang(
    IN  NDIS_HANDLE MiniportAdapterContext
    );

VOID SaitHalt(
    IN  NDIS_HANDLE MiniportAdapterContext
    );

NDIS_STATUS SaitInitialize(
    OUT PNDIS_STATUS    OpenErrorStatus,
    OUT PUINT           SelectedMediumIndex,
    IN  PNDIS_MEDIUM    MediumArray,
    IN  UINT            MediumArraySize,
    IN  NDIS_HANDLE     MiniportAdapterHandle,
    IN  NDIS_HANDLE     WrapperConfigurationContext
    );

NDIS_STATUS SaitQueryInformation(
    IN  NDIS_HANDLE     MiniportAdapterContext,
    IN  NDIS_OID        Oid,
    IN  PVOID           InformationBuffer,
    IN  ULONG           InformationBufferLength,
    OUT PULONG          BytesWritten,
    OUT PULONG          BytesNeeded
    );

NDIS_STATUS SaitReconfigure(
    OUT PNDIS_STATUS    OpenErrorStatus,
    IN  NDIS_HANDLE     MiniportAdapterContext,
    IN  NDIS_HANDLE     WrapperConfigurationContext
    );

NDIS_STATUS SaitReset(
    OUT PBOOLEAN        AddressingReset,
    IN  NDIS_HANDLE     MiniportAdapterContext
    );

NDIS_STATUS SaitSend(
    IN  NDIS_HANDLE     MiniportAdapterContext,
    IN  PNDIS_PACKET    Packet,
    IN  UINT            Flags
    );

NDIS_STATUS SaitSetInformation(
    IN  NDIS_HANDLE     MiniportAdapterContext,
    IN  NDIS_OID        Oid,
    IN  PVOID           InformationBuffer,
    IN  ULONG           InformationBufferLength,
    OUT PULONG          BytesRead,
    OUT PULONG          BytesNeeded
    );

NDIS_STATUS SaitTransferData(
    OUT PNDIS_PACKET    Packet,
    OUT PUINT           BytesTransferred,
    IN  NDIS_HANDLE     MiniportAdapterContext,
    IN  NDIS_HANDLE     MiniportReceiveContext,
    IN  UINT            ByteOffset,
    IN  UINT            BytesToTransfer
    );

VOID SaitReturnPacket(
    IN  NDIS_HANDLE     MiniportAdapterContext,
    IN  PNDIS_PACKET    Packet
    ); 

VOID SaitAllocateComplete(
    IN  NDIS_HANDLE     MiniportAdapterContext,
    IN  PVOID           VirtualAddress,
    IN  PNDIS_PHYSICAL_ADDRESS  PhysicalAddress,
    IN  ULONG           Length,
    IN  PVOID           Context
    );

VOID SaitAdapterShutdown(
    IN  PVOID           ShutdownContext
    ); 

VOID SaitSendPackets(
    IN  NDIS_HANDLE     MiniportAdapterContext,
    IN  PPNDIS_PACKET   PacketArray,
    IN  UINT            NumberOfPackets
    );

#ifdef NDIS51_MINIPORT
VOID SaitCancelSendPackets(
    IN  NDIS_HANDLE     MiniportAdapterContext,
    IN  PVOID           CancelId
    );

VOID SaitPnPEventNotify(
    IN  NDIS_HANDLE     MiniportAdapterContext,
    IN  NDIS_DEVICE_PNP_EVENT   PnPEvent,
    IN  PVOID           InformationBuffer,
    IN  ULONG           InformationBufferLength
    );
#endif 


NDIS_STATUS SaitFindHardware(
    IN  PSAIT_ADAPTER    Adapter,
    IN  NDIS_HANDLE         WrapperConfigurationContext
    );

VOID SaitFreeResources(
    IN  PSAIT_ADAPTER    Adapter
    );

NDIS_STATUS SaitInitializeSend(
    IN  PSAIT_ADAPTER    Adapter
    );

VOID SaitFreeSend(
    IN  PSAIT_ADAPTER    Adapter
    );

NTSTATUS SaitUsbPacketWrite(
    IN PSAIT_ADAPTER   Adapter, 
    IN PVOID             pPacket,
    IN UINT              Flags
    );

NTSTATUS SaitUsbPacketWriteCompletionRoutine(
    IN  PDEVICE_OBJECT DeviceObject,
    IN  PIRP           Irp,
    IN  PVOID          Context
    );

NTSTATUS SaitConvertNdisToUsb(
    IN PSAIT_ADAPTER   Adapter, 
    IN PVOID             pPack,
	IN OUT PSAIT_WRITE_CONTEXT pWriteContext
	);

PSAIT_WRITE_CONTEXT 
SaitGetWriteContext(
    IN PSAIT_ADAPTER   Adapter
	);

VOID
SaitPushWritContext(
	IN PSAIT_ADAPTER   Adapter,
    PSAIT_WRITE_CONTEXT  pWriteContext
	);

NDIS_STATUS SaitInitializeReceive(
    IN  PSAIT_ADAPTER    Adapter
    );

VOID SaitFreeReceive(
    IN  PSAIT_ADAPTER    Adapter
    );

BOOLEAN SaitAcquireRemoveLock(
    IN  PSAIT_ADAPTER    Adapter
    );

VOID SaitReleaseRemoveLock(
    IN  PSAIT_ADAPTER    Adapter
    );

VOID SaitWaitForSafeHalt(
    IN  PSAIT_ADAPTER    Adapter
    );

VOID
SaitReceiveThread(
    IN PVOID Context
    );

NTSTATUS
SaitUsbPacketRead(
    IN  PSAIT_ADAPTER    Adapter
	);

NTSTATUS SaitUsbPacketReadCompletionRoutine(
    IN  PDEVICE_OBJECT DeviceObject,
    IN  PIRP           Irp,
    IN  PVOID          Context
    );

VOID SaitConvertUsbToNdis(
    IN  PSAIT_ADAPTER    Adapter,
	IN  PCHAR              pDataBuf,
	IN  UINT               DataLength
	);

///////////////////////////////////////////////////////////////////////////////////////////////////
// Debug
///////////////////////////////////////////////////////////////////////////////////////////////////

// definition of debug levels

#define DBG_NONE            0
#define DBG_ERR             1
#define DBG_WARN            2
#define DBG_MYTRACE			3
#define DBG_TRACE           4
#define DBG_INFO            5
#define DBG_VERB            6

#ifdef SAIT_WMI_TRACE

/*
tracepdb -f objchk_wxp_x86\i386\Sait.pdb -p C:\Sait
SET TRACE_FORMAT_SEARCH_PATH=C:\Sait

tracelog -start Sait -guid Sait.ctl -f Sait.log -flags 0x7FFFFFFF -level 5
tracelog -stop Sait

tracefmt -o Sait.txt -f Sait.log
*/

#define WPP_AREA_LEVEL_LOGGER(Area,Lvl)           WPP_LEVEL_LOGGER(Area)
#define WPP_AREA_LEVEL_ENABLED(Area,Lvl)          (WPP_LEVEL_ENABLED(Area) && WPP_CONTROL(WPP_BIT_##Area).Level >= Lvl)

#define WPP_CONTROL_GUIDS \
    WPP_DEFINE_CONTROL_GUID(Sait,(E8D1D728,C97D,4358,8A8B,280C16BEA9BE), \
        WPP_DEFINE_BIT(DBG_GENERAL)                 /* bit  0 = 0x00000001 */ \
        WPP_DEFINE_BIT(DBG_PNP)                     /* bit  1 = 0x00000002 */ \
        WPP_DEFINE_BIT(DBG_POWER)                   /* bit  2 = 0x00000004 */ \
        WPP_DEFINE_BIT(DBG_COUNT)                   /* bit  3 = 0x00000008 */ \
        WPP_DEFINE_BIT(DBG_CREATECLOSE)             /* bit  4 = 0x00000010 */ \
        WPP_DEFINE_BIT(DBG_WMI)                     /* bit  5 = 0x00000020 */ \
        WPP_DEFINE_BIT(DBG_UNLOAD)                  /* bit  6 = 0x00000040 */ \
        WPP_DEFINE_BIT(DBG_IO)                      /* bit  7 = 0x00000080 */ \
        WPP_DEFINE_BIT(DBG_INIT)                    /* bit  8 = 0x00000100 */ \
        WPP_DEFINE_BIT(DBG_09)                      /* bit  9 = 0x00000200 */ \
        WPP_DEFINE_BIT(DBG_10)                      /* bit 10 = 0x00000400 */ \
        WPP_DEFINE_BIT(DBG_11)                      /* bit 11 = 0x00000800 */ \
        WPP_DEFINE_BIT(DBG_12)                      /* bit 12 = 0x00001000 */ \
        WPP_DEFINE_BIT(DBG_13)                      /* bit 13 = 0x00002000 */ \
        WPP_DEFINE_BIT(DBG_14)                      /* bit 14 = 0x00004000 */ \
        WPP_DEFINE_BIT(DBG_15)                      /* bit 15 = 0x00008000 */ \
        WPP_DEFINE_BIT(DBG_16)                      /* bit 16 = 0x00010000 */ \
        WPP_DEFINE_BIT(DBG_17)                      /* bit 17 = 0x00020000 */ \
        WPP_DEFINE_BIT(DBG_18)                      /* bit 18 = 0x00040000 */ \
        WPP_DEFINE_BIT(DBG_19)                      /* bit 19 = 0x00080000 */ \
        WPP_DEFINE_BIT(DBG_20)                      /* bit 20 = 0x00100000 */ \
        WPP_DEFINE_BIT(DBG_21)                      /* bit 21 = 0x00200000 */ \
        WPP_DEFINE_BIT(DBG_22)                      /* bit 22 = 0x00400000 */ \
        WPP_DEFINE_BIT(DBG_23)                      /* bit 23 = 0x00800000 */ \
        WPP_DEFINE_BIT(DBG_24)                      /* bit 24 = 0x01000000 */ \
        WPP_DEFINE_BIT(DBG_25)                      /* bit 25 = 0x02000000 */ \
        WPP_DEFINE_BIT(DBG_26)                      /* bit 26 = 0x04000000 */ \
        WPP_DEFINE_BIT(DBG_27)                      /* bit 27 = 0x08000000 */ \
        WPP_DEFINE_BIT(DBG_28)                      /* bit 28 = 0x10000000 */ \
        WPP_DEFINE_BIT(DBG_29)                      /* bit 29 = 0x20000000 */ \
        WPP_DEFINE_BIT(DBG_30)                      /* bit 30 = 0x40000000 */ \
        WPP_DEFINE_BIT(DBG_31)                      /* bit 31 = 0x80000000 */ \
        )

#else

// definition of debug areas

#define DBG_GENERAL         (1 << 0)
#define DBG_PNP             (1 << 1)
#define DBG_POWER           (1 << 2)
#define DBG_COUNT           (1 << 3)
#define DBG_CREATECLOSE     (1 << 4)
#define DBG_WMI             (1 << 5)
#define DBG_UNLOAD          (1 << 6)
#define DBG_IO              (1 << 7)
#define DBG_INIT            (1 << 8)

#define DBG_ALL             0xFFFFFFFF

VOID SaitDebugPrint(
    IN ULONG    Area,
    IN ULONG    Level,
    IN PCCHAR   Format,
    IN          ...
    );

#endif

NTSTATUS SaitSubmitUrbSynch(
    IN  PSAIT_ADAPTER            Adapter,
    IN  PURB                        Urb
    );

NTSTATUS SaitResetPipe(
    IN  PSAIT_ADAPTER            Adapter,
    IN  USBD_PIPE_HANDLE                    PipeHandle
    );

NTSTATUS SaitAbortPipe(
    IN  PSAIT_ADAPTER            Adapter,
    IN  USBD_PIPE_HANDLE                    PipeHandle
    );

NTSTATUS SaitResetDevice(
    IN  PSAIT_ADAPTER            Adapter
    );

NTSTATUS SaitSelectAlternateInterface(
    IN  PSAIT_ADAPTER            Adapter,
    IN  ULONG                               AlternateSetting
    );

//BY SKKIM
NDIS_STATUS
NICRegisterDevice(
    VOID
    );

NDIS_STATUS
NICDeregisterDevice(
    VOID
    );
NTSTATUS
NICDispatch(
    IN PDEVICE_OBJECT           DeviceObject,
    IN PIRP                     Irp
    );

#endif // __SAIT_H__

