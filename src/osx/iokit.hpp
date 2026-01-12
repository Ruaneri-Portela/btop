#pragma once
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/hidsystem/IOHIDEventSystemClient.h>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#if __MAC_OS_X_VERSION_MIN_REQUIRED < 120000
#define kIOMainPortDefault kIOMasterPortDefault
#endif

using IOServiceCallback = bool (*)(io_object_t object, void *data);

std::optional<std::string> SafeCFStringToStdString(CFStringRef strRef);

std::optional<int64_t> SafeCFNumberToInt64(CFNumberRef numberRef);

std::optional<std::vector<uint8_t>> SafeCFDataToRawVector(CFDataRef dataRef);

std::optional<bool> SafeCFBooleanToBool(CFBooleanRef boolRef);

std::optional<std::string>
SafeIOServiceGetStringFromDictionary(CFDictionaryRef dictionary,
                                     CFStringRef key);

std::optional<int64_t>
SafeIOServiceGetNumberFromDictionary(CFDictionaryRef dictionary,
                                     CFStringRef key);

std::optional<std::vector<uint8_t>>
SafeIOServiceGetDataVectorFromDictionary(CFDictionaryRef dictionary,
                                         CFStringRef key);

std::optional<bool> SafeIOServiceBoolFromDictionary(CFDictionaryRef dictionary,
                                                    CFStringRef key);

io_registry_entry_t IOServiceGetParent(io_registry_entry_t entry,
                                       const io_name_t plane);

bool IOServiceGenericIterator(const std::string &className,
                              IOServiceCallback callback, void *data);

bool IOServiceGenericChildrenIterator(io_object_t parent, const io_name_t plane,
                                      IOServiceCallback callback, void *data);

namespace IOReport {
using IOReportSubscriptionRef = CFTypeRef;
extern void *LibHandle;
extern bool TryLoaded;

extern CFDictionaryRef (*CopyChannelsInGroup)(CFStringRef, CFStringRef,
                                              uint64_t, uint64_t, uint64_t);

extern void (*MergeChannels)(CFDictionaryRef, CFDictionaryRef, CFTypeRef);

extern IOReportSubscriptionRef (*CreateSubscription)(void *,
                                                     CFMutableDictionaryRef,
                                                     CFMutableDictionaryRef *,
                                                     uint64_t, CFTypeRef);

extern CFDictionaryRef (*CreateSamples)(IOReportSubscriptionRef,
                                        CFMutableDictionaryRef, CFTypeRef);

extern CFDictionaryRef (*CreateSamplesDelta)(CFDictionaryRef, CFDictionaryRef,
                                             CFTypeRef);

extern CFStringRef (*ChannelGetGroup)(CFDictionaryRef);
extern CFStringRef (*ChannelGetSubGroup)(CFDictionaryRef);
extern CFStringRef (*ChannelGetChannelName)(CFDictionaryRef);
extern CFStringRef (*ChannelGetUnitLabel)(CFDictionaryRef);
extern CFStringRef (*ChannelGetDriverName)(CFDictionaryRef);

extern int32_t (*StateGetCount)(CFDictionaryRef);
extern CFStringRef (*StateGetNameForIndex)(CFDictionaryRef, int32_t);
extern int64_t (*StateGetResidency)(CFDictionaryRef, int32_t);
extern int64_t (*SimpleGetIntegerValue)(CFDictionaryRef, int32_t);

void TryLoad();
} // namespace IOReport

//? IOHIDSensor declarations for GPU temperature
extern "C" {
typedef struct __IOHIDEvent *IOHIDEventRef;
typedef struct __IOHIDServiceClient *IOHIDServiceClientRef;
#ifdef __LP64__
typedef double IOHIDFloat;
#else
typedef float IOHIDFloat;
#endif

#define IOHIDEventFieldBase(type) (type << 16)
#define kIOHIDEventTypeTemperature 15

IOHIDEventSystemClientRef
IOHIDEventSystemClientCreate(CFAllocatorRef allocator);
int IOHIDEventSystemClientSetMatching(IOHIDEventSystemClientRef client,
                                      CFDictionaryRef match);
CFArrayRef IOHIDEventSystemClientCopyServices(IOHIDEventSystemClientRef);
IOHIDEventRef IOHIDServiceClientCopyEvent(IOHIDServiceClientRef, int64_t,
                                          int32_t, int64_t);
CFStringRef IOHIDServiceClientCopyProperty(IOHIDServiceClientRef service,
                                           CFStringRef property);
IOHIDFloat IOHIDEventGetFloatValue(IOHIDEventRef event, int32_t field);

CFDictionaryRef CreateHidMatching(int page, int usage);
}