/* ex:set ts=4 sw=4: <- for vi
 *
 * IPod.h
 *
 * This is based on information extracted from around the Internet, by people
 * who wanted to look a lot further under the covers than I did.  I've deliberately
 * pushed all Apple's datatypes back to being opaque, since there is nothing
 * to be gained by looking inside them, and everything to lose.
 *
 * This file doesn't contain any of the 'recovery-mode' or other 'jailbreak-related' stuff
 *
 * http://iphonesvn.halifrag.com/svn/iPhone/
 * http://www.theiphonewiki.com/wiki/index.php?title=MobileDevice_Library#MobileDevice_Header_.28mobiledevice.h.29
 * http://code.google.com/p/iphonebrowser/issues/detail?id=52
 * http://www.koders.com/csharp/fidE79340F4674D47FFF3EFB6F949A1589D942798F3.aspx
 *
 * As far as I'm concerned, this header is in the Public Domain.
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/*
 * its critical that we spell out all integer sizes, for the entry points
 * and data structures in the external DLL/framework that we have no
 * control over.
 */
#include <stdint.h>

#define __PACK __attribute__((__packed__))


#define __DLLIMPORT
#include <CoreFoundation/CoreFoundation.h>
#include <mach/error.h>


/* Error codes */
#define MDERR_APPLE_MOBILE		(err_system(0x3a))
#define MDERR_IPHONE			(err_sub(0))

#define ERR_MOBILE_DEVICE 0

/* Apple Mobile (AM*) errors */
#define MDERR_OK                ERR_SUCCESS
#define MDERR_SYSCALL           (ERR_MOBILE_DEVICE | 0x01)
#define MDERR_OUT_OF_MEMORY     (ERR_MOBILE_DEVICE | 0x03)
#define MDERR_QUERY_FAILED      (ERR_MOBILE_DEVICE | 0x04)
#define MDERR_INVALID_ARGUMENT  (ERR_MOBILE_DEVICE | 0x0b)
#define MDERR_NO_SUCH_SERVICE   (ERR_MOBILE_DEVICE | 0x22)
#define MDERR_DICT_NOT_LOADED   (ERR_MOBILE_DEVICE | 0x25)

/* Apple File Connection (AFC*) errors */
#define MDERR_AFC_OUT_OF_MEMORY 0x03

/* USBMux errors */
#define MDERR_USBMUX_ARG_NULL   0x16
#define MDERR_USBMUX_FAILED     0xffffffff

#define AMD_IPHONE_PRODUCT_ID   0x1290

typedef uint32_t afc_error_t;
// typedef uint64_t afc_file_ref;
typedef uint32_t service_conn_t;

/* opaque structures */
typedef struct _am_device				*am_device;
typedef struct _am_service				*am_service;
typedef struct _afc_connection			*afc_connection;
typedef struct _am_device_notification	*am_device_notification;
typedef struct _afc_dictionary			*afc_dictionary;

/* Messages passed to device notification callbacks: passed as part of
 * am_device_notification_callback_info. */
typedef enum {
	ADNCI_MSG_CONNECTED     = 1,
	ADNCI_MSG_DISCONNECTED  = 2,
	ADNCI_MSG_UNKNOWN       = 3
} adnci_msg;

struct am_device_notification_callback_info {
	am_device	dev;				/* 0    device */
	uint32_t	msg;				/* 4    one of adnci_msg */
}  __PACK;

/* The type of the device notification callback function. */
typedef void (*am_device_notification_callback)(
	struct am_device_notification_callback_info *,
	void* callback_data);

/* ----------------------------------------------------------------------------
 *   Public routines
 * ------------------------------------------------------------------------- */

/* Registers a notification with the current run loop. The callback gets
 * copied into the notification struct, as well as being registered with the
 * current run loop. callback_data gets passed to the callback in addition
 * to the info block.
 * unused0 and unused1 are both 0 when iTunes calls this.
 *
 *  Returns:
 *      MDERR_OK            if successful
 *      MDERR_SYSCALL       if CFRunLoopAddSource() failed
 *      MDERR_OUT_OF_MEMORY if we ran out of memory
 */

mach_error_t AMDeviceNotificationSubscribe(
	am_device_notification_callback callback,
	uint32_t unused0,
	uint32_t unused1,
	void *callback_data,
	am_device_notification *notification);

/*
 * Unregisters notifications. Buggy (iTunes 8.2): if you subscribe, unsubscribe and subscribe again, arriving
           notifications will contain cookie and subscription from 1st call to subscribe, not the 2nd one. iTunes
           calls this function only once on exit.
        */
mach_error_t AMDeviceNotificationUnsubscribe(
	am_device_notification subscription);

/*  Connects to the iPhone. Pass in the am_device structure that the
 *  notification callback will give to you.
 *
 *  Returns:
 *      MDERR_OK                if successfully connected
 *      MDERR_SYSCALL           if setsockopt() failed
 *      MDERR_QUERY_FAILED      if the daemon query failed
 *      MDERR_INVALID_ARGUMENT  if USBMuxConnectByPort returned 0xffffffff
 */

mach_error_t AMDeviceConnect(
	am_device device);

/*  Calls PairingRecordPath() on the given device, than tests whether the path
 *  which that function returns exists. During the initial connect, the path
 *  returned by that function is '/', and so this returns 1.
 *
 *  Returns:
 *      0   if the path did not exist
 *      1   if it did
 */

mach_error_t AMDeviceIsPaired(
	am_device device);

mach_error_t AMDevicePair(am_device device);

/*  iTunes calls this function immediately after testing whether the device is
 *  paired. It creates a pairing file and establishes a Lockdown connection.
 *
 *  Returns:
 *      MDERR_OK                if successful
 *      MDERR_INVALID_ARGUMENT  if the supplied device is null
 *      MDERR_DICT_NOT_LOADED   if the load_dict() call failed
 */

mach_error_t AMDeviceValidatePairing(
	am_device device);

/*  Creates a Lockdown session and adjusts the device structure appropriately
 *  to indicate that the session has been started. iTunes calls this function
 *  after validating pairing.
 *
 *  Returns:
 *      MDERR_OK                if successful
 *      MDERR_INVALID_ARGUMENT  if the Lockdown conn has not been established
 *      MDERR_DICT_NOT_LOADED   if the load_dict() call failed
 */

mach_error_t AMDeviceStartSession(
	am_device device);

/* Starts a service and returns a handle that can be used in order to further
 * access the service. You should stop the session and disconnect before using
 * the service. iTunes calls this function after starting a session. It starts
 * the service and the SSL connection. unknown may safely be
 * NULL (it is when iTunes calls this), but if it is not, then it will be
 * filled upon function exit. service_name should be one of the AMSVC_*
 * constants. If the service is AFC (AMSVC_AFC), then the handle is the handle
 * that will be used for further AFC* calls.
 *
 * Returns:
 *      MDERR_OK                if successful
 *      MDERR_SYSCALL           if the setsockopt() call failed
 *      MDERR_INVALID_ARGUMENT  if the Lockdown conn has not been established
 */

mach_error_t AMDeviceStartService(
	am_device device,
	CFStringRef service_name,
	service_conn_t *handle,
	uint32_t *unknown);

/* Stops a session. You should do this before accessing services.
 *
 * Returns:
 *      MDERR_OK                if successful
 *      MDERR_INVALID_ARGUMENT  if the Lockdown conn has not been established
 */

mach_error_t AMDeviceStopSession(
	am_device device);

/* Retrieves an afc_dictionary that describes the connected device.  To
 * extract values from the dictionary, use AFCKeyValueRead() and close
 * it when finished with AFCKeyValueClose()
 */
afc_error_t AFCDeviceInfoOpen(
	afc_connection conn,
	afc_dictionary *info);

/* Turns debug mode on if the environment variable AFCDEBUG is set to a numeric
 * value, or if the file '/AFCDEBUG' is present and contains a value. */
void AFCPlatformInit();

/* ----------------------------------------------------------------------------
 *   Less-documented public routines
 * ------------------------------------------------------------------------- */

afc_error_t AFCKeyValueRead(
	afc_dictionary dict,
	char **key,
	char **val);

afc_error_t AFCKeyValueClose(
	afc_dictionary dict);

uint32_t AMDeviceGetConnectionID(am_device device);
mach_error_t AMDeviceDisconnect(am_device device);
mach_error_t AMDeviceRetain(am_device device);
mach_error_t AMDeviceRelease(am_device device);

uint32_t AMDeviceGetInterfaceType(
	am_device device); // { return 1; }

uint32_t AMDeviceGetInterfaceSpeed(
	am_device device); // { return 0x78000; }

/* Reads various device settings; returns nil if no value is found for
 * the nominated key
 *
 * Must be balanced by CFRelease()
 *
 * Possible values for key:
 * ActivationState
 * ActivationStateAcknowledged
 * BasebandBootloaderVersion
 * BasebandVersion
 * BluetoothAddress
 * BuildVersion
 * DeviceCertificate
 * DeviceClass
 * DeviceName
 * DevicePublicKey
 * FirmwareVersion
 * HostAttached
 * IntegratedCircuitCardIdentity
 * InternationalMobileEquipmentIdentity
 * InternationalMobileSubscriberIdentity
 * ModelNumber
 * PhoneNumber
 * ProductType
 * ProductVersion
 * ProtocolVersion
 * RegionInfo
 * SBLockdownEverRegisteredKey
 * SIMStatus
 * SerialNumber
 * SomebodySetTimeZone
 * TimeIntervalSince1970
 * TimeZone
 * TimeZoneOffsetFromUTC
 * TrustedHostAttached
 * UniqueDeviceID
 * Uses24HourClock
 * WiFiAddress
 * iTunesHasConnected
 */
CFStringRef AMDeviceCopyValue(
	am_device device,
	uint32_t mbz,
	CFStringRef key);

/*
 * Returns the udid of the specified device.  The same value is returned
 * from AMDeviceCopyValue(device,0,"UniqueDeviceID").
 *
 * Must be balanced by CFRelease()
 */
CFStringRef AMDeviceCopyDeviceIdentifier(
	am_device device);

mach_error_t AMDShutdownNotificationProxy(
	void *);

#ifdef __cplusplus
}
#endif
