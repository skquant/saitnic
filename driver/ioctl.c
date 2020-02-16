#include "pch.h"
#include "../intrface.h"
//
// Simple Mutual Exclusion constructs used in preference to
// using KeXXX calls since we don't have Mutex calls in NDIS.
// These can only be called at passive IRQL.
//

typedef struct _NIC_MUTEX
{
    ULONG                   Counter;
    ULONG                   ModuleAndLine;  // useful for debugging

} NIC_MUTEX, *PNIC_MUTEX;

#define NIC_INIT_MUTEX(_pMutex)                                 \
{                                                               \
    (_pMutex)->Counter = 0;                                     \
    (_pMutex)->ModuleAndLine = 0;                               \
}

#define NIC_ACQUIRE_MUTEX(_pMutex)                              \
{                                                               \
    while (NdisInterlockedIncrement((PLONG)&((_pMutex)->Counter)) != 1)\
    {                                                           \
        NdisInterlockedDecrement((PLONG)&((_pMutex)->Counter));        \
        NdisMSleep(10000);                                      \
    }                                                           \
    (_pMutex)->ModuleAndLine = ('I' << 16) | __LINE__;\
}

#define NIC_RELEASE_MUTEX(_pMutex)                              \
{                                                               \
    (_pMutex)->ModuleAndLine = 0;                               \
    NdisInterlockedDecrement((PLONG)&(_pMutex)->Counter);              \
}

#define LINKNAME_STRING     L"\\DosDevices\\NETVMINI"
#define NTDEVICE_STRING     L"\\Device\\NETVMINI"

//
// Global variables
//

NDIS_HANDLE        NdisDeviceHandle = NULL; // From NdisMRegisterDevice
LONG               MiniportCount = 0; // Total number of miniports in existance
PDEVICE_OBJECT     ControlDeviceObject = NULL;  // Device for IOCTLs
NIC_MUTEX          ControlDeviceMutex;
extern NDIS_HANDLE wrapperHandle;
extern PSAIT_ADAPTER 				gdeviceExtension;

#pragma NDIS_PAGEABLE_FUNCTION(NICRegisterDevice)
#pragma NDIS_PAGEABLE_FUNCTION(NICDeregisterDevice)
#pragma NDIS_PAGEABLE_FUNCTION(NICDispatch)


NDIS_STATUS
NICRegisterDevice(
    VOID
    )
/*++

Routine Description:

    Register an ioctl interface - a device object to be used for this
    purpose is created by NDIS when we call NdisMRegisterDevice.

    This routine is called whenever a new miniport instance is
    initialized. However, we only create one global device object,
    when the first miniport instance is initialized. This routine
    handles potential race conditions with NICDeregisterDevice via
    the ControlDeviceMutex.

    NOTE: do not call this from DriverEntry; it will prevent the driver
    from being unloaded (e.g. on uninstall).

Arguments:

    None

Return Value:

    NDIS_STATUS_SUCCESS if we successfully register a device object.

--*/
{
    NDIS_STATUS         Status = NDIS_STATUS_SUCCESS;
    UNICODE_STRING      DeviceName;
    UNICODE_STRING      DeviceLinkUnicodeString;
    PDRIVER_DISPATCH    DispatchTable[IRP_MJ_MAXIMUM_FUNCTION+1];

    SaitDebugPrint(DBG_IO, DBG_MYTRACE, __FUNCTION__"==>NICRegisterDevice\n");

    NIC_ACQUIRE_MUTEX(&ControlDeviceMutex);

    ++MiniportCount;
    
    if (1 == MiniportCount)
    {
        NdisZeroMemory(DispatchTable, (IRP_MJ_MAXIMUM_FUNCTION+1) * sizeof(PDRIVER_DISPATCH));
        
        DispatchTable[IRP_MJ_CREATE] = NICDispatch;
        DispatchTable[IRP_MJ_CLEANUP] = NICDispatch;
        DispatchTable[IRP_MJ_CLOSE] = NICDispatch;
        DispatchTable[IRP_MJ_DEVICE_CONTROL] = NICDispatch;
        

        NdisInitUnicodeString(&DeviceName, NTDEVICE_STRING);
        NdisInitUnicodeString(&DeviceLinkUnicodeString, LINKNAME_STRING);

        //
        // Create a device object and register our dispatch handlers
        //
        Status = NdisMRegisterDevice(
                    wrapperHandle, 
                    &DeviceName,
                    &DeviceLinkUnicodeString,
                    &DispatchTable[0],
                    &ControlDeviceObject,
                    &NdisDeviceHandle
                    );
    }

    NIC_RELEASE_MUTEX(&ControlDeviceMutex);

    SaitDebugPrint(DBG_IO, DBG_MYTRACE, __FUNCTION__"<==NICRegisterDevice: %x\n", Status);

    return (Status);
}

NTSTATUS
BulkUsb_CallUSBD1(
    IN PDEVICE_OBJECT DeviceObject,
    IN PURB Urb
    )
/*++

Routine Description:

    Passes a URB to the USBD class driver
	The client device driver passes USB request block (URB) structures 
	to the class driver as a parameter in an IRP with Irp->MajorFunction
	set to IRP_MJ_INTERNAL_DEVICE_CONTROL and the next IRP stack location 
	Parameters.DeviceIoControl.IoControlCode field set to 
	IOCTL_INTERNAL_USB_SUBMIT_URB. 

Arguments:

    DeviceObject - pointer to the physical device object (PDO)

    Urb - pointer to an already-formatted Urb request block

Return Value:

    STATUS_SUCCESS if successful,
    STATUS_UNSUCCESSFUL otherwise

--*/
{
    NTSTATUS ntStatus, status = STATUS_SUCCESS;
    PSAIT_ADAPTER deviceExtension;
    PIRP irp;
    KEVENT event;
    IO_STATUS_BLOCK ioStatus;
    PIO_STACK_LOCATION nextStack;

    SaitDebugPrint(DBG_IO, DBG_MYTRACE, __FUNCTION__"enter BulkUsb_CallUSBD\n");

    deviceExtension = gdeviceExtension;//DeviceObject->DeviceExtension;

    //
    // issue a synchronous request
    //

    KeInitializeEvent(&event, NotificationEvent, FALSE);

    irp = IoBuildDeviceIoControlRequest(
                IOCTL_INTERNAL_USB_SUBMIT_URB,
                deviceExtension->LowerDeviceObject, //Points to the next-lower driver's device object
                NULL, // optional input bufer; none needed here
                0,	  // input buffer len if used
                NULL, // optional output bufer; none needed here
                0,    // output buffer len if used
                TRUE, // If InternalDeviceControl is TRUE the target driver's Dispatch
				      //  outine for IRP_MJ_INTERNAL_DEVICE_CONTROL or IRP_MJ_SCSI 
					  // is called; otherwise, the Dispatch routine for 
					  // IRP_MJ_DEVICE_CONTROL is called.
                &event,     // event to be signalled on completion
                &ioStatus);  // Specifies an I/O status block to be set when the request is completed the lower driver. 

    //
    // As an alternative, we could call KeDelayExecutionThread, wait for some
    // period of time, and try again....but we keep it simple for right now
    //
    if (!irp) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    //
    // Call the class driver to perform the operation.  If the returned status
    // is PENDING, wait for the request to complete.
    //

    nextStack = IoGetNextIrpStackLocation(irp);
    ASSERT(nextStack != NULL);

    //
    // pass the URB to the USB driver stack
    //
    nextStack->Parameters.Others.Argument1 = Urb;

    ntStatus = IoCallDriver(deviceExtension->LowerDeviceObject, irp);

    SaitDebugPrint(DBG_IO, DBG_MYTRACE, __FUNCTION__"BulkUsb_CallUSBD() return from IoCallDriver USBD %x\n", ntStatus);

    if (ntStatus == STATUS_PENDING) {

        status = KeWaitForSingleObject(
                       &event,
                       Suspended,
                       KernelMode,
                       FALSE,
                       NULL);

    } else {
        ioStatus.Status = ntStatus;
    }

    SaitDebugPrint(DBG_IO, DBG_MYTRACE, __FUNCTION__"BulkUsb_CallUSBD() URB status = %x status = %x irp status %x\n",
        Urb->UrbHeader.Status, status, ioStatus.Status);

    //
    // USBD maps the error code for us
    //
    ntStatus = ioStatus.Status;

    SaitDebugPrint(DBG_IO, DBG_MYTRACE, __FUNCTION__"exit BulkUsb_CallUSBD FAILED (%x)\n", ntStatus);

    return ntStatus;
}
NTSTATUS
BulkUsb_Sendcmd1(
    IN PDEVICE_OBJECT DeviceObject,								
	IN PIRP Irp	
	)

{
	/***********************************************************
		Local variables
	***********************************************************/
    PIO_STACK_LOCATION 	irpStack, nextStack;
    PVOID 				ioBuffer;
    ULONG 				inputBufferLength = 3;
    ULONG 				outputBufferLength;
    PSAIT_ADAPTER 	deviceExtension;
    NTSTATUS 			ntStatus;
    PURB 				urb;
    unsigned short		siz;

	deviceExtension = gdeviceExtension;//DeviceObject->DeviceExtension;
	irpStack = IoGetCurrentIrpStackLocation ( Irp );
    ioBuffer = Irp->AssociatedIrp.SystemBuffer;
	inputBufferLength = irpStack->Parameters.DeviceIoControl.InputBufferLength;
//	outputBufferLength = irpStack->Parameters.DeviceIoControl.OutputBufferLength;

	if(inputBufferLength != 3)
	{
		ntStatus = STATUS_INVALID_PARAMETER;
		Irp->IoStatus.Information = 0;                
	    Irp->IoStatus.Status = ntStatus;
		return ntStatus;
	}

	siz = sizeof(struct _URB_CONTROL_VENDOR_OR_CLASS_REQUEST );
	urb = ExAllocatePool(NonPagedPool, siz);
    if(urb == NULL)
	{
		ntStatus = STATUS_INSUFFICIENT_RESOURCES;
		Irp->IoStatus.Information = 0;                
	    Irp->IoStatus.Status = ntStatus;
		return ntStatus;
	}
	
	RtlZeroMemory( urb,	siz );
	UsbBuildVendorRequest (
                        urb,
                        URB_FUNCTION_VENDOR_DEVICE, //URB_FUNCTION_CLASS_ENDPOINT,
						siz,
					//	USBD_TRANSFER_DIRECTION_IN |USBD_SHORT_TRANSFER_OK,
						0,
						0,
						0xb3,
						0,
						0,
						ioBuffer,
						NULL,
						inputBufferLength,
						NULL,
						);
		
	ntStatus = BulkUsb_CallUSBD1 (DeviceObject,	urb	);

    ExFreePool( urb );
	Irp->IoStatus.Information = 0;                
    Irp->IoStatus.Status = ntStatus;

	SaitDebugPrint(DBG_IO, DBG_MYTRACE, __FUNCTION__"Sendcmd()= %x\n",ntStatus);
	return ntStatus;
}
NTSTATUS
BulkUsb_CallUSBD2(
    IN PDEVICE_OBJECT DeviceObject,
    IN PURB Urb
    )
/*++

Routine Description:

    Passes a URB to the USBD class driver
	The client device driver passes USB request block (URB) structures 
	to the class driver as a parameter in an IRP with Irp->MajorFunction
	set to IRP_MJ_INTERNAL_DEVICE_CONTROL and the next IRP stack location 
	Parameters.DeviceIoControl.IoControlCode field set to 
	IOCTL_INTERNAL_USB_SUBMIT_URB. 

Arguments:

    DeviceObject - pointer to the physical device object (PDO)

    Urb - pointer to an already-formatted Urb request block

Return Value:

    STATUS_SUCCESS if successful,
    STATUS_UNSUCCESSFUL otherwise

--*/
{
    NTSTATUS ntStatus, status = STATUS_SUCCESS;
    PSAIT_ADAPTER deviceExtension;
    PIRP irp;
    KEVENT event;
    IO_STATUS_BLOCK ioStatus;
    PIO_STACK_LOCATION nextStack;

    SaitDebugPrint(DBG_IO, DBG_MYTRACE, __FUNCTION__"enter BulkUsb_CallUSBD\n");

    deviceExtension = gdeviceExtension;//DeviceObject->DeviceExtension;

    //
    // issue a synchronous request
    //

    KeInitializeEvent(&event, NotificationEvent, FALSE);

    irp = IoBuildDeviceIoControlRequest(
                IOCTL_INTERNAL_USB_SUBMIT_URB,
                deviceExtension->LowerDeviceObject, //Points to the next-lower driver's device object
                NULL, // optional input bufer; none needed here
                0,	  // input buffer len if used
                NULL, // optional output bufer; none needed here
                0,    // output buffer len if used
                TRUE, // If InternalDeviceControl is TRUE the target driver's Dispatch
				      //  outine for IRP_MJ_INTERNAL_DEVICE_CONTROL or IRP_MJ_SCSI 
					  // is called; otherwise, the Dispatch routine for 
					  // IRP_MJ_DEVICE_CONTROL is called.
                &event,     // event to be signalled on completion
                &ioStatus);  // Specifies an I/O status block to be set when the request is completed the lower driver. 

    //
    // As an alternative, we could call KeDelayExecutionThread, wait for some
    // period of time, and try again....but we keep it simple for right now
    //
    if (!irp) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    //
    // Call the class driver to perform the operation.  If the returned status
    // is PENDING, wait for the request to complete.
    //

    nextStack = IoGetNextIrpStackLocation(irp);
    ASSERT(nextStack != NULL);

    //
    // pass the URB to the USB driver stack
    //
    nextStack->Parameters.Others.Argument1 = Urb;

    ntStatus = IoCallDriver(deviceExtension->LowerDeviceObject, irp);

    SaitDebugPrint(DBG_IO, DBG_MYTRACE, __FUNCTION__"BulkUsb_CallUSBD() return from IoCallDriver USBD %x\n", ntStatus);

    if (ntStatus == STATUS_PENDING) {

        status = KeWaitForSingleObject(
                       &event,
                       Suspended,
                       KernelMode,
                       FALSE,
                       NULL);

    } else {
        ioStatus.Status = ntStatus;
    }

    SaitDebugPrint(DBG_IO, DBG_MYTRACE, __FUNCTION__"BulkUsb_CallUSBD() URB status = %x status = %x irp status %x\n",
        Urb->UrbHeader.Status, status, ioStatus.Status);

    //
    // USBD maps the error code for us
    //
    ntStatus = ioStatus.Status;

    SaitDebugPrint(DBG_IO, DBG_MYTRACE, __FUNCTION__"exit BulkUsb_CallUSBD FAILED (%x)\n", ntStatus);

    return ntStatus;
}

NTSTATUS
BulkUsb_Sendcmd2(
    IN PDEVICE_OBJECT DeviceObject,								
	IN PIRP Irp	
	)

{
	/***********************************************************
		Local variables
	***********************************************************/
    PIO_STACK_LOCATION 	irpStack, nextStack;
    PVOID 				ioBuffer;
    ULONG 				inputBufferLength = 3;
    ULONG 				outputBufferLength;
    PSAIT_ADAPTER 	deviceExtension;
    NTSTATUS 			ntStatus;
    PURB 				urb;
    unsigned short		siz;

	deviceExtension = gdeviceExtension;//DeviceObject->DeviceExtension;
	irpStack = IoGetCurrentIrpStackLocation ( Irp );
    ioBuffer = Irp->AssociatedIrp.SystemBuffer;
	inputBufferLength = irpStack->Parameters.DeviceIoControl.InputBufferLength;
//	outputBufferLength = irpStack->Parameters.DeviceIoControl.OutputBufferLength;

	if(inputBufferLength != 4)
	{
		ntStatus = STATUS_INVALID_PARAMETER;
		Irp->IoStatus.Information = 0;                
	    Irp->IoStatus.Status = ntStatus;
		return ntStatus;
	}

	siz = sizeof(struct _URB_CONTROL_VENDOR_OR_CLASS_REQUEST );
	urb = ExAllocatePool(NonPagedPool, siz);
    if(urb == NULL)
	{
		ntStatus = STATUS_INSUFFICIENT_RESOURCES;
		Irp->IoStatus.Information = 0;                
	    Irp->IoStatus.Status = ntStatus;
		return ntStatus;
	}
	
	RtlZeroMemory( urb,	siz );
	UsbBuildVendorRequest (
                        urb,
                        URB_FUNCTION_VENDOR_DEVICE, //URB_FUNCTION_CLASS_ENDPOINT,
						siz,
					//	USBD_TRANSFER_DIRECTION_IN |USBD_SHORT_TRANSFER_OK,
						0,
						0,
						0xb4,
						0xa0,
						0,
						ioBuffer,
						NULL,
						inputBufferLength,
						NULL,
						);
		
	ntStatus = BulkUsb_CallUSBD2 (DeviceObject,	urb	);

    ExFreePool( urb );
	Irp->IoStatus.Information = 0;                
    Irp->IoStatus.Status = ntStatus;

	SaitDebugPrint(DBG_IO, DBG_MYTRACE, __FUNCTION__"Sendcmd()= %x\n",ntStatus);
	return ntStatus;
}

NTSTATUS
NICDispatch(
    IN PDEVICE_OBJECT           DeviceObject,
    IN PIRP                     Irp
    )
/*++
Routine Description:

    Process IRPs sent to this device.

Arguments:

    DeviceObject - pointer to a device object
    Irp      - pointer to an I/O Request Packet

Return Value:

    NTSTATUS - STATUS_SUCCESS always - change this when adding
    real code to handle ioctls.

--*/
{
    PIO_STACK_LOCATION  irpStack;
    NTSTATUS            status = STATUS_SUCCESS;
    ULONG               inlen;
    PVOID               buffer;

    irpStack = IoGetCurrentIrpStackLocation(Irp);
    SaitDebugPrint(DBG_IO, DBG_MYTRACE, __FUNCTION__"==>NICDispatch %d\n", irpStack->MajorFunction);
      
    switch (irpStack->MajorFunction)
    {
        case IRP_MJ_CREATE:
            break;
        
        case IRP_MJ_CLEANUP:
            break;
        
        case IRP_MJ_CLOSE:
            break;        
        
        case IRP_MJ_DEVICE_CONTROL: 
        {

          buffer = Irp->AssociatedIrp.SystemBuffer;  
          inlen = irpStack->Parameters.DeviceIoControl.InputBufferLength;
          
          switch (irpStack->Parameters.DeviceIoControl.IoControlCode) 
          {

            //
            // Add code here to handle ioctl commands.
            //
			case IOCTL_SENDCMD1:
				status = BulkUsb_Sendcmd1( DeviceObject ,Irp);
				IoCompleteRequest(Irp, IO_NO_INCREMENT);
     			return status;

			case IOCTL_SENDCMD2:
				status = BulkUsb_Sendcmd2( DeviceObject ,Irp);
				IoCompleteRequest(Irp, IO_NO_INCREMENT);
     			return status;

            case IOCTL_NETVMINI_READ_DATA:
                SaitDebugPrint(DBG_IO, DBG_MYTRACE, __FUNCTION__"Received Read IOCTL\n");
                break;
            case IOCTL_NETVMINI_WRITE_DATA:
                SaitDebugPrint(DBG_IO, DBG_MYTRACE, __FUNCTION__"Received Write IOCTL\n");
                break;
            default:
                status = STATUS_UNSUCCESSFUL;
                break;
          }
          break;  
        }
        default:
            break;
    }
    Irp->IoStatus.Information = 0;
    Irp->IoStatus.Status = status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);

    SaitDebugPrint(DBG_IO, DBG_MYTRACE, __FUNCTION__"<== NIC Dispatch\n");

    return status;

} 


NDIS_STATUS
NICDeregisterDevice(
    VOID
    )
/*++

Routine Description:

    Deregister the ioctl interface. This is called whenever a miniport
    instance is halted. When the last miniport instance is halted, we
    request NDIS to delete the device object

Arguments:

    NdisDeviceHandle - Handle returned by NdisMRegisterDevice

Return Value:

    NDIS_STATUS_SUCCESS if everything worked ok

--*/
{
    NDIS_STATUS Status = NDIS_STATUS_SUCCESS;

    SaitDebugPrint(DBG_IO, DBG_MYTRACE, __FUNCTION__"==>NICDeregisterDevice\n");

    NIC_ACQUIRE_MUTEX(&ControlDeviceMutex);

    ASSERT(MiniportCount > 0);

    --MiniportCount;
    
    if (0 == MiniportCount)
    {
        //
        // All miniport instances have been halted.
        // Deregister the control device.
        //

        if (NdisDeviceHandle != NULL)
        {
            Status = NdisMDeregisterDevice(NdisDeviceHandle);
            NdisDeviceHandle = NULL;
        }
    }

    NIC_RELEASE_MUTEX(&ControlDeviceMutex);

    SaitDebugPrint(DBG_IO, DBG_MYTRACE, __FUNCTION__"<== NICDeregisterDevice: %x\n", Status);
    return Status;
    
}



