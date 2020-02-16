//////////////////////////////////////////////////////////////////////////////////////////////////
// receive.c
//
// Receive ���� ó�� ��ƾ��..
//


#include "pch.h"
#ifdef SAIT_WMI_TRACE
#include "receive.tmh"
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////
//  SaitInitializeReceive
//      Receive Resource�� �ʱ�ȭ �ϰ� �Ҵ� �޴´�.
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

	// PacketPool�� �Ҵ� �޴´�.
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

	// BufferPool�� �Ҵ� �޴´�.
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

	// ����� LookasideList�� �ʱ�ȭ �Ѵ�.
    NdisInitializeNPagedLookasideList(
        &Adapter->ReceiveDescriptorLookaside,
        NULL,
        NULL,
        0,
        sizeof(SAIT_RECEIVE_DESCRIPTOR),
        SAIT_POOL_TAG,
        0
        );

	// ����� NDIS Packet�� �̸� �����Ѵ�.
    for (index = 0; index < SAIT_MAX_RECV_PACKETS; ++index)
    {
		// Receive Descriptor�� �Ҵ� �޴´�.
        receiveDescriptor = (PSAIT_RECEIVE_DESCRIPTOR)
                                    NdisAllocateFromNPagedLookasideList(
                                        &Adapter->ReceiveDescriptorLookaside
                                        );

        if (receiveDescriptor == NULL)
        {
            continue;
        }

		// NDIS Buffer�� ���� �����Ͱ� ����� ����.
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


		// Packet Pool���� Ndis Packet�� �ϳ� �Ҵ� �޴´�.
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

		// Buffer Pool���� Ndis Buffer�� �ϳ� �Ҵ� �޴´�.
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

		// NDIS Packet�� OOB ������ ����
        NDIS_SET_PACKET_HEADER_SIZE(receiveDescriptor->Packet, SAIT_HEADER_SIZE);

		// NdisPacket�� NdisBuffer�� ���� ��Ų��.
        NdisChainBufferAtFront(receiveDescriptor->Packet, receiveDescriptor->Buffer);

        *(PSAIT_RECEIVE_DESCRIPTOR*)&receiveDescriptor->Packet->MiniportReserved[0] = receiveDescriptor;

        InsertTailList(&Adapter->ReceiveDescriptorListHead, &receiveDescriptor->ListEntry);

    }

	//
    // IRQL Passive level���� �����ϴ� Receive�� System Thread�� �ϳ� �����Ѵ�.
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
//      Receive Resource�� �����Ѵ�.
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

    // �켱 Receive Resouce�� �Ҵ� �Ǿ������� üũ�Ѵ�.
    if (Adapter->bReceiveAllocated)
    {
        // ��� Recieve Packet ���� ������� �ǵ��� �ö����� ����Ѵ�.
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

        // ��� Receive descriptor�� �����Ѵ�.
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
//      ��Ŷ Recieve�� �Ϸ� �Ǿ��� ��, NDIS���� ȣ��Ǵ� ��ƾ.
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
//      USB���� Packet�� �ޱ����� Thread ��ƾ.
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

		// ����� Receive Packet�� ���� ���, ����Ѵ�.
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
//      USB Read Packet�� �����ϴ� ��ƾ.
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
		// SAIT_READ_CONTEXT�� �Ҵ� �޴´�.
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

		// Receive Data�� ������ ���۸� �Ҵ� �޴´�.
		databuf = ExAllocatePoolWithTag(
		 	        NonPagedPool,
					SAIT_MAX_TOTAL_SIZE,
                    SAIT_POOL_TAG
                    );

        if (databuf == NULL)
        {
            break;
        }

	    // URB ����ü�� ������ ���۸� �Ҵ� �޴´�.
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

		// URB ����ü �ʱ�ȭ.
	    NdisZeroMemory(urb, size);

		// IRP�� �ϳ� �����Ѵ�.
	    irp = IoAllocateIrp( (CCHAR)(Adapter->LowerDeviceObject->StackSize + 1), FALSE );

	    if ( NULL == irp )
		{
            status = STATUS_INSUFFICIENT_RESOURCES;
            break;
		}

		// �Ҵ� ���� IRP�� �ʱ�ȭ.
		irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
        irp->IoStatus.Information = 0;

		// Bulk URB�� �ϳ� �ʱ�ȭ �Ѵ�.
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

		// �Ҵ���� IRP�� URB�� ���δ�.
		irpStack = IoGetNextIrpStackLocation( irp );

		irpStack->MajorFunction = IRP_MJ_INTERNAL_DEVICE_CONTROL;
        irpStack->Parameters.Others.Argument1 = urb;
        irpStack->Parameters.DeviceIoControl.IoControlCode = IOCTL_INTERNAL_USB_SUBMIT_URB;

		// Read Context ����ü ���� �����Ѵ�.
		// �Ϸ� ��ƾ���� �̰��� �����Ҽ� �ִ�.
		ioContext->Adapter = Adapter;
		ioContext->BufferLen = SAIT_MAX_TOTAL_SIZE;
	    ioContext->Urb = urb;
		ioContext->VirtualAddress = databuf;

		// �Ϸ��ƾ ����.
		IoSetCompletionRoutine(
            irp, 
            SaitUsbPacketReadCompletionRoutine,
            ioContext,
            TRUE,
            TRUE,
            TRUE
            );

		// ���� USB Bus�� ���۵� Irp ���� Count�� �ϳ� ����
		NdisInterlockedIncrement(&Adapter->ReceiveUsbPacket);

		// URB�� ���� ����̹��� �����Ѵ�.
		status = IoCallDriver(Adapter->LowerDeviceObject, irp);
		// �׻� Pending�� Return�Ѵ�.
		status = STATUS_PENDING;

	}while(FALSE);

	if(status == STATUS_PENDING)
	{
		SaitDebugPrint(DBG_IO, DBG_MYTRACE, __FUNCTION__"--");
		return status;
	}

	// �Ҵ�� �ڿ����� �����Ѵ�.
	// ����ó��..
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
//      USB Receive�� ���� ���� �Ǿ��� ��, USB ����ü�� NDIS Packet ����ü�� �����.
//
//  Arguments:
//      IN  PSAIT_ADAPTER
//              our adapter object
//      IN  PCHAR  pDataBuf
//              USB Packet�� ������ Buffer
//      IN  UINT   DataLength
//              Buffer�� ������ ũ��
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

	// Data Buffer�� ���� ��, �׳� Return�Ѵ�.
	if(pDataBuf == NULL)
	{
		return;
	}

	do
	{
		// DataLength�� Network packet ũ�� ���� Ŭ ���, �׳� Return�Ѵ�.
		if ( DataLength > SAIT_MAX_TOTAL_SIZE )
		{
			break;
		}

		NdisAcquireSpinLock(&Adapter->ReceiveLock);

		listEntry = Adapter->ReceiveDescriptorListHead.Flink;
        if(listEntry != &Adapter->ReceiveDescriptorListHead)
		{
			// ��� ������ Receive Descriptor�� �ϳ� �����´�.
			receiveDescriptor = CONTAINING_RECORD(listEntry, SAIT_RECEIVE_DESCRIPTOR, ListEntry);
			RemoveEntryList(listEntry);
            ++Adapter->ReceiveCount;

			// NDIS Buffer�� Usb Data Buffer�� ������ �����Ѵ�.
			NdisMoveMemory(receiveDescriptor->VirtualAddress, pDataBuf, DataLength);
			NdisAdjustBufferLength(receiveDescriptor->Buffer, DataLength);

			NDIS_SET_PACKET_HEADER_SIZE(
                (PNDIS_PACKET) receiveDescriptor->Packet,
                14 		// Ethernet Header Size(source + destination + type)
                );

			NDIS_SET_PACKET_STATUS((PNDIS_PACKET) receiveDescriptor->Packet, NDIS_STATUS_SUCCESS);

			NdisReleaseSpinLock(&Adapter->ReceiveLock);

			// NDIS�� Packet�� ���� �Ǿ����� �˸���.
			// �Ϸ�� ReturnPacket �Լ��� ���� �̷������.
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
//      SaitUsbPacketRead�� �Ϸ� ��ƾ. 
//
//  Arguments:
//      IN  PDEVICE_OBJECT DeviceObject
//              Device Object ����ü
//      IN  PIRP           Irp
//              ���õ� IRP ����ü
//      IN  PVOID          Context
//              SaitUsbPacketRead���� ������ Context ����ü, PSAIT_READ_CONTEXT  
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
		// Receive�� ������ ���, USB Packet�� NDIS Packet���� ��ȯ�Ѵ�.
        SaitConvertUsbToNdis(Adapter, pBuffer, DataLen);
	}
	else
	{
		NdisInterlockedDecrement(&Adapter->ReceiveUsbPacket);
		// Receive Packet�� ���� 3���� �������, ����ϰ� �ִ� �����带 �ٽ� ������.
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
