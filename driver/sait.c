//////////////////////////////////////////////////////////////////////////////////////////////////
// Sait.c
//
// Daewoo USB Network Adapter의 Main 모듈.
//

#include "pch.h"
#ifdef SAIT_WMI_TRACE
#include "Sait.tmh"
#endif

NDIS_HANDLE                     wrapperHandle;
PSAIT_ADAPTER 				gdeviceExtension;
///////////////////////////////////////////////////////////////////////////////////////////////////
//  DriverEntry 
//      드라이버의 최초 시작지점.
//      이 Entry Point는 I/O 시스템에서 호출된다.
//
//  Arguments:
//      IN  DriverObject
//              드라이버 오브젝트의 포인터
//
//      IN  RegistryPath
//              드라이버와 관계된 레지스트리 패스, Unicode String이다.
//
//  Return Value:
//      Status
//
NDIS_STATUS DriverEntry(
    IN  PDRIVER_OBJECT  DriverObject,
    IN  PUNICODE_STRING RegistryPath
    )
{
    NDIS_STATUS                     status;
    //NDIS_HANDLE                     wrapperHandle;
    NDIS_MINIPORT_CHARACTERISTICS   miniportChars;

    SaitDebugPrint(DBG_INIT, DBG_MYTRACE, __FUNCTION__"++");
    SaitDebugPrint(DBG_INIT, DBG_INFO, "Compiled at %s on %s", __TIME__, __DATE__);

#ifdef DBG
//    DbgBreakPoint();
#endif

    // 드라이버를 NDIS에 등록한다.
    NdisMInitializeWrapper(&wrapperHandle, DriverObject, RegistryPath, NULL);
    if (wrapperHandle == NULL)
    {
        status = NDIS_STATUS_FAILURE;

        SaitDebugPrint(DBG_INIT, DBG_ERR, __FUNCTION__"--. STATUS %x", status);

        return status;
    }

    // 등록할 miniport characteristics structure를 만든다.
    NdisZeroMemory(&miniportChars, sizeof(NDIS_MINIPORT_CHARACTERISTICS));

    // TODO: set ndis version
    miniportChars.MajorNdisVersion = 5;
#ifdef NDIS51_MINIPORT
    miniportChars.MinorNdisVersion = 1;
#else
    miniportChars.MinorNdisVersion = 0;
#endif

    miniportChars.CheckForHangHandler = SaitCheckForHang;
    miniportChars.HaltHandler = SaitHalt;
    miniportChars.InitializeHandler = SaitInitialize;
    miniportChars.QueryInformationHandler = SaitQueryInformation;

    // ReconfigureHandler를 현 버젼에서 NDIS에서 절대 호출하지 않는다.
//    miniportChars.ReconfigureHandler = SaitReconfigure;

    miniportChars.ResetHandler = SaitReset;
    miniportChars.ReturnPacketHandler = SaitReturnPacket;
    
    miniportChars.SendPacketsHandler = SaitSendPackets;//NULL;
	//miniportChars.SendHandler        = SaitSend;
    
    miniportChars.SetInformationHandler = SaitSetInformation;
    miniportChars.AllocateCompleteHandler = SaitAllocateComplete;

#ifdef NDIS51_MINIPORT
    miniportChars.CancelSendPacketsHandler = SaitCancelSendPackets;
    miniportChars.PnPEventNotifyHandler = SaitPnPEventNotify;
    miniportChars.AdapterShutdownHandler = SaitAdapterShutdown;
#endif

    // miniport handlers를 등록한다.
    status = NdisMRegisterMiniport(wrapperHandle, &miniportChars, sizeof(NDIS_MINIPORT_CHARACTERISTICS));
    if (status != NDIS_STATUS_SUCCESS)
    {
        NdisTerminateWrapper(wrapperHandle, NULL);
    }

    SaitDebugPrint(DBG_INIT, DBG_MYTRACE, __FUNCTION__"--. STATUS %x", status);

    return status;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
//  SaitInitialize
//      NDIS 드라이버의 StartDevice..
//
//  Arguments:
//      OUT OpenErrorStatus
//              NDIS_STATUS_OPEN_ERROR를 리턴하게 될 경우 에러 사태값을 저장하여 리턴한다.
//
//      OUT SelectedMediumIndex
//              MediumArray에서 선택된 Medium의 인덱스 값
//
//      IN  MediumArray
//              선택가능한 Medium 값 들.
//
//      IN  MediumArraySize
//              MediumArray의 크기.
//
//      IN  MiniportAdapterHandle
//              miniport adapter handle
//
//      IN  WrapperConfigurationContext
//              레지스트리 엑세스를 위해 사용되는 Context
//
//  Return Value:
//      Status
//
NDIS_STATUS SaitInitialize(
    OUT PNDIS_STATUS    OpenErrorStatus,
    OUT PUINT           SelectedMediumIndex,
    IN  PNDIS_MEDIUM    MediumArray,
    IN  UINT            MediumArraySize,
    IN  NDIS_HANDLE     MiniportAdapterHandle,
    IN  NDIS_HANDLE     WrapperConfigurationContext
    )
{
    NDIS_STATUS                     status;
    ULONG                           index;
    PSAIT_ADAPTER        adapter;
    PUCHAR                          networkAddress;
    UINT                            length;
    NDIS_HANDLE                     configurationHandle;
    PNDIS_CONFIGURATION_PARAMETER   configurationParameter;

    SaitDebugPrint(DBG_INIT, DBG_TRACE, __FUNCTION__"++");

    adapter = NULL;

    do
    {
        // 지원되는 Medium이 드라이버에서 사용가는한 Medium이 있는지를 조사한다.
        for (index = 0; index < MediumArraySize; ++index)
        {
            if (MediumArray[index] == SAIT_MEDIUM_TYPE)
                break;
        }

        if (index == MediumArraySize)
        {
            status = NDIS_STATUS_UNSUPPORTED_MEDIA;
            break;
        }

        // 선택된 Medium을 SelectedMediumIndex에 저장한다.
        *SelectedMediumIndex = index;

        // TODO: Allocate adapter
		// Adapter 구조체를 할당 한 후 값을 설정한다.
		// Adapter 구조체는 WDM의 DeviceExtention과 거의 같은 용도로 사용된다고 보면됨.
        status = NdisAllocateMemoryWithTag(
                    &adapter,
                    sizeof(SAIT_ADAPTER),
                    SAIT_POOL_TAG
                    );

        if (status != NDIS_STATUS_SUCCESS)
        {
            break;
        }

        NdisZeroMemory(adapter, sizeof(SAIT_ADAPTER));

        // Miniport Handle을 저장.
        adapter->AdapterHandle = MiniportAdapterHandle;

        // RemoveCount에 1을 설정한다.
		// 이 값이 0이 될 겨우 Halt가 호출 된 경우이다.
        adapter->RemoveCount = 1;
        adapter->HaltPending = FALSE;

        // Remove event 초기화.
        NdisInitializeEvent(&adapter->RemoveEvent);

        adapter->CurrentPowerState = NdisDeviceStateD0;

        adapter->MediaState = NdisMediaStateConnected;

        adapter->CurrentPacketFilter = NDIS_PACKET_TYPE_DIRECTED | NDIS_PACKET_TYPE_MULTICAST | NDIS_PACKET_TYPE_BROADCAST;

        // Adapter Type을 NDIS에 알려줌..
        NdisMSetAttributesEx(
            MiniportAdapterHandle,
            adapter,
            0,
#ifdef NDIS51_MINIPORT            
            NDIS_ATTRIBUTE_DESERIALIZE,
#else 
            NDIS_ATTRIBUTE_DESERIALIZE | NDIS_ATTRIBUTE_USES_SAFE_BUFFER_APIS, 
#endif               
            NdisInterfaceInternal
            );

		// USB Configuration 정보를 읽어 온 후 초기화 한다.
        status = SaitFindHardware(adapter, WrapperConfigurationContext);
        if (status != NDIS_STATUS_SUCCESS)
        {
            break;
        }

        // TODO: read NIC address
        for (index = 0; index < SAIT_LENGTH_OF_ADDRESS; ++index)
        {
            adapter->PermanentAddress[index] = (UCHAR)index;
        }

        // 레지스트리에 저장된 정보를 읽어온다.
        NdisOpenConfiguration(
            &status,
            &configurationHandle,
            WrapperConfigurationContext
            );

        if (status == NDIS_STATUS_SUCCESS)
        {
            // MAC Address 정보를 읽어옴.
            NdisReadNetworkAddress(
                &status,
                &networkAddress,
                &length,
                configurationHandle
                );

            if ((status == NDIS_STATUS_SUCCESS) && (length == SAIT_LENGTH_OF_ADDRESS))
            {
                NdisMoveMemory(adapter->CurrentAddress, networkAddress, SAIT_LENGTH_OF_ADDRESS);
            }
            else
            {
                NdisMoveMemory(adapter->CurrentAddress, adapter->PermanentAddress, SAIT_LENGTH_OF_ADDRESS);
            }
        // TODO: Read registry

            NdisCloseConfiguration(configurationHandle);
        }

        // 레지스트리 읽기 실패는 Critical한 에러가 아님..
		// 계속 진행..
        status = NDIS_STATUS_SUCCESS;

		KeInitializeEvent(
                &adapter->ReceiveEvent,
                NotificationEvent,    
                FALSE                 
                );

        // TODO: allocate shared memory, packets, buffers, etc
        status = SaitInitializeSend(adapter);
        if (status != NDIS_STATUS_SUCCESS)
        {
            break;
        }

        status = SaitInitializeReceive(adapter);
        if (status != NDIS_STATUS_SUCCESS)
        {
            break;
        }

#ifdef NDIS50_MINIPORT
        // NDIS 5.0 이하 버젼에서는 ShutdownHandler를 등록해 줘하함..
		// 이상 버젼에서는 NdisMRegisterMiniport에서 등록해 줌..
		// 시스템 종료시 호출 됨.
        NdisMRegisterAdapterShutdownHandler(
            adapter->AdapterHandle,
            adapter,
            SaitAdapterShutdown
            );
#endif         
    }
    while (FALSE);

    if (status != NDIS_STATUS_SUCCESS)
    {
        // 초기화 실패일 경우, 할당했던 자원을 해제함.
        SaitFreeResources(adapter);
    }

    // Receive Thread를 Enable함.
	adapter->bRunningPollingThread = TRUE;

    // Ndis에서 실제 사용하지 않음..
    *OpenErrorStatus = status;

    SaitDebugPrint(DBG_INIT, DBG_TRACE, __FUNCTION__"--. STATUS %x", status);

    return status;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
//  SaitHalt
//      NIC을 종료함, NDIS Miniport에서 가장 마지막에 호출 됨.
//      현제 모든 IRP를 종료하고, 할당 받은 리소스를 해제함.
//
//  Arguments:
//      OUT MiniportAdapterContext
//              our adapter object
//
//  Return Value:
//      None
//
VOID SaitHalt(
    IN  NDIS_HANDLE MiniportAdapterContext
    )
{
    PSAIT_ADAPTER    adapter;

    SaitDebugPrint(DBG_UNLOAD, DBG_TRACE, __FUNCTION__"++");

    adapter = (PSAIT_ADAPTER)MiniportAdapterContext;

	adapter->bRunningPollingThread = FALSE;

    SaitWaitForSafeHalt(adapter);
	// 안정적인 종료를 위해 대기..
	NdisMSleep(100000);
	
    //*****************************************************************
    //*****************************************************************
    // TODO:  stop the NIC hardware
    //*****************************************************************
    //*****************************************************************

#ifdef NDIS50_MINIPORT
	// NDIS 5.0 이하 버젼일 경우. 등록한 Shutdown Handler를 해제한다.
    NdisMDeregisterAdapterShutdownHandler(adapter->AdapterHandle);
#endif   

	NICDeregisterDevice();

    SaitFreeResources(adapter);

	
    SaitDebugPrint(DBG_UNLOAD, DBG_TRACE, __FUNCTION__"--");

    return;
}


///////////////////////////////////////////////////////////////////////////////////////////////////
//  SaitFindHardware
//      하드웨어 자원을 초기화 한다.
//
//  Arguments:
//      IN  Adapter
//              our adapter object
//
//      IN  WrapperConfigurationContext
//              NDIS configuration context
//
//  Return Value:
//      Status
//
NDIS_STATUS SaitFindHardware(
    IN  PSAIT_ADAPTER    Adapter,
    IN  NDIS_HANDLE         WrapperConfigurationContext
    )
{
    NDIS_STATUS                     status;
    PURB                            urb;
    PUSB_DEVICE_DESCRIPTOR          deviceDescriptor;
    PUSB_CONFIGURATION_DESCRIPTOR   configDescriptor;
    USHORT                          size;
    PUSBD_INTERFACE_LIST_ENTRY      interfaceBuffer;
    PUSBD_INTERFACE_LIST_ENTRY      interfaceList;
    UCHAR                           numInterfaces;
    UCHAR                           index;
    PUSB_INTERFACE_DESCRIPTOR       interfaceDescriptor;
    PUSBD_INTERFACE_INFORMATION     interfaceInfo;
    PUSB_ENDPOINT_DESCRIPTOR        epDescriptor;
	PUSBD_PIPE_INFORMATION pipeInformation;

    SaitDebugPrint(DBG_PNP, DBG_TRACE, __FUNCTION__"++");

    // Device Object를 가져온다.
    NdisMGetDeviceProperty(
        Adapter->AdapterHandle,
        &Adapter->PhysicalDeviceObject,
        &Adapter->DeviceObject,
        &Adapter->LowerDeviceObject,
        NULL,
        NULL
        );

	gdeviceExtension = Adapter;
    urb = NULL;
    deviceDescriptor = NULL;
    configDescriptor = NULL;
    interfaceBuffer = NULL;

    do
    {
        // URB 전송을 위해 URB를 새로 할당한다.
        urb = (PURB)ExAllocatePoolWithTag(
                        NonPagedPool, 
                        sizeof(URB), 
                        SAIT_POOL_TAG
                        );

        if (urb == NULL)
        {
            status = STATUS_INSUFFICIENT_RESOURCES;
            break;
        }

        // 최초, USB 장치에서 Device Descriptor를 읽어온다.
        deviceDescriptor = (PUSB_DEVICE_DESCRIPTOR)ExAllocatePoolWithTag(
                                                    NonPagedPool, 
                                                    sizeof(USB_DEVICE_DESCRIPTOR), 
                                                    SAIT_POOL_TAG
                                                    );
        if (deviceDescriptor == NULL)
        {
            status = STATUS_INSUFFICIENT_RESOURCES;
            break;
        }

        UsbBuildGetDescriptorRequest(
            urb, 
            (USHORT)sizeof(struct _URB_CONTROL_DESCRIPTOR_REQUEST),
            USB_DEVICE_DESCRIPTOR_TYPE,
            0,
            0,
            deviceDescriptor,
            NULL,
            sizeof(USB_DEVICE_DESCRIPTOR),
            NULL
            );

        status = SaitSubmitUrbSynch(Adapter, urb);
        if (!NT_SUCCESS(status))
        {
            break;
        }

        // Configuration Descriptor 정보를 읽어온다.
		// 처음에 Descriptor Buffer를 NULL로 보내면, 필요한 사이즈가 Return 됨.
        configDescriptor = (PUSB_CONFIGURATION_DESCRIPTOR)ExAllocatePoolWithTag(
                                                            NonPagedPool, 
                                                            sizeof(USB_CONFIGURATION_DESCRIPTOR), 
                                                            SAIT_POOL_TAG
                                                            );

        if (configDescriptor == NULL)
        {
            status = STATUS_INSUFFICIENT_RESOURCES;
            break;
        }

        UsbBuildGetDescriptorRequest(
            urb,
            (USHORT)sizeof(struct _URB_CONTROL_DESCRIPTOR_REQUEST),
            USB_CONFIGURATION_DESCRIPTOR_TYPE,
            0,
            0,
            configDescriptor,
            NULL,
            sizeof(USB_CONFIGURATION_DESCRIPTOR),
            NULL
            );

        status = SaitSubmitUrbSynch(Adapter, urb);
        if (!NT_SUCCESS(status))
        {
            break;
        }

        size = configDescriptor->wTotalLength;
        ExFreePool(configDescriptor);

		// 필요한 버퍼 크기만큼 메모리를 할당 받는다.
        configDescriptor = (PUSB_CONFIGURATION_DESCRIPTOR)ExAllocatePoolWithTag(
                                                            NonPagedPool, 
                                                            size, 
                                                            SAIT_POOL_TAG
                                                            );

        if (configDescriptor == NULL)
        {
            status = STATUS_INSUFFICIENT_RESOURCES;
            break;
        }

        UsbBuildGetDescriptorRequest(
            urb,
            (USHORT)sizeof(struct _URB_CONTROL_DESCRIPTOR_REQUEST),
            USB_CONFIGURATION_DESCRIPTOR_TYPE,
            0,
            0,
            configDescriptor,
            NULL,
            size,
            NULL
            );

        status = SaitSubmitUrbSynch(Adapter, urb);
        if (!NT_SUCCESS(status))
        {
            break;
        }

        // 읽어온 Configuration Descriptor에서 디바이스에서 사용할 인터페이스를 찾은 후에,
		// 사용 가능한 상태로 만든다.
        numInterfaces = configDescriptor->bNumInterfaces;
        interfaceBuffer = interfaceList = 
            (PUSBD_INTERFACE_LIST_ENTRY)ExAllocatePoolWithTag(
                                            NonPagedPool, 
                                            sizeof(USBD_INTERFACE_LIST_ENTRY) * (numInterfaces + 1), 
                                            SAIT_POOL_TAG
                                            );

        if (interfaceBuffer == NULL)
        {
            status = STATUS_INSUFFICIENT_RESOURCES;
            break;
        }

        for (index = 0; index < numInterfaces; ++index)
        {
            interfaceDescriptor = USBD_ParseConfigurationDescriptorEx(
                                    configDescriptor,
                                    configDescriptor,
                                    index,
                                    0,
                                    -1,
                                    -1,
                                    -1
                                    );

            if (interfaceDescriptor != NULL)
            {
                interfaceList->InterfaceDescriptor = interfaceDescriptor;
                interfaceList->Interface = NULL;

                ++interfaceList;
            }
        }

        interfaceList->InterfaceDescriptor = NULL;
        interfaceList->Interface = NULL;

        ExFreePool(urb);
        urb = USBD_CreateConfigurationRequestEx(configDescriptor, interfaceBuffer);
        if (urb == NULL)
        {
            status = STATUS_INSUFFICIENT_RESOURCES;
            break;
        }

        interfaceInfo = &urb->UrbSelectConfiguration.Interface;

        interfaceDescriptor = interfaceBuffer->InterfaceDescriptor;
        if (interfaceDescriptor->bNumEndpoints != 2)
        {
            status = STATUS_DEVICE_CONFIGURATION_ERROR;
            break;
        }

        epDescriptor = (PUSB_ENDPOINT_DESCRIPTOR)interfaceDescriptor;

        epDescriptor = (PUSB_ENDPOINT_DESCRIPTOR)USBD_ParseDescriptors(
                                                    configDescriptor, 
                                                    configDescriptor->wTotalLength, 
                                                    epDescriptor, 
                                                    USB_ENDPOINT_DESCRIPTOR_TYPE
                                                    );

        if ((epDescriptor == NULL) || 
            (epDescriptor->bEndpointAddress != 0x2) ||
            (epDescriptor->bmAttributes != USB_ENDPOINT_TYPE_BULK) ||
            (epDescriptor->wMaxPacketSize != 0x40))
        {
            status = STATUS_DEVICE_CONFIGURATION_ERROR;
            break;
        }
        else
        {
            interfaceInfo->Pipes[0].MaximumTransferSize = 4096;
            ++epDescriptor;
        }

        epDescriptor = (PUSB_ENDPOINT_DESCRIPTOR)USBD_ParseDescriptors(
                                                    configDescriptor, 
                                                    configDescriptor->wTotalLength, 
                                                    epDescriptor, 
                                                    USB_ENDPOINT_DESCRIPTOR_TYPE
                                                    );

        if ((epDescriptor == NULL) || 
            (epDescriptor->bEndpointAddress != 0x86) ||
            (epDescriptor->bmAttributes != USB_ENDPOINT_TYPE_BULK) ||
            (epDescriptor->wMaxPacketSize != 0x40))
        {
            status = STATUS_DEVICE_CONFIGURATION_ERROR;
            break;
        }
        else
        {
            interfaceInfo->Pipes[1].MaximumTransferSize = 4096;
            ++epDescriptor;
        }

        status = SaitSubmitUrbSynch(Adapter, urb);
        if (!NT_SUCCESS(status))
        {
            break;
        }

        Adapter->InterfaceInformation = 
            (PUSBD_INTERFACE_INFORMATION)ExAllocatePoolWithTag(
                                            NonPagedPool, 
                                            interfaceInfo->Length, 
                                            SAIT_POOL_TAG
                                            );

        if (Adapter->InterfaceInformation == NULL)
        {
            status = STATUS_INSUFFICIENT_RESOURCES;
            break;
        }

        RtlCopyMemory(Adapter->InterfaceInformation, interfaceInfo, interfaceInfo->Length);

		// In, Out Pipe의 핸들을 저장한다.
		// USB Bulk 파이프를 통한 데이터 전송을 위해 필요..
		for (index=0; index<interfaceInfo->NumberOfPipes; index++) {

            pipeInformation = &(Adapter->InterfaceInformation)->Pipes[index];

            if ( UsbdPipeTypeBulk == pipeInformation->PipeType )
            {
                if ( USB_ENDPOINT_DIRECTION_IN( pipeInformation->EndpointAddress ) ) {
                    Adapter->hUsbInPipe = pipeInformation->PipeHandle;
                }

                if ( USB_ENDPOINT_DIRECTION_OUT( pipeInformation->EndpointAddress ) ) {
                    Adapter->hUsbOutPipe = pipeInformation->PipeHandle;
                }
            } 
        }

        Adapter->ConfigHandle = urb->UrbSelectConfiguration.ConfigurationHandle;
        Adapter->ConfigDescriptor = configDescriptor;
        configDescriptor = NULL;
    }while(FALSE);

    if (urb != NULL)
    {
        ExFreePool(urb);
    }

    if (deviceDescriptor != NULL)
    {
        ExFreePool(deviceDescriptor);
    }

    if (configDescriptor != NULL)
    {
        ExFreePool(configDescriptor);
    }

    if (interfaceBuffer != NULL)
    {
        ExFreePool(interfaceBuffer);
    }

    SaitDebugPrint(DBG_PNP, DBG_TRACE, __FUNCTION__"--. STATUS %x", status);

    return status;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
//  SaitFreeHardware
//      하드웨어 리소스를 해제한다.
//
//  Arguments:
//      IN  Adapter
//              our adapter object
//
//  Return Value:
//      None
//
VOID SaitFreeHardware(
    IN  PSAIT_ADAPTER    Adapter
    )
{
    NTSTATUS    status;
    PURB        urb;

    SaitDebugPrint(DBG_PNP, DBG_TRACE, __FUNCTION__"++");
    if (Adapter->ConfigDescriptor != NULL)
    {
        {
            urb = (PURB)ExAllocatePoolWithTag(
                            NonPagedPool, 
                            sizeof(struct _URB_SELECT_CONFIGURATION), 
                            SAIT_POOL_TAG
                            );

            if (urb != NULL)
            {
                UsbBuildSelectConfigurationRequest(
                    urb, 
                    (USHORT)sizeof(struct _URB_SELECT_CONFIGURATION),
                    NULL
                    );

                status = SaitSubmitUrbSynch(Adapter, urb); 

                ExFreePool(urb);
            }
            else
            {
                status = STATUS_INSUFFICIENT_RESOURCES;
            }
        }

        if (Adapter->ConfigDescriptor != NULL)
        {
            ExFreePool(Adapter->ConfigDescriptor);
            Adapter->ConfigDescriptor = NULL;
        }
    }

    if (Adapter->InterfaceInformation != NULL)
    {
        ExFreePool(Adapter->InterfaceInformation);
        Adapter->InterfaceInformation = NULL;
    }

    SaitDebugPrint(DBG_PNP, DBG_TRACE, __FUNCTION__"--");

    return;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
//  SaitFreeResources
//      모든 Adapter Resource를 해제한다.
//
//  Arguments:
//      OUT Adapter
//              our adapter object
//
//  Return Value:
//      None
//
VOID SaitFreeResources(
    IN  PSAIT_ADAPTER    Adapter
    )
{
    SaitDebugPrint(DBG_UNLOAD, DBG_TRACE, __FUNCTION__"++");

    if (Adapter == NULL)
    {
        SaitDebugPrint(DBG_UNLOAD, DBG_TRACE, __FUNCTION__"--");
        return;
    }

    SaitFreeSend(Adapter);
    SaitFreeReceive(Adapter);
    SaitFreeHardware(Adapter);

    NdisFreeMemory(Adapter, sizeof(SAIT_ADAPTER), 0);

    SaitDebugPrint(DBG_UNLOAD, DBG_TRACE, __FUNCTION__"--");

    return;
}

#ifdef NDIS51_MINIPORT
///////////////////////////////////////////////////////////////////////////////////////////////////
//  SaitPnPEventNotify
//      Pnp event notification callback
//
//  Arguments:
//      IN  MiniportAdapterContext
//              our adapter object
//
//      IN  PnPEvent
//              pnp event
//
//      IN  InformationBuffer
//              additional information
//
//      IN  InformationBufferLength
//              size of information buffer
//
//  Return Value:
//      None
//
VOID SaitPnPEventNotify(
    IN  NDIS_HANDLE     MiniportAdapterContext,
    IN  NDIS_DEVICE_PNP_EVENT   PnPEvent,
    IN  PVOID           InformationBuffer,
    IN  ULONG           InformationBufferLength
    )
{
    SaitDebugPrint(DBG_PNP, DBG_TRACE, __FUNCTION__"++");

    switch (PnPEvent)
    {
    case NdisDevicePnPEventQueryRemoved:
        SaitDebugPrint(DBG_PNP, DBG_INFO, __FUNCTION__": NdisDevicePnPEventQueryRemoved");
        break;
    case NdisDevicePnPEventRemoved:
        SaitDebugPrint(DBG_PNP, DBG_INFO, __FUNCTION__": NdisDevicePnPEventRemoved");
        break;       
    case NdisDevicePnPEventSurpriseRemoved:
        SaitDebugPrint(DBG_PNP, DBG_INFO, __FUNCTION__": NdisDevicePnPEventSurpriseRemoved");
        break;
    case NdisDevicePnPEventQueryStopped:
        SaitDebugPrint(DBG_PNP, DBG_INFO, __FUNCTION__": NdisDevicePnPEventQueryStopped");
        break;
    case NdisDevicePnPEventStopped:
        SaitDebugPrint(DBG_PNP, DBG_INFO, __FUNCTION__": NdisDevicePnPEventStopped");
        break;      
    case NdisDevicePnPEventPowerProfileChanged:
        SaitDebugPrint(DBG_PNP, DBG_INFO, __FUNCTION__": NdisDevicePnPEventPowerProfileChanged");
        break;      
    default:
        SaitDebugPrint(DBG_PNP, DBG_WARN, __FUNCTION__": Unknown PnpEvent %x", PnPEvent);
        break;         
    }

    SaitDebugPrint(DBG_PNP, DBG_TRACE, __FUNCTION__"--");

    return;
}
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////
//  SaitAdapterShutdown
//      shutdown notification handler
//
//  Arguments:
//      IN  ShutdownContext
//              our adapter object
//
//  Return Value:
//      None
//
VOID SaitAdapterShutdown(
    IN  PVOID           ShutdownContext
    )
{
    PSAIT_ADAPTER    adapter;

    SaitDebugPrint(DBG_IO, DBG_TRACE, __FUNCTION__"++");

    adapter = (PSAIT_ADAPTER)ShutdownContext;

    //*****************************************************************
    //*****************************************************************
    // TODO:  NIC을 초기 상태로 만든다. (RESET)
    //*****************************************************************
    //*****************************************************************

	adapter->bRunningPollingThread = FALSE;

	SaitAbortPipe(adapter, adapter->hUsbInPipe);
    SaitAbortPipe(adapter, adapter->hUsbOutPipe);

	while (adapter->hReceiveThread != NULL)
    {
        //
        // Sleep 50 milliseconds.
        //
        NdisMSleep(50000);
    }

    SaitDebugPrint(DBG_IO, DBG_TRACE, __FUNCTION__"--");

    return;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
//  SaitCheckForHang
//      check for hang handler
//
//  Arguments:
//      IN  MiniportAdapterContext
//              our adapter object
//
//  Return Value:
//      TRUE, if NIC is in a hang state
//
BOOLEAN SaitCheckForHang(
    IN  NDIS_HANDLE MiniportAdapterContext
    )
{
    PSAIT_ADAPTER    adapter;

    SaitDebugPrint(DBG_IO, DBG_TRACE, __FUNCTION__"++");

    adapter = (PSAIT_ADAPTER)MiniportAdapterContext;

    SaitDebugPrint(DBG_IO, DBG_TRACE, __FUNCTION__"--");

    return FALSE;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
//  SaitReset
//      resets NIC
//
//  Arguments:
//      OUT AddressingReset
//              return TRUE, if NDIS needs to refresh address information
//
//      IN  MiniportAdapterContext
//              our adapter object
//
//  Return Value:
//      Status
//
NDIS_STATUS SaitReset(
    OUT PBOOLEAN        AddressingReset,
    IN  NDIS_HANDLE     MiniportAdapterContext
    )
{
    PSAIT_ADAPTER    adapter;

    SaitDebugPrint(DBG_IO, DBG_TRACE, __FUNCTION__"++");

    adapter = (PSAIT_ADAPTER)MiniportAdapterContext;

    //*****************************************************************
    //*****************************************************************
    // TODO:  reset our device
    //*****************************************************************
    //*****************************************************************

    *AddressingReset = FALSE;

    SaitDebugPrint(DBG_IO, DBG_TRACE, __FUNCTION__"--");

    return NDIS_STATUS_SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
//  SaitAcquireRemoveLock
//      Acquires remove lock.
//
//  Arguments:
//      IN  Adapter
//              our adapter object
//
//  Return Value:
//      FALSE if halt is pending, TRUE otherwise.
//
BOOLEAN SaitAcquireRemoveLock(
    IN  PSAIT_ADAPTER    Adapter
    )
{
    LONG    count;

    count = NdisInterlockedIncrement(&Adapter->RemoveCount);

    ASSERT(count > 0);

    if (Adapter->HaltPending)
    {
        SaitReleaseRemoveLock(Adapter);

        return FALSE;
    }

    return TRUE;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
//  SaitReleaseRemoveLock
//      Releases remove lock.
//
//  Arguments:
//      IN  Adapter
//              our adapter object
//
//  Return Value:
//      None.
//
VOID SaitReleaseRemoveLock(
    IN  PSAIT_ADAPTER    Adapter
    )
{
    LONG    count;

    count = NdisInterlockedDecrement(&Adapter->RemoveCount);

    ASSERT(count >= 0);

    if (count == 0)
    {
        NdisSetEvent(&Adapter->RemoveEvent);
    }

    return;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
//  SaitWaitForSafeHalt
//      모든 Removelock이 해제 될때까지 대기한다.
//
//  Arguments:
//      IN  Adapter
//              our adapter object
//
//  Return Value:
//      None.
//
//  Comment:
//
VOID SaitWaitForSafeHalt(
    IN  PSAIT_ADAPTER    Adapter
    )
{
    Adapter->HaltPending = TRUE;
    SaitReleaseRemoveLock(Adapter);

	SaitAbortPipe(Adapter, Adapter->hUsbInPipe);
    SaitAbortPipe(Adapter, Adapter->hUsbOutPipe);

    while (TRUE)
    {
		// 모든 USB 패킷의 처리가 완료될 때 까지 대기한다.
        if (!NdisWaitEvent(&Adapter->RemoveEvent, 1000))
        {
            SaitDebugPrint(DBG_IO, DBG_WARN, __FUNCTION__": still waiting for safe halt");
        }
		// Receive Thread가 종료될때까지 대기한다.
		if (!NdisWaitEvent(&Adapter->ReceiveCloseEvent, 1000))
		{
			SaitDebugPrint(DBG_IO, DBG_WARN, __FUNCTION__": still waiting for receive thread closed");
		}
        else
        {
            break;
        }
    }

    return;
}

