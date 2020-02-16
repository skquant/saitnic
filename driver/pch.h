//////////////////////////////////////////////////////////////////////////////////////////////////
// pch.h
//

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus
#pragma warning(disable:4200)   // nonstandard extension used : zero-sized array in struct/union
#pragma warning(disable:4201)   // nonstandard extension used : nameless struct/union
#include <initguid.h>
#include <ndis.h>
#define NTSTRSAFE_LIB
#include <ntstrsafe.h>
#include <usbdi.h>
#include <usbdlib.h>
#ifndef WIN2K
#include <usbbusif.h>
#endif
#ifdef __cplusplus
}
#endif // __cplusplus
#include "Sait.h"
