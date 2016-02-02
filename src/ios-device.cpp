/**
 * node-ios-device
 * Copyright (c) 2013-2015 by Appcelerator, Inc. All Rights Reserved.
 * Licensed under the terms of the Apache Public License
 * Please see the LICENSE included with this distribution for details.
 */

#include <nan.h>
#include <node.h>
#include <v8.h>
#include <stdlib.h>
#include "mobiledevice.h"

using namespace v8;

/*
 * A struct to track listener properties such as the JavaScript callback
 * function.
 */
typedef struct Listener {
	Nan::Persistent<Function> callback;
} Listener;

/*
 * Globals
 */
static CFMutableDictionaryRef listeners;
static CFMutableDictionaryRef connected_devices;
static bool devices_changed;

/*
 * Converts CFStringRef strings to C strings.
 */
char* cfstring_to_cstr(CFStringRef str) {
	if (str != NULL) {
		// add 1 to make sure there's enough buffer for the utf-8 string and the null character
		CFIndex length = CFStringGetLength(str) + 1;
		CFIndex maxSize = CFStringGetMaximumSizeForEncoding(length, kCFStringEncodingUTF8);
		char* buffer = (char*)malloc(maxSize);
		if (CFStringGetCString(str, buffer, maxSize, kCFStringEncodingUTF8)) {
			return buffer;
		}
	}
	return NULL;
}

/*
 * Device object that persists while the device is plugged in. It contains the
 * original MobileDevice device reference and a V8 JavaScript object containing
 * the devices properties.
 */
class Device {
public:
	am_device device;
	Nan::Persistent<Object> props;
	bool connected;

	service_conn_t logConnection;
	CFSocketRef logSocket;
	CFRunLoopSourceRef logSource;
	Listener* logCallback;

	Device(am_device& dev) : device(dev), connected(false), logSocket(NULL), logSource(NULL), logCallback(NULL) {
		props.Reset(Nan::New<Object>());
	}

	// fetches info from the device and populates the JavaScript object
	void populate(CFStringRef udid) {
		Local<Object> p = Nan::New<Object>();

		char* str = cfstring_to_cstr(udid);
		if (str != NULL) {
			Nan::Set(p, Nan::New("udid").ToLocalChecked(), Nan::New(str).ToLocalChecked());
			free(str);
		}

		this->getProp(p, "name",            CFSTR("DeviceName"));
		this->getProp(p, "deviceClass",     CFSTR("DeviceClass"));
		this->getProp(p, "deviceColor",     CFSTR("DeviceColor"));
		this->getProp(p, "modelNumber",     CFSTR("ModelNumber"));
		this->getProp(p, "productVersion",  CFSTR("ProductVersion"));
		this->getProp(p, "serialNumber",    CFSTR("SerialNumber"));
		this->getProp(p, "imei",            CFSTR("InternationalMobileEquipmentIdentity"));
		this->getProp(p, "meid",            CFSTR("MobileEquipmentIdentifier"));

		props.Reset(p);
	}

private:
	void getProp(Local<Object>& p, const char* propName, CFStringRef name) {
		CFStringRef value = AMDeviceCopyValue(this->device, 0, name);
		if (value != NULL) {
			char* str = cfstring_to_cstr(value);
			CFRelease(value);
			if (str != NULL) {
				Nan::Set(p, Nan::New(propName).ToLocalChecked(), Nan::New(str).ToLocalChecked());
				free(str);
			}
		}
	}
};

/*
 * on()
 * Defines a JavaScript function that adds an event listener.
 */
NAN_METHOD(on) {
	if (info.Length() >= 2) {
		if (!info[0]->IsString()) {
			return Nan::ThrowError(Exception::Error(Nan::New("Argument \'event\' must be a string").ToLocalChecked()));
		}

		if (!info[1]->IsFunction()) {
			return Nan::ThrowError(Exception::Error(Nan::New("Argument \'callback\' must be a function").ToLocalChecked()));
		}

		Handle<String> event = Handle<String>::Cast(info[0]);
		String::Utf8Value str(event->ToString());
		CFStringRef eventName = CFStringCreateWithCString(NULL, (char*)*str, kCFStringEncodingUTF8);

		Listener* listener = new Listener;
		listener->callback.Reset(Local<Function>::Cast(info[1]));
		CFDictionarySetValue(listeners, eventName, listener);
	}

	info.GetReturnValue().SetUndefined();
}

/*
 * Notifies all listeners of an event.
 */
void emit(const char* event) {
	CFStringRef eventStr = CFStringCreateWithCStringNoCopy(NULL, event, kCFStringEncodingUTF8, NULL);
	CFIndex size = CFDictionaryGetCount(listeners);
	CFStringRef* keys = (CFStringRef*)malloc(size * sizeof(CFStringRef));
	CFDictionaryGetKeysAndValues(listeners, (const void **)keys, NULL);
	CFIndex i = 0;

	for (; i < size; i++) {
		if (CFStringCompare(keys[i], eventStr, 0) == kCFCompareEqualTo) {
			const Listener* listener = (const Listener*)CFDictionaryGetValue(listeners, keys[i]);
			if (listener != NULL) {
				Local<Function> callback = Nan::New<Function>(listener->callback);
				callback->Call(Nan::GetCurrentContext()->Global(), 0, NULL);
			}
		}
	}

	free(keys);
}

/*
 * pumpRunLoop()
 * Defines a JavaScript function that processes all pending notifications.
 */
NAN_METHOD(pump_run_loop) {
	CFTimeInterval interval = 1.0;

	if (info.Length() > 0 && info[0]->IsNumber()) {
		Local<Number> intervalArg = Local<Number>::Cast(info[0]);
		interval = intervalArg->NumberValue();
	}

	devices_changed = false;

	CFRunLoopRunInMode(kCFRunLoopDefaultMode, interval, false);

	if (devices_changed) {
		emit("devicesChanged");
	}

	info.GetReturnValue().SetUndefined();
}

/*
 * devices()
 * Defines a JavaScript function that returns a JavaScript array of iOS devices.
 * This should be called after pumpRunLoop() has been called.
 */
NAN_METHOD(devices) {
	Local<Array> result = Nan::New<Array>();

	CFIndex size = CFDictionaryGetCount(connected_devices);
	Device** values = (Device**)malloc(size * sizeof(Device*));
	CFDictionaryGetKeysAndValues(connected_devices, NULL, (const void **)values);

	for (CFIndex i = 0; i < size; i++) {
		Nan::Persistent<Object>* obj = &values[i]->props;
		Nan::Set(result, i, Nan::New<Object>(*obj));
	}

	free(values);

	info.GetReturnValue().Set(result);
}

/*
 * The callback when a device notification is received.
 */
void on_device_notification(am_device_notification_callback_info* info, void* arg) {
	CFStringRef udid;

	switch (info->msg) {
		case ADNCI_MSG_CONNECTED:
			udid = AMDeviceCopyDeviceIdentifier(info->dev);
			if (!CFDictionaryContainsKey(connected_devices, udid)) {
				// connect to the device and get its information
				if (AMDeviceConnect(info->dev) == MDERR_OK) {
					if (AMDeviceIsPaired(info->dev) != 1 && AMDevicePair(info->dev) != 1) {
						return;
					}

					if (AMDeviceValidatePairing(info->dev) != MDERR_OK) {
						if (AMDevicePair(info->dev) != 1) {
							return;
						}
						if (AMDeviceValidatePairing(info->dev) != MDERR_OK) {
							return;
						}
					}

					if (AMDeviceStartSession(info->dev) == MDERR_OK) {
						Device* device = new Device(info->dev);
						device->populate(udid);
						CFDictionarySetValue(connected_devices, udid, device);
						devices_changed = true;

						AMDeviceStopSession(info->dev);
					}

					AMDeviceDisconnect(info->dev);
				}
			}
			break;

		case ADNCI_MSG_DISCONNECTED:
			udid = AMDeviceCopyDeviceIdentifier(info->dev);
			if (CFDictionaryContainsKey(connected_devices, udid)) {
				// remove the device from the dictionary and destroy it
				Device* device = (Device*)CFDictionaryGetValue(connected_devices, udid);
				CFDictionaryRemoveValue(connected_devices, udid);

				if (device->logCallback) {
					delete device->logCallback;
				}
				if (device->logSource) {
					CFRelease(device->logSource);
				}
				if (device->logSocket) {
					CFRelease(device->logSocket);
				}

				delete device;
				devices_changed = true;
			}
			break;
	}
}

static void cleanup(void *arg) {
	// free up connected devices
	CFIndex size = CFDictionaryGetCount(connected_devices);
	CFStringRef* keys = (CFStringRef*)malloc(size * sizeof(CFStringRef));
	CFDictionaryGetKeysAndValues(connected_devices, (const void **)keys, NULL);
	CFIndex i = 0;

	for (; i < size; i++) {
		Device* device = (Device*)CFDictionaryGetValue(connected_devices, keys[i]);
		CFDictionaryRemoveValue(connected_devices, keys[i]);

		if (device->connected) {
			AMDeviceStopSession(device->device);
			AMDeviceDisconnect(device->device);
		}

		if (device->logCallback) {
			delete device->logCallback;
		}
		if (device->logSource) {
			CFRelease(device->logSource);
		}
		if (device->logSocket) {
			CFRelease(device->logSocket);
		}

		delete device;
	}

	free(keys);

	// free up listeners
	size = CFDictionaryGetCount(listeners);
	keys = (CFStringRef*)malloc(size * sizeof(CFStringRef));
	CFDictionaryGetKeysAndValues(listeners, (const void **)keys, NULL);
	i = 0;

	for (; i < size; i++) {
		CFDictionaryRemoveValue(listeners, keys[i]);
	}

	free(keys);
}

/*
 * Wire up the JavaScript functions, initialize the dictionaries, and subscribe
 * to the device notifications.
 */
void init(Handle<Object> exports) {
	exports->Set(Nan::New("on").ToLocalChecked(),          Nan::New<FunctionTemplate>(on)->GetFunction());
	exports->Set(Nan::New("pumpRunLoop").ToLocalChecked(), Nan::New<FunctionTemplate>(pump_run_loop)->GetFunction());
	exports->Set(Nan::New("devices").ToLocalChecked(),     Nan::New<FunctionTemplate>(devices)->GetFunction());

	listeners = CFDictionaryCreateMutable(NULL, 0, &kCFTypeDictionaryKeyCallBacks, NULL);
	connected_devices = CFDictionaryCreateMutable(NULL, 0, &kCFTypeDictionaryKeyCallBacks, NULL);

	am_device_notification notification;
	AMDeviceNotificationSubscribe(&on_device_notification, 0, 0, NULL, &notification);

	node::AtExit(cleanup);
}

#define ADDON_MODULE2(ver, fn) NODE_MODULE(node_ios_device_v ## ver, fn)
#define ADDON_MODULE(ver, fn) ADDON_MODULE2(ver, fn)

// in Node.js 0.8, NODE_MODULE_VERSION is (1) and the parenthesis mess things up
#if NODE_MODULE_VERSION > 1
	ADDON_MODULE(NODE_MODULE_VERSION, init)
#else
	ADDON_MODULE(1, init)
#endif
