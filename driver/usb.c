//////////////////////////////////////////////////////////////////////////////////////////////////
// usb.c
//
// USB 처리 관련 루틴들.
//

#include "pch.h"
#ifdef SAIT_WMI_TRACE
#include "usb.tmh"
#endif

#ifdef WIN2K
// make sure that IoReuseIrp is defined
NTKERNELAPI
VOID
IoReuseIrp(
    IN OUT PIRP Irp,
    IN NTSTATUS Iostatus
    );
#endif


///////////////////////////////////////////////////////////////////////////////////////////////////
//  SaitSubmitUrbSynch
//      Bus Driver에 USB를 동기 방식으로 보낸다.
//
//  Arguments:
//      IN  Adapter
//              our adapter object
//
//      IN  Urb
//              전송할 URB
//
//  Return Value:
//      NT status code.
//
NTSTATUS SaitSubmitUrbSynch(
    IN  PSAIT_ADAPTER            Adapter,
    IN  PURB                        Urb
    )
{
    NTSTATUS            status;
    PIRP                irp;
    IO_STATUS_BLOCK     ioStatus;
    KEVENT              event;
    PIO_STACK_LOCATION  irpStack;

    SaitDebugPrint(DBG_IO, DBG_INFO, __FUNCTION__"++");

    KeInitializeEvent(&event, NotificationEvent, FALSE);

	// IRP_MJ_DEVICE_CONTROL irp 패킷을 하나 생성한다.
	// 이함수를 이용한 함수는 동기 처리 되어야 한다.
    irp = IoBuildDeviceIoControlRequest(
            IOCTL_INTERNAL_USB_SUBMIT_URB,
            Adapter->LowerDeviceObject,
            NULL,
            0,
            NULL,
            0,
            TRUE,
            &event,
            &ioStatus
            );

    if (irp != NULL)
    {
        irpStack = IoGetNextIrpStackLocation(irp);
        irpStack->Parameters.Others.Argument1 = Urb;

        status = IoCallDriver(Adapter->LowerDeviceObject, irp);
        if (status == STATUS_PENDING)
        {
            KeWaitForSingleObject(
                &event,
                Executive,
                KernelMode,
                FALSE,
                NULL
                );
            
            status = ioStatus.Status;    
        }
    }
    else
    {
        status = STATUS_INSUFFICIENT_RESOURCES;
    }

    SaitDebugPrint(DBG_IO, DBG_INFO, __FUNCTION__"--. STATUS %x", status);

    return status;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
//  SaitResetPipe
//      Pipe를 Reset 한다.
//
//  Arguments:
//      IN  Adapter
//              our adapter object
//
//      IN  PipeHandle
//              Handle to a pipe, which needs to be reset
//
//  Return Value:
//      NT status code.
//
NTSTATUS SaitResetPipe(
    IN  PSAIT_ADAPTER            Adapter,
    IN  USBD_PIPE_HANDLE                    PipeHandle
    )
{
    NTSTATUS                    status;
    struct _URB_PIPE_REQUEST    urb;

    SaitDebugPrint(DBG_IO, DBG_INFO, __FUNCTION__"++");

    urb.Hdr.Length = (USHORT)sizeof(struct _URB_PIPE_REQUEST);
    urb.Hdr.Function = URB_FUNCTION_RESET_PIPE;
    urb.PipeHandle = PipeHandle;

    status = SaitSubmitUrbSynch(Adapter, (PURB)&urb);

    SaitDebugPrint(DBG_IO, DBG_INFO, __FUNCTION__"--. STATUS %x", status);

    return status;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
//  SaitAbortPipe
//      pipe에 전송 대기중인 모든 패킷을 종료한다.
//
//  Arguments:
//      IN  Adapter
//              our adapter object
//
//      IN  PipeHandle
//              Handle to a pipe, which needs to be aborted
//
//  Return Value:
//      NT status code.
//
NTSTATUS SaitAbortPipe(
    IN  PSAIT_ADAPTER            Adapter,
    IN  USBD_PIPE_HANDLE                    PipeHandle
    )
{
    NTSTATUS                    status;
    struct _URB_PIPE_REQUEST    urb;

    SaitDebugPrint(DBG_IO, DBG_INFO, __FUNCTION__"++");

    urb.Hdr.Length = (USHORT)sizeof(struct _URB_PIPE_REQUEST);
    urb.Hdr.Function = URB_FUNCTION_ABORT_PIPE;
    urb.PipeHandle = PipeHandle;

    status = SaitSubmitUrbSynch(Adapter, (PURB)&urb);

    SaitDebugPrint(DBG_IO, DBG_INFO, __FUNCTION__"--. STATUS %x", status);

    return status;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
//  SaitResetDevice
//      Resets the port associated with the PDO.
//
//  Arguments:
//      IN  Adapter
//              our adapter object
//
//  Return Value:
//      NT status code.
//
NTSTATUS SaitResetDevice(
    IN  PSAIT_ADAPTER            Adapter
    )
{
    NTSTATUS            status;
    PIRP                irp;
    KEVENT              event;
    IO_STATUS_BLOCK     ioStatus;

    SaitDebugPrint(DBG_IO, DBG_INFO, __FUNCTION__"++");

    KeInitializeEvent(&event, NotificationEvent, FALSE);

    irp = IoBuildDeviceIoControlRequest(
            IOCTL_INTERNAL_USB_RESET_PORT,
            Adapter->LowerDeviceObject,
            NULL,
            0,
            NULL,
            0,
            TRUE,
            &event,
            &ioStatus
            );

    if(irp != NULL) 
    {
        status = IoCallDriver(Adapter->LowerDeviceObject, irp);
        if (status == STATUS_PENDING)
        {
            KeWaitForSingleObject(&event, Executive, KernelMode, FALSE, NULL);
            status = ioStatus.Status;
        }
    }
    else
    {
        status = STATUS_INSUFFICIENT_RESOURCES;
    }

    SaitDebugPrint(DBG_IO, DBG_INFO, __FUNCTION__"--. STATUS %x", status);

    return status;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
//  SaitSelectAlternateInterface
//      동일 Configuration 구조체 안에 Alternate Interface를 서택하는 루틴.
//
//  Arguments:
//      IN  Adapter
//              our adapter object
//
//      IN  AlternateSetting
//              interface number
//
//  Return Value:
//      NT status code.
//
NTSTATUS SaitSelectAlternateInterface(
    IN  PSAIT_ADAPTER            Adapter,
    IN  ULONG                               AlternateSetting
    )
{
    NTSTATUS                    status;
    PUSB_INTERFACE_DESCRIPTOR   interfaceDescriptor;
    PURB                        urb;
    ULONG                       urbSize;
    PUSBD_INTERFACE_INFORMATION interfaceInfo;
    ULONG                       index;

    SaitDebugPrint(DBG_IO, DBG_INFO, __FUNCTION__"++");

    // Configuration Descriptor에서 새로운 Interface 구조체를 찾는다.
    interfaceDescriptor = USBD_ParseConfigurationDescriptorEx(
                                Adapter->ConfigDescriptor,
                                Adapter->ConfigDescriptor,
                                0,
                                AlternateSetting,
                                -1,
                                -1,
                                -1
                                );


    if (interfaceDescriptor != NULL)
    {
        // allocate select interface URB
        urbSize = GET_SELECT_INTERFACE_REQUEST_SIZE(
                        interfaceDescriptor->bNumEndpoints
                        );

        urb = (PURB)ExAllocatePoolWithTag(
                        NonPagedPool,
                        urbSize,
                        SAIT_POOL_TAG
                        );

        if (urb != NULL)
        {
            RtlZeroMemory(urb, urbSize);

            UsbBuildSelectInterfaceRequest(
                urb,
                (USHORT)urbSize,
                Adapter->ConfigHandle,
                0,
                (UCHAR)AlternateSetting
                );

            interfaceInfo = &urb->UrbSelectInterface.Interface;
            interfaceInfo->Length = 
                GET_USBD_INTERFACE_SIZE(interfaceDescriptor->bNumEndpoints);

            // MaximumTransferSize를 적당한 값으로 초기화 한다.
			// 일반적으로 PageSize.
            for (index = 0; index < interfaceDescriptor->bNumEndpoints; ++index)
            {
                interfaceInfo->Pipes[index].MaximumTransferSize = USBD_DEFAULT_MAXIMUM_TRANSFER_SIZE;
            }

            status = SaitSubmitUrbSynch(Adapter, urb);
            if (NT_SUCCESS(status))
            {
                // 새로운 Interface 정보를 Adapter 구조체에 저장한다.
                ExFreePool(Adapter->InterfaceInformation);

                Adapter->InterfaceInformation = 
                    (PUSBD_INTERFACE_INFORMATION)ExAllocatePoolWithTag(
                                                    NonPagedPool, 
                                                    interfaceInfo->Length, 
                                                    SAIT_POOL_TAG
                                                    );

                if (Adapter->InterfaceInformation != NULL)
                {
                    RtlCopyMemory(
                        Adapter->InterfaceInformation, 
                        interfaceInfo, 
                        interfaceInfo->Length
                        );
                }
                else
                {
                    status = STATUS_INSUFFICIENT_RESOURCES;
                }
            }

            ExFreePool(urb);
        }
        else
        {
            status = STATUS_INSUFFICIENT_RESOURCES;
        }
    }
    else
    {
        status = STATUS_DEVICE_CONFIGURATION_ERROR;
    }


    SaitDebugPrint(DBG_IO, DBG_INFO, __FUNCTION__"--. STATUS %x", status);

    return status;
}

