//////////////////////////////////////////////////////////////////////////////////////////////////
// send.c
//
// Send 관련 처리 루틴들..
//

#include "pch.h"
#ifdef SAIT_WMI_TRACE
#include "send.tmh"
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////
//  SaitInitializeSend
//      Send resource를 초기화하고, 할당한다.
//
//  Arguments:
//      IN  Adapter
//              our adapter object
//
//  Return Value:
//      Status
//
NDIS_STATUS SaitInitializeSend(
    IN  PSAIT_ADAPTER    Adapter
    )
{
    NDIS_STATUS     status;
	PSAIT_WRITE_CONTEXT  writeContext;
	ULONG           index;

    SaitDebugPrint(DBG_IO, DBG_MYTRACE, __FUNCTION__"++");

    status = NDIS_STATUS_SUCCESS;

    NdisAllocateSpinLock(&Adapter->SendLock);
    NdisInitializeEvent(&Adapter->SendIdleEvent);

    InitializeListHead(&Adapter->SendQueue);
    Adapter->SendCount = 1;

    InitializeListHead(&Adapter->SendPendingQueue);
    Adapter->SendPendingCount = 0;

	InitializeListHead(&Adapter->SendDescriptorListHead);

	// Send를 하기위한 Context 구조체를 재 사용하기 위해서 미리 할당 받는다.
	for(index = 0; index < 32; ++index)
	{
		// SAIT_WRITE_CONTEXT 구조체를 할당 받는다.
		writeContext = ExAllocatePoolWithTag(
                                   NonPagedPool, 
                                   sizeof(SAIT_WRITE_CONTEXT), 
                                   SAIT_POOL_TAG
                                   );

		if(writeContext == NULL)
		{
			continue;
		}

		// 전송할 패킷을 위한 Buffer를 할당 받는다.
		writeContext->VirtualAddress = ExAllocatePoolWithTag(
                                                      NonPagedPool, 
                                                      SAIT_MAX_TOTAL_SIZE + 16, 
                                                      SAIT_POOL_TAG
                                                      );
		if(writeContext->VirtualAddress == NULL)
		{
			ExFreePool(writeContext);
			continue;
		}

		// URB 구조체를 할당 받는다.
		writeContext->pUrb = ExAllocatePoolWithTag(
                                           NonPagedPool, 
                                           sizeof(struct _URB_BULK_OR_INTERRUPT_TRANSFER), 
                                           SAIT_POOL_TAG
                                           );

		if(writeContext->pUrb == NULL)
		{
			ExFreePool(writeContext->VirtualAddress);
			ExFreePool(writeContext);
			continue;
		}

		InsertTailList(&Adapter->SendDescriptorListHead, &writeContext->ListEntry);
	}

	// Create an IOCTL interface
    //
    NICRegisterDevice();

    Adapter->bSendAllocated = TRUE;

    SaitDebugPrint(DBG_IO, DBG_MYTRACE, __FUNCTION__"--. STATUS %x", status);

    return status;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
//  SaitFreeSend
//      Send resource를 해제한다.
//
//  Arguments:
//      IN  Adapter
//              our adapter object
//
//  Return Value:
//      None
//
VOID SaitFreeSend(
    IN  PSAIT_ADAPTER    Adapter
    )
{
    PLIST_ENTRY     listEntry;
    PNDIS_PACKET    packet;
    LIST_ENTRY      freeList;
    ULONG           count;
	PLIST_ENTRY     ContextEntry;
    PSAIT_WRITE_CONTEXT   pWriteContext;

    SaitDebugPrint(DBG_IO, DBG_MYTRACE, __FUNCTION__"++");

    InitializeListHead(&freeList);

    if (Adapter->bSendAllocated)
    {
        // Pending 되어 있는 Resource들을 모두 대제한다.
        NdisAcquireSpinLock(&Adapter->SendLock);

        while (!IsListEmpty(&Adapter->SendPendingQueue))
        {
            listEntry = RemoveHeadList(&Adapter->SendPendingQueue);
            --Adapter->SendPendingCount;

            InsertTailList(&freeList, listEntry);
        }

        NdisReleaseSpinLock(&Adapter->SendLock);

        while (!IsListEmpty(&freeList))
        {
            listEntry = RemoveHeadList(&freeList);
            packet = CONTAINING_RECORD(listEntry, NDIS_PACKET, MiniportReserved);

            NdisMSendComplete(
                Adapter->AdapterHandle,
                packet,
                NDIS_STATUS_FAILURE
                );
        }

        // 모든 Send Packet들이 완료 될 때까지 대기한다.
        NdisAcquireSpinLock(&Adapter->SendLock);

        --Adapter->SendCount;
        count = Adapter->SendCount;

        NdisReleaseSpinLock(&Adapter->SendLock);

        if (count != 0)
        {
            while (TRUE)
            {
                if (!NdisWaitEvent(&Adapter->SendIdleEvent, 1000))
                {
                    SaitDebugPrint(DBG_IO, DBG_WARN, __FUNCTION__": still waiting to complete all send packets");
                }
                else
                {
                    break;
                }
            }
        }

		// Write Context 메모리 해제.
		NdisAcquireSpinLock(&Adapter->SendLock);
		while (!IsListEmpty(&Adapter->SendDescriptorListHead))
		{
			ContextEntry = RemoveHeadList(&Adapter->SendDescriptorListHead);
			pWriteContext = CONTAINING_RECORD(ContextEntry, SAIT_WRITE_CONTEXT, ListEntry);

			if(pWriteContext != NULL)
			{
			    ExFreePool(pWriteContext->VirtualAddress);
				ExFreePool(pWriteContext->pUrb);
			    ExFreePool(pWriteContext);
			}
		}
		NdisReleaseSpinLock(&Adapter->SendLock);

        NdisFreeSpinLock(&Adapter->SendLock);

        Adapter->bSendAllocated = FALSE;
    }

    SaitDebugPrint(DBG_IO, DBG_MYTRACE, __FUNCTION__"--");

    return;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
//  SaitSend
//      Ndis Packet을 전송하는 Routine. NDIS에서 호출된다.
//
//  Arguments:
//      IN  MiniportAdapterContext
//              our adapter object
//
//      IN  Packet
//              전송할 NDIS 패킷.
//
//      IN  Flags
//              Packet flags
//
//  Return Value:
//      Status
//
NDIS_STATUS SaitSend(
    IN  NDIS_HANDLE     MiniportAdapterContext,
    IN  PNDIS_PACKET    Packet,
    IN  UINT            Flags
    )
{
    NDIS_STATUS status;
	PSAIT_ADAPTER adapter = (PSAIT_ADAPTER)MiniportAdapterContext;

	SaitDebugPrint(DBG_IO, DBG_MYTRACE, __FUNCTION__"++");

	// NDIS Packet을 USB Packet으로 변환하여 전송한다.
	status =  ( NDIS_STATUS ) SaitUsbPacketWrite(
                adapter,
                Packet,
                Flags
                );

  
    SaitDebugPrint(DBG_IO, DBG_MYTRACE, __FUNCTION__"--");
	SaitDebugPrint(DBG_IO, DBG_MYTRACE, "Return Status = %X", status);

    return status;
}

VOID SaitSendPackets(
    IN  NDIS_HANDLE     MiniportAdapterContext,
    IN  PPNDIS_PACKET   PacketArray,
    IN  UINT            NumberOfPackets
    )
{
    PSAIT_ADAPTER    adapter;
    ULONG                       index;
    PNDIS_PACKET                packet;
    UINT                        physicalCount;
    UINT                        bufferCount;
    PNDIS_BUFFER                buffer;
    UINT                        packetLength;

    SaitDebugPrint(DBG_IO, DBG_MYTRACE, __FUNCTION__"++");

    adapter = (PSAIT_ADAPTER)MiniportAdapterContext;

    NdisAcquireSpinLock(&adapter->SendLock);

    for (index = 0; index < NumberOfPackets; ++index)
    {
        packet = PacketArray[index];

        if (adapter->HaltPending)
        {
            NdisMSendComplete(
                adapter->AdapterHandle,
                packet,
                NDIS_STATUS_FAILURE
                );
        }
        else if (!IsListEmpty(&adapter->SendPendingQueue))
        {
            // we do not want to send this packet in front of 
            // pending send packets

            InsertTailList(&adapter->SendPendingQueue, (PLIST_ENTRY)&packet->MiniportReserved[0]);
            ++adapter->SendPendingCount;
        }
        else
        {
            // check if hardware resources are available for send
            if (TRUE)
            {
                // send
                NdisQueryPacket(
                    packet,
                    &physicalCount,
                    &bufferCount,
                    &buffer,
                    &packetLength
                    );

                ++adapter->SendCount;
                InsertTailList(&adapter->SendQueue, (PLIST_ENTRY)&packet->MiniportReserved[0]);


                // program the send
            }
            else
            {
                InsertTailList(&adapter->SendPendingQueue, (PLIST_ENTRY)&packet->MiniportReserved[0]);
                ++adapter->SendPendingCount;
            }
        }
    }

    NdisReleaseSpinLock(&adapter->SendLock);

    SaitDebugPrint(DBG_IO, DBG_MYTRACE, __FUNCTION__"--");

    return;
}
#ifdef NDIS51_MINIPORT
///////////////////////////////////////////////////////////////////////////////////////////////////
//  SaitCancelSendPackets
//      Pending 되어있는 Send Packet들을 취소한다.
//
//  Arguments:
//      IN  MiniportAdapterContext
//              our adapter object
//
//      IN  CancelId
//              packets with this id should be canceled
//
//  Return Value:
//      None
//
VOID SaitCancelSendPackets(
    IN  NDIS_HANDLE     MiniportAdapterContext,
    IN  PVOID           CancelId
    )
{
    PSAIT_ADAPTER    adapter;
    LIST_ENTRY                  cancelList;
    PLIST_ENTRY                 listEntry;
    PLIST_ENTRY                 nextEntry;
    PNDIS_PACKET                packet;
    PVOID                       packetId;

    SaitDebugPrint(DBG_IO, DBG_MYTRACE, __FUNCTION__"++");

    adapter = (PSAIT_ADAPTER)MiniportAdapterContext;

    InitializeListHead(&cancelList);

    NdisAcquireSpinLock(&adapter->SendLock);

    listEntry = adapter->SendPendingQueue.Flink;
    while (listEntry != &adapter->SendPendingQueue)
    {
        // get the NDIS_PACKET
        packet = CONTAINING_RECORD(listEntry, NDIS_PACKET, MiniportReserved);
        packetId = NdisGetPacketCancelId(packet);

        nextEntry = listEntry->Flink;

        // check if we need to cancel this packet
        if (packetId == CancelId)
        {
            // remove from pending list
            --adapter->SendPendingCount;
            RemoveEntryList(listEntry);

            // insert into cancel list
            InsertTailList(&cancelList, listEntry);
        }

        listEntry = nextEntry;
    }

    NdisReleaseSpinLock(&adapter->SendLock);

    while (!IsListEmpty(&cancelList))
    {
        listEntry = RemoveHeadList(&cancelList);

        packet = CONTAINING_RECORD(listEntry, NDIS_PACKET, MiniportReserved);

        NdisMSendComplete(
            adapter->AdapterHandle,
            packet,
            NDIS_STATUS_REQUEST_ABORTED
            );
    }

    SaitDebugPrint(DBG_IO, DBG_MYTRACE, __FUNCTION__"--");

    return;
}
#endif 

///////////////////////////////////////////////////////////////////////////////////////////////////
//  SaitUsbPacketWrite
//      NDIS Packet을 USB Packet으로 변환하여 전송한다.
//
//  Arguments:
//      IN  Adapter
//              our adapter object
//
//      IN  pPacket
//              전송할 NDIS Packet
//
//      IN  Flags
//              Ndis Packet Flags
//
//  Return Value:
//      Status
//
NTSTATUS
SaitUsbPacketWrite(
           IN PSAIT_ADAPTER   Adapter, 
           IN PVOID             pPacket,
           IN UINT              Flags
           )
{
    PSAIT_WRITE_CONTEXT       ioContext = NULL;
	NTSTATUS                    status;
	PIO_STACK_LOCATION          irpStack;
	PIRP                        irp = NULL;
	
	SaitDebugPrint(DBG_IO, DBG_MYTRACE, __FUNCTION__"++");

	if (!SaitAcquireRemoveLock(Adapter))
    {
        return STATUS_UNSUCCESSFUL;
    }

	status = STATUS_SUCCESS;
    ioContext = NULL;

	do
	{
		// IRP를 할당한다.
		irp = IoAllocateIrp( (CCHAR)(Adapter->LowerDeviceObject->StackSize + 1), FALSE );

		if ( NULL == irp )
		{
            status = STATUS_INSUFFICIENT_RESOURCES;
            break;
		}

		irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
        irp->IoStatus.Information = 0;

		// 사용 가능한 Write Context를 가져온다.
        ioContext = SaitGetWriteContext(Adapter);

		if (ioContext == NULL)
        {
            status = STATUS_INSUFFICIENT_RESOURCES;
            break;
        }

		if((ioContext->pUrb == NULL) || (ioContext->VirtualAddress == NULL))
		{
			status = STATUS_INSUFFICIENT_RESOURCES;
            break;
		}

		// NDIS Packet을 USB Packet으로 변환한다.
		status = SaitConvertNdisToUsb(Adapter, pPacket, ioContext);

        if (status != STATUS_SUCCESS)
        {
			status = STATUS_INSUFFICIENT_RESOURCES;
            break;
        }

		// URB 구조체를 초기화한다.
	    NdisZeroMemory(ioContext->pUrb, sizeof(struct _URB_BULK_OR_INTERRUPT_TRANSFER));

		// URB 구조체를 Bulk 통신을 위한 구조체로 만든다.
		UsbBuildInterruptOrBulkTransferRequest(
                                 ioContext->pUrb,
                                 sizeof(struct _URB_BULK_OR_INTERRUPT_TRANSFER),
                                 Adapter->hUsbOutPipe,
                                 ioContext->VirtualAddress,
                                 NULL,
                                 ioContext->BufferLen,
                                 USBD_TRANSFER_DIRECTION_OUT,
                                 NULL
                                 );

		irpStack = IoGetNextIrpStackLocation( irp );

		irpStack->MajorFunction = IRP_MJ_INTERNAL_DEVICE_CONTROL;
        irpStack->Parameters.Others.Argument1 = ioContext->pUrb;
        irpStack->Parameters.DeviceIoControl.IoControlCode = IOCTL_INTERNAL_USB_SUBMIT_URB;

		ioContext->Adapter = Adapter;
		ioContext->NullPacket = FALSE;

		// IRP 완료 루틴 설정.
		IoSetCompletionRoutine(
            irp, 
            SaitUsbPacketWriteCompletionRoutine,
            ioContext,
            TRUE,
            TRUE,
            TRUE
            );

		// URB를 다음 스택으로 전송한다.
		status = IoCallDriver(Adapter->LowerDeviceObject, irp);
		status = STATUS_PENDING;

	}while(FALSE);

	if(status == STATUS_PENDING)
	{
        SaitDebugPrint(DBG_IO, DBG_MYTRACE, __FUNCTION__"--");
		
		return STATUS_SUCCESS;
	}

	// Error 처리 루틴, 할당된 자원을 해제한다.
	if(irp != NULL)
	{
	    IoFreeIrp(irp);
	}

	if(ioContext != NULL)
	{
        SaitPushWritContext(Adapter, ioContext);
	}

	SaitDebugPrint(DBG_IO, DBG_MYTRACE, __FUNCTION__"--");
	return status;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
//  SaitConvertNdisToUsb
//      NDIS Packet을 USB Packet으로 변환한다.
//
//  Arguments:
//      IN  Adapter
//              our adapter object
//
//      IN  pPacket
//              전송할 NDIS Packet
//
//      IN  pWriteContext
//              사용 가능한 Write Context 구조체
//
//  Return Value:
//      Status
//
NTSTATUS
SaitConvertNdisToUsb(
    IN PSAIT_ADAPTER   Adapter, 
    IN PVOID             pPack,
	IN OUT PSAIT_WRITE_CONTEXT pWriteContext
	)
{
    PNDIS_BUFFER    ndisBuf;
    PNDIS_PACKET    pPacket = (PNDIS_PACKET) pPack;
	
	PUCHAR pBufData;
	PUCHAR pUsbPacket;

	UINT ndisPacketLen;
	UINT usbPacketLen;
	UINT bufLen;
	UINT Templen;
	UINT i =0;

	SaitDebugPrint(DBG_IO, DBG_MYTRACE, __FUNCTION__"++");

	// NDIS Packet에 관한 정보를 읽어온다.
	NdisQueryPacket(pPacket, NULL, NULL, &ndisBuf, &ndisPacketLen);

    ASSERT(ndisPacketLen);

	if(ndisPacketLen > 0)
	{
		usbPacketLen = ndisPacketLen;
		pWriteContext->BufferLen = usbPacketLen;
		//
		// Ndis Buffer의 길이가 Pipe의 최대 전송 크기의 배수일 경우
		// Receive 단에서 마지막 패킷을 인식할수 없다.
		// 따라서 여기에서 1byte 크기만큼 더 메모리를 할당한다.
		//
		if ((pWriteContext->BufferLen % Adapter->InterfaceInformation->Pipes[0].MaximumPacketSize ) == 0) {
            pWriteContext->BufferLen = pWriteContext->BufferLen + 1; 
		}
	}

	if(ndisPacketLen > SAIT_MAX_TOTAL_SIZE)
	{
		pWriteContext->BufferLen = 0;
        return STATUS_UNSUCCESSFUL;
	}

	if(!ndisBuf)
	{
		pWriteContext->BufferLen = 0;
		return STATUS_UNSUCCESSFUL;
	}

	// Buffer의 정보를 읽어온다.
	NdisQueryBuffer(ndisBuf, (PVOID *)&pBufData, &bufLen);

    pUsbPacket = pWriteContext->VirtualAddress;

	if(pUsbPacket == NULL)
	{
		pWriteContext->BufferLen = 0;
		return STATUS_UNSUCCESSFUL;
	}

	Templen = 0;

	while (pBufData)
    {
        ASSERT(pBufData);

		if((Templen + bufLen) <= ndisPacketLen)
		{
		    NdisMoveMemory(&pUsbPacket[Templen], pBufData, bufLen);
			Templen += bufLen;
			if(Templen == ndisPacketLen)
			{
				pBufData = NULL;
				break;
			}
		}
		else
		{
			// Error
			pWriteContext->BufferLen = 0;
			break;
		}

		NdisGetNextBuffer(ndisBuf, &ndisBuf);

		if(ndisBuf)
		{
			NdisQueryBuffer(ndisBuf, (PVOID *)&pBufData, &bufLen);
		}
		else
		{
			pBufData = NULL;
		}
    } 

	// 정상 적인 패킷이 아닐경우 에러 리턴.
	if((pBufData != NULL) || (Templen != ndisPacketLen))
	{
		pWriteContext->BufferLen = 0;
		return STATUS_UNSUCCESSFUL;
	}

	SaitDebugPrint(DBG_IO, DBG_MYTRACE, __FUNCTION__"--");
	return STATUS_SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
//  SaitUsbPacketWriteCompletionRoutine
//      SaitUsbPacketWrite의 완료 루틴.
//
//  Arguments:
//      IN  DeviceObject
//              Device Object 구조체.
//
//      IN  Irp
//              관련된 IRP 구조체
//
//      IN  Context
//              SaitUsbPacketWrite에서 설정한 Write Context 구조체.
//
//  Return Value:
//      Status
//
NTSTATUS SaitUsbPacketWriteCompletionRoutine(
    IN  PDEVICE_OBJECT DeviceObject,
    IN  PIRP           Irp,
    IN  PVOID          Context
    )
{
    PSAIT_WRITE_CONTEXT       WriteContext = (PSAIT_WRITE_CONTEXT)Context;
	NTSTATUS                    status;
	PSAIT_ADAPTER             Adapter;
	PVOID                       pBuffer;
	PURB                        pUrb;
	UINT                        BufLen;

	SaitDebugPrint(DBG_IO, DBG_MYTRACE, __FUNCTION__"++");

	Adapter = WriteContext->Adapter;
	pBuffer = WriteContext->VirtualAddress;
	pUrb = WriteContext->pUrb;
	BufLen = WriteContext->BufferLen;

	status = Irp->IoStatus.Status;

	if(status == STATUS_SUCCESS)
	{
		InterlockedIncrement(&Adapter->XmitOk);
	}

    // TODO: Free irp back to our irp pool
    IoFreeIrp(Irp);

    SaitReleaseRemoveLock(WriteContext->Adapter);

	SaitPushWritContext(Adapter, WriteContext);

	SaitDebugPrint(DBG_IO, DBG_MYTRACE, __FUNCTION__"--");
    return STATUS_MORE_PROCESSING_REQUIRED;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
//  SaitGetWriteContext
//      사용 가능한 Write Context 구조체를 하나 가져온다.
//
//  Arguments:
//      IN  Adapter
//              our adapter object.
//
//  Return Value:
//      Write Context Structure.
//
PSAIT_WRITE_CONTEXT 
SaitGetWriteContext(
    IN PSAIT_ADAPTER   Adapter
	)
{
	PSAIT_WRITE_CONTEXT   pWriteContext = NULL;
	PLIST_ENTRY         listEntry;

	NdisAcquireSpinLock(&Adapter->SendLock);
     
    listEntry = Adapter->SendDescriptorListHead.Flink;
    if(listEntry != &Adapter->SendDescriptorListHead)
	{
		// 사용 가능한 Write Context 구조체를 List에서 하나 가져온다.
		pWriteContext = CONTAINING_RECORD(listEntry, SAIT_WRITE_CONTEXT, ListEntry);
		RemoveEntryList(listEntry);
	}	
	
	NdisReleaseSpinLock(&Adapter->SendLock);

	return pWriteContext;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
//  SaitGetWriteContext
//      Write Context 구조체를 Queue에 저장한다.
//
//  Arguments:
//      IN  Adapter
//              our adapter object.
//  
//      PSAIT_WRITE_CONTEXT  pWriteContext
//              해제할 Write Context 구조체. 
//  Return Value:
//      None
//
VOID
SaitPushWritContext(
	IN PSAIT_ADAPTER   Adapter,
    PSAIT_WRITE_CONTEXT  pWriteContext
	)
{
	NdisAcquireSpinLock(&Adapter->SendLock);

	InsertTailList(&Adapter->SendDescriptorListHead, &pWriteContext->ListEntry);

	NdisReleaseSpinLock(&Adapter->SendLock);

	return;
}