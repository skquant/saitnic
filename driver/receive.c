//////////////////////////////////////////////////////////////////////////////////////////////////
// receive.c
//
// Receive 관련 처리 루틴들..
//


#include "pch.h"
#ifdef SAIT_WMI_TRACE
#include "receive.tmh"
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////
//  SaitInitializeReceive
//      Receive Resource를 초기화 하고 할당 받는다.
//
//  Arguments:
//      IN  Adapter
//              our adapter object
//
//  Return Value:
//      Status
//
NDIS_STATUS SaitInitializeReceive(
    IN  PSAIT_ADAPTER    Adapter
    )
{
    NDIS_STATUS         status;
    ULONG               index;
    PSAIT_RECEIVE_DESCRIPTOR receiveDescriptor;

    SaitDebugPrint(DBG_IO, DBG_MYTRACE, __FUNCTION__"++");

    status = NDIS_STATUS_SUCCESS;

    NdisAllocateSpinLock(&Adapter->ReceiveLock);
    NdisInitializeEvent(&Adapter->ReceiveIdleEvent);
	NdisInitializeEvent(&Adapter->ReceiveCloseEvent);

    InitializeListHead(&Adapter->ReceiveDescriptorListHead);

    Adapter->ReceiveCount = 1;

	// PacketPool을 할당 받는다.
    NdisAllocatePacketPoolEx(
        &status, 
        &Adapter->ReceivePacketPool,
        SAIT_MAX_RECV_PACKETS,
        SAIT_MAX_RECV_PACKETS,
        PROTOCOL_RESERVED_SIZE_IN_PACKET
        );

    if (status != NDIS_STATUS_SUCCESS)
    {
        return status;
    }

	// BufferPool을 할당 받는다.
    NdisAllocateBufferPool(
        &status,
        &Adapter->ReceiveBufferPool,
        SAIT_MAX_RECV_PACKETS
        );

    if (status != NDIS_STATUS_SUCCESS)
    {
        NdisFreePacketPool(Adapter->ReceivePacketPool);
        return status;
    }

	// 사용할 LookasideList를 초기화 한다.
    NdisInitializeNPagedLookasideList(
        &Adapter->ReceiveDescriptorLookaside,
        NULL,
        NULL,
        0,
        sizeof(SAIT_RECEIVE_DESCRIPTOR),
        SAIT_POOL_TAG,
        0
        );

	// 사용할 NDIS Packet을 미리 생성한다.
    for (index = 0; index < SAIT_MAX_RECV_PACKETS; ++index)
    {
		// Receive Descriptor를 할당 받는다.
        receiveDescriptor = (PSAIT_RECEIVE_DESCRIPTOR)
                                    NdisAllocateFromNPagedLookasideList(
                                        &Adapter->ReceiveDescriptorLookaside
                                        );

        if (receiveDescriptor == NULL)
        {
            continue;
        }

		// NDIS Buffer에 실제 데이터가 저장될 공간.
        status = NdisAllocateMemoryWithTag(
                    &receiveDescriptor->VirtualAddress,
                    SAIT_MAX_TOTAL_SIZE,
                    SAIT_POOL_TAG
                    );

        if (status != NDIS_STATUS_SUCCESS)
        {
            NdisFreeToNPagedLookasideList(
                &Adapter->ReceiveDescriptorLookaside,
                receiveDescriptor
                );

            continue;
        }


		// Packet Pool에서 Ndis Packet을 하나 할당 받는다.
        NdisAllocatePacket(
            &status,
            &receiveDescriptor->Packet,
            Adapter->ReceivePacketPool
            );

        if (status != NDIS_STATUS_SUCCESS)
        {
            NdisFreeMemory(
                receiveDescriptor->VirtualAddress,
                SAIT_MAX_TOTAL_SIZE,
                0
                );

            NdisFreeToNPagedLookasideList(
                &Adapter->ReceiveDescriptorLookaside,
                receiveDescriptor
                );

            continue;
        }

		// Buffer Pool에서 Ndis Buffer를 하나 할당 받는다.
        NdisAllocateBuffer(
            &status,
            &receiveDescriptor->Buffer,
            Adapter->ReceiveBufferPool,
            receiveDescriptor->VirtualAddress,
            SAIT_MAX_TOTAL_SIZE
            );

        if (status != NDIS_STATUS_SUCCESS)
        {
            NdisFreeMemory(
                receiveDescriptor->VirtualAddress,
                SAIT_MAX_TOTAL_SIZE,
                0
                );

            NdisFreePacket(receiveDescriptor->Packet);

            NdisFreeToNPagedLookasideList(
                &Adapter->ReceiveDescriptorLookaside,
                receiveDescriptor
                );

            continue;
        }

		// NDIS Packet의 OOB 데이터 설정
        NDIS_SET_PACKET_HEADER_SIZE(receiveDescriptor->Packet, SAIT_HEADER_SIZE);

		// NdisPacket에 NdisBuffer를 연결 시킨다.
        NdisChainBufferAtFront(receiveDescriptor->Packet, receiveDescriptor->Buffer);

        *(PSAIT_RECEIVE_DESCRIPTOR*)&receiveDescriptor->Packet->MiniportReserved[0] = receiveDescriptor;

        InsertTailList(&Adapter->ReceiveDescriptorListHead, &receiveDescriptor->ListEntry);

    }

	//
    // IRQL Passive level에서 동작하는 Receive용 System Thread를 하나 생성한다.
    //
	
	if(status == NDIS_STATUS_SUCCESS)
	{
	    status =  PsCreateSystemThread(
					    &Adapter->hReceiveThread,
					    (ACCESS_MASK) 0L,
					    NULL,
					    NULL,
					    NULL,
					    SaitReceiveThread,
					    Adapter
					    );

	    if (status != STATUS_SUCCESS)
		{
		    status = STATUS_INSUFFICIENT_RESOURCES;
		}
	}

    Adapter->bReceiveAllocated = TRUE;
	

    SaitDebugPrint(DBG_IO, DBG_MYTRACE, __FUNCTION__"--");

    return status;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
//  SaitFreeReceive
//      Receive Resource를 해제한다.
//
//  Arguments:
//      IN  Adapter
//              our adapter object
//
//  Return Value:
//      None
//
VOID SaitFreeReceive(
    IN  PSAIT_ADAPTER    Adapter
    )
{
    PLIST_ENTRY         listEntry;
    PSAIT_RECEIVE_DESCRIPTOR receiveDescriptor;
    ULONG               count;

    SaitDebugPrint(DBG_IO, DBG_MYTRACE, __FUNCTION__"++");

    // 우선 Receive Resouce가 할당 되었는지를 체크한다.
    if (Adapter->bReceiveAllocated)
    {
        // 모들 Recieve Packet 들의 제어권이 되돌아 올때까지 대기한다.
        NdisAcquireSpinLock(&Adapter->ReceiveLock);
        --Adapter->ReceiveCount;
        count = Adapter->ReceiveCount;
        NdisReleaseSpinLock(&Adapter->ReceiveLock);

        if (count != 0)
        {
            while (TRUE)
            {
                if (!NdisWaitEvent(&Adapter->ReceiveIdleEvent, 1000))
                {
                    SaitDebugPrint(DBG_IO, DBG_WARN, __FUNCTION__": still waiting to reclaim all receive packets");
                }
                else
                {
                    break;
                }
            }
        }

        // 모든 Receive descriptor를 해제한다.
        while (!IsListEmpty(&Adapter->ReceiveDescriptorListHead))
        {
            listEntry = RemoveHeadList(&Adapter->ReceiveDescriptorListHead);
            receiveDescriptor = CONTAINING_RECORD(listEntry, SAIT_RECEIVE_DESCRIPTOR, ListEntry);

            NdisAdjustBufferLength(receiveDescriptor->Buffer, SAIT_MAX_TOTAL_SIZE);
            NdisFreeBuffer(receiveDescriptor->Buffer);
            NdisFreePacket(receiveDescriptor->Packet);

            NdisFreeMemory(
                receiveDescriptor->VirtualAddress,
                SAIT_MAX_TOTAL_SIZE,
                0
                );

            NdisFreeToNPagedLookasideList(&Adapter->ReceiveDescriptorLookaside, receiveDescriptor);
        }

        NdisDeleteNPagedLookasideList(&Adapter->ReceiveDescriptorLookaside);
        NdisFreeBufferPool(Adapter->ReceiveBufferPool);
        NdisFreePacketPool(Adapter->ReceivePacketPool);

        NdisFreeSpinLock(&Adapter->ReceiveLock);

        Adapter->bReceiveAllocated = FALSE;
    }

    SaitDebugPrint(DBG_IO, DBG_MYTRACE, __FUNCTION__"--");

    return;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
//  SaitReturnPacket
//      패킷 Recieve가 완료 되었을 때, NDIS에서 호출되는 루틴.
//
//  Arguments:
//      IN  MiniportAdapterContext
//              our adapter object
//
//      IN  Packet
//              NDIS_PACKET
//
//  Return Value:
//      None
//
VOID SaitReturnPacket(
    IN  NDIS_HANDLE     MiniportAdapterContext,
    IN  PNDIS_PACKET    Packet
    )
{
    PSAIT_ADAPTER            adapter;
    PSAIT_RECEIVE_DESCRIPTOR receiveDescriptor;
    ULONG                   count;

    SaitDebugPrint(DBG_IO, DBG_MYTRACE, __FUNCTION__"++. Packet %p", Packet);

    adapter = (PSAIT_ADAPTER)MiniportAdapterContext;
    receiveDescriptor = *(PSAIT_RECEIVE_DESCRIPTOR*)&Packet->MiniportReserved[0];

    NdisDprAcquireSpinLock(&adapter->ReceiveLock);

    ASSERT(adapter->ReceiveCount > 0);

	NdisAdjustBufferLength(receiveDescriptor->Buffer, SAIT_MAX_TOTAL_SIZE);

    --adapter->ReceiveCount;
    count = adapter->ReceiveCount;

    InsertTailList(&adapter->ReceiveDescriptorListHead, &receiveDescriptor->ListEntry);

    NdisDprReleaseSpinLock(&adapter->ReceiveLock);

    if (count == 0)
    {
        NdisSetEvent(&adapter->ReceiveIdleEvent);
    }

	NdisInterlockedDecrement(&adapter->ReceiveUsbPacket);
	if(adapter->ReceiveUsbPacket < 3)
	{
	    KeSetEvent(&adapter->ReceiveEvent, 0, FALSE);
	}

    SaitDebugPrint(DBG_IO, DBG_MYTRACE, __FUNCTION__"--");

    return;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
//  SaitAllocateComplete
//      async memory allocation completed
//
//  Arguments:
//      IN  MiniportAdapterContext
//              our adapter object
//
//      IN  VirtualAddress
//              virtual address of allocated block
//
//      IN  PhysicalAddress
//              physical address of allocated block
//
//      IN  Length
//              length of allocated block
//
//      IN  Context
//              our context
//
//  Return Value:
//      None
//
VOID SaitAllocateComplete(
    IN  NDIS_HANDLE     MiniportAdapterContext,
    IN  PVOID           VirtualAddress,
    IN  PNDIS_PHYSICAL_ADDRESS  PhysicalAddress,
    IN  ULONG           Length,
    IN  PVOID           Context
    )
{
    SaitDebugPrint(DBG_IO, DBG_MYTRACE, __FUNCTION__"++");

    SaitDebugPrint(DBG_IO, DBG_MYTRACE, __FUNCTION__"--");

    return;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
//  SaitReceiveThread
//      USB에서 Packet을 받기위한 Thread 루틴.
//
//  Arguments:
//      IN  Context
//              our adapter object
//
//  Return Value:
//      None
//
VOID
SaitReceiveThread(
    IN PVOID Context
    )
{
    PSAIT_ADAPTER  Adapter = (PSAIT_ADAPTER)Context;
	NTSTATUS ntStatus, WaitStat;

	SaitDebugPrint(DBG_IO, DBG_MYTRACE, __FUNCTION__"++");

	while(Adapter->bRunningPollingThread)
	{
		ntStatus = SaitUsbPacketRead(Adapter);

		// 사용할 Receive Packet이 없을 경우, 대기한다.
        if(Adapter->ReceiveUsbPacket >= SAIT_MAX_RECV_PACKETS)
		{
			KeClearEvent(&Adapter->ReceiveEvent);

		    WaitStat = KeWaitForSingleObject(
                       &Adapter->ReceiveEvent,
                       Executive,
				       KernelMode,
				       FALSE,
                       NULL); 
		}
	}

	Adapter->hReceiveThread = NULL;
	NdisSetEvent(&Adapter->ReceiveCloseEvent);

	SaitDebugPrint(DBG_IO, DBG_MYTRACE, __FUNCTION__"--");
    
	PsTerminateSystemThread(0);
}

///////////////////////////////////////////////////////////////////////////////////////////////////
//  SaitUsbPacketRead
//      USB Read Packet을 전송하는 루틴.
//
//  Arguments:
//      IN  PSAIT_ADAPTER
//              our adapter object
//
//  Return Value:
//      Status
//
NTSTATUS
SaitUsbPacketRead(
    IN  PSAIT_ADAPTER    Adapter
	)
{
	NTSTATUS                    status = STATUS_SUCCESS;
	NTSTATUS                    WaitStat;
	PSAIT_READ_CONTEXT        ioContext = NULL;
	ULONG                       size;
	ULONG                       length;
    PURB                        urb = NULL;
	PCHAR                       databuf = NULL;
    PIO_STACK_LOCATION          irpStack;
	PIRP                        irp = NULL;
	UINT                        DataLen;

	SaitDebugPrint(DBG_IO, DBG_TRACE, __FUNCTION__"++");

	if (!SaitAcquireRemoveLock(Adapter))
    {
        return STATUS_UNSUCCESSFUL;
    }

	size = sizeof(struct _URB_BULK_OR_INTERRUPT_TRANSFER);
	length = SAIT_MAX_TOTAL_SIZE;

    do
	{
		// SAIT_READ_CONTEXT를 할당 받는다.
		ioContext = (PSAIT_READ_CONTEXT)ExAllocatePoolWithTag(
                                                    NonPagedPool, 
                                                    sizeof(SAIT_READ_CONTEXT), 
                                                    SAIT_POOL_TAG
                                                    );

		if (ioContext == NULL)
        {
            status = STATUS_INSUFFICIENT_RESOURCES;
            break;
        }

		// Receive Data를 저장할 버퍼를 할당 받는다.
		databuf = ExAllocatePoolWithTag(
		 	        NonPagedPool,
					SAIT_MAX_TOTAL_SIZE,
                    SAIT_POOL_TAG
                    );

        if (databuf == NULL)
        {
            break;
        }

	    // URB 구조체를 저장할 버퍼를 할당 받는다.
	    urb = (PURB)ExAllocatePoolWithTag(
                            NonPagedPool, 
                            size, 
                            SAIT_POOL_TAG
                            );

	    if (urb == NULL)
		{
            status = STATUS_INSUFFICIENT_RESOURCES;
            break;
		}

		// URB 구조체 초기화.
	    NdisZeroMemory(urb, size);

		// IRP를 하나 생성한다.
	    irp = IoAllocateIrp( (CCHAR)(Adapter->LowerDeviceObject->StackSize + 1), FALSE );

	    if ( NULL == irp )
		{
            status = STATUS_INSUFFICIENT_RESOURCES;
            break;
		}

		// 할당 받은 IRP값 초기화.
		irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
        irp->IoStatus.Information = 0;

		// Bulk URB를 하나 초기화 한다.
		UsbBuildInterruptOrBulkTransferRequest(
            urb,
            sizeof(struct _URB_BULK_OR_INTERRUPT_TRANSFER),
            Adapter->hUsbInPipe,
            databuf,
            NULL,
            length,
            USBD_TRANSFER_DIRECTION_IN | USBD_SHORT_TRANSFER_OK,
            NULL
            );

		// 할당받은 IRP에 URB를 붙인다.
		irpStack = IoGetNextIrpStackLocation( irp );

		irpStack->MajorFunction = IRP_MJ_INTERNAL_DEVICE_CONTROL;
        irpStack->Parameters.Others.Argument1 = urb;
        irpStack->Parameters.DeviceIoControl.IoControlCode = IOCTL_INTERNAL_USB_SUBMIT_URB;

		// Read Context 구조체 값을 설정한다.
		// 완료 루틴에서 이값을 참조할수 있다.
		ioContext->Adapter = Adapter;
		ioContext->BufferLen = SAIT_MAX_TOTAL_SIZE;
	    ioContext->Urb = urb;
		ioContext->VirtualAddress = databuf;

		// 완료루틴 설정.
		IoSetCompletionRoutine(
            irp, 
            SaitUsbPacketReadCompletionRoutine,
            ioContext,
            TRUE,
            TRUE,
            TRUE
            );

		// 현재 USB Bus에 전송된 Irp 갯수 Count를 하나 증가
		NdisInterlockedIncrement(&Adapter->ReceiveUsbPacket);

		// URB를 하위 드라이버에 전송한다.
		status = IoCallDriver(Adapter->LowerDeviceObject, irp);
		// 항상 Pending을 Return한다.
		status = STATUS_PENDING;

	}while(FALSE);

	if(status == STATUS_PENDING)
	{
		SaitDebugPrint(DBG_IO, DBG_MYTRACE, __FUNCTION__"--");
		return status;
	}

	// 할당된 자원들을 해제한다.
	// 에러처리..
	if(databuf != NULL)
	{
		ExFreePool(databuf);
	}

	if(urb != NULL)
	{
		ExFreePool(urb);
	}

	if(irp != NULL)
	{
	    IoFreeIrp(irp);
	}

	if(ioContext != NULL)
	{
		ExFreePool(ioContext);
	}

	SaitDebugPrint(DBG_IO, DBG_TRACE, __FUNCTION__"--");
	return status;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
//  SaitConvertUsbToNdis
//      USB Receive가 정상 종료 되었을 때, USB 구조체를 NDIS Packet 구조체로 만든다.
//
//  Arguments:
//      IN  PSAIT_ADAPTER
//              our adapter object
//      IN  PCHAR  pDataBuf
//              USB Packet의 데이터 Buffer
//      IN  UINT   DataLength
//              Buffer의 데이터 크기
//
//  Return Value:
//      None
//
VOID
SaitConvertUsbToNdis(
    IN  PSAIT_ADAPTER    Adapter,
	IN  PCHAR              pDataBuf,
	IN  UINT               DataLength
	)
{
	NTSTATUS status = STATUS_SUCCESS;
	PLIST_ENTRY         listEntry;
	PSAIT_RECEIVE_DESCRIPTOR  receiveDescriptor;
	PCHAR     pNdisBuf;
	PNDIS_BUFFER  pBufDesc;

	SaitDebugPrint(DBG_IO, DBG_MYTRACE, __FUNCTION__"++");

	// Data Buffer가 없을 때, 그냥 Return한다.
	if(pDataBuf == NULL)
	{
		return;
	}

	do
	{
		// DataLength가 Network packet 크기 보다 클 경우, 그냥 Return한다.
		if ( DataLength > SAIT_MAX_TOTAL_SIZE )
		{
			break;
		}

		NdisAcquireSpinLock(&Adapter->ReceiveLock);

		listEntry = Adapter->ReceiveDescriptorListHead.Flink;
        if(listEntry != &Adapter->ReceiveDescriptorListHead)
		{
			// 사용 가능한 Receive Descriptor를 하나 가져온다.
			receiveDescriptor = CONTAINING_RECORD(listEntry, SAIT_RECEIVE_DESCRIPTOR, ListEntry);
			RemoveEntryList(listEntry);
            ++Adapter->ReceiveCount;

			// NDIS Buffer에 Usb Data Buffer의 내용을 복사한다.
			NdisMoveMemory(receiveDescriptor->VirtualAddress, pDataBuf, DataLength);
			NdisAdjustBufferLength(receiveDescriptor->Buffer, DataLength);

			NDIS_SET_PACKET_HEADER_SIZE(
                (PNDIS_PACKET) receiveDescriptor->Packet,
                14 		// Ethernet Header Size(source + destination + type)
                );

			NDIS_SET_PACKET_STATUS((PNDIS_PACKET) receiveDescriptor->Packet, NDIS_STATUS_SUCCESS);

			NdisReleaseSpinLock(&Adapter->ReceiveLock);

			// NDIS에 Packet이 수신 되었음을 알린다.
			// 완료는 ReturnPacket 함수를 통해 이루어진다.
			NdisMIndicateReceivePacket(
                Adapter->AdapterHandle,
                &((PNDIS_PACKET) receiveDescriptor->Packet),
                1
                );

			NdisAcquireSpinLock(&Adapter->ReceiveLock);
		}

		NdisReleaseSpinLock(&Adapter->ReceiveLock);

	}while(FALSE);

	SaitDebugPrint(DBG_IO, DBG_MYTRACE, __FUNCTION__"--");
	return;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
//  SaitUsbPacketReadCompletionRoutine
//      SaitUsbPacketRead의 완료 루틴. 
//
//  Arguments:
//      IN  PDEVICE_OBJECT DeviceObject
//              Device Object 구조체
//      IN  PIRP           Irp
//              관련된 IRP 구조체
//      IN  PVOID          Context
//              SaitUsbPacketRead에서 설정한 Context 구조체, PSAIT_READ_CONTEXT  
//
//  Return Value:
//      Status
//
NTSTATUS SaitUsbPacketReadCompletionRoutine(
    IN  PDEVICE_OBJECT DeviceObject,
    IN  PIRP           Irp,
    IN  PVOID          Context
    )
{
    PSAIT_READ_CONTEXT        ReadContext = (PSAIT_READ_CONTEXT)Context;
	PSAIT_ADAPTER             Adapter;

	NTSTATUS                    status;
	PVOID                       pBuffer;
	PURB                        pUrb;
	UINT                        BufLen;
	UINT                        DataLen;

	SaitDebugPrint(DBG_IO, DBG_TRACE, __FUNCTION__"++");

	Adapter = ReadContext->Adapter;
	pBuffer = ReadContext->VirtualAddress;
	pUrb = ReadContext->Urb;
	BufLen = ReadContext->BufferLen;
	DataLen = pUrb->UrbBulkOrInterruptTransfer.TransferBufferLength;

	status = Irp->IoStatus.Status;

	if(status == STATUS_SUCCESS)
	{
		InterlockedIncrement(&Adapter->RcvOk);
		// Receive가 성공할 경우, USB Packet을 NDIS Packet으로 변환한다.
        SaitConvertUsbToNdis(Adapter, pBuffer, DataLen);
	}
	else
	{
		NdisInterlockedDecrement(&Adapter->ReceiveUsbPacket);
		// Receive Packet의 수가 3보다 적을경우, 대기하고 있는 쓰래드를 다시 돌린다.
	    if(Adapter->ReceiveUsbPacket < 3)
		{
	        KeSetEvent(&Adapter->ReceiveEvent, 0, FALSE);
		}
	}

	// free the resources
	ExFreePool(pBuffer);
	ExFreePool(pUrb);
	IoFreeIrp(Irp);
	
	SaitReleaseRemoveLock(ReadContext->Adapter);

	ExFreePool(ReadContext);
    
	SaitDebugPrint(DBG_IO, DBG_TRACE, __FUNCTION__"--");
    return STATUS_MORE_PROCESSING_REQUIRED;
}
