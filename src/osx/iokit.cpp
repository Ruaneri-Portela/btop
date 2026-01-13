/* Copyright 2026 Ruaneri Portela (ruaneriportela@outlook.com)

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.

   indent = tab
   tab-size = 4
*/

#include "iokit.hpp"
#include <cstdint>
#include <dlfcn.h>
#include <string>

// IOReport non public objects

namespace IOReport {
void *LibHandle = nullptr;
bool TryLoaded = false;

CFDictionaryRef (*CopyChannelsInGroup)(CFStringRef, CFStringRef, uint64_t,
                                       uint64_t, uint64_t) = nullptr;

void (*MergeChannels)(CFDictionaryRef, CFDictionaryRef, CFTypeRef) = nullptr;

IOReportSubscriptionRef (*CreateSubscription)(void *, CFMutableDictionaryRef,
                                              CFMutableDictionaryRef *,
                                              uint64_t, CFTypeRef) = nullptr;

CFDictionaryRef (*CreateSamples)(IOReportSubscriptionRef,
                                 CFMutableDictionaryRef, CFTypeRef) = nullptr;

CFDictionaryRef (*CreateSamplesDelta)(CFDictionaryRef, CFDictionaryRef,
                                      CFTypeRef) = nullptr;

CFStringRef (*ChannelGetGroup)(CFDictionaryRef) = nullptr;
CFStringRef (*ChannelGetSubGroup)(CFDictionaryRef) = nullptr;
CFStringRef (*ChannelGetChannelName)(CFDictionaryRef) = nullptr;
CFStringRef (*ChannelGetUnitLabel)(CFDictionaryRef) = nullptr;
CFStringRef (*ChannelGetDriverName)(CFDictionaryRef) = nullptr;

int32_t (*StateGetCount)(CFDictionaryRef) = nullptr;
CFStringRef (*StateGetNameForIndex)(CFDictionaryRef, int32_t) = nullptr;
int64_t (*StateGetResidency)(CFDictionaryRef, int32_t) = nullptr;
int64_t (*SimpleGetIntegerValue)(CFDictionaryRef, int32_t) = nullptr;

void TryLoad() {
  if (!TryLoaded)
    TryLoaded = true;

  LibHandle = dlopen("/usr/lib/libIOReport.dylib", RTLD_NOW);
  if (LibHandle == nullptr) {
    return;
  }

//? Load all required functions
#define LOAD_FUNC(sym)                                                         \
  sym = reinterpret_cast<decltype(IOReport::sym)>(                             \
      dlsym(LibHandle, "IOReport" #sym));                                      \
  if (!sym) {                                                                  \
    dlclose(LibHandle);                                                        \
    LibHandle = nullptr;                                                       \
    return;                                                                    \
  }

  LOAD_FUNC(CopyChannelsInGroup)
  LOAD_FUNC(MergeChannels)
  LOAD_FUNC(CreateSubscription)
  LOAD_FUNC(CreateSamples)
  LOAD_FUNC(CreateSamplesDelta)
  LOAD_FUNC(ChannelGetGroup)
  LOAD_FUNC(ChannelGetSubGroup)
  LOAD_FUNC(ChannelGetChannelName)
  LOAD_FUNC(ChannelGetUnitLabel)
  LOAD_FUNC(StateGetCount)
  LOAD_FUNC(StateGetNameForIndex)
  LOAD_FUNC(StateGetResidency)
  LOAD_FUNC(SimpleGetIntegerValue)
  LOAD_FUNC(ChannelGetDriverName)

#undef LOAD_FUNC
}
} // namespace IOReport

// Helpers IOKit -> CPP

std::optional<std::string> SafeCFStringToStdString(CFStringRef strRef) {
  if (!strRef)
    return std::nullopt;

  CFIndex maxSize = CFStringGetMaximumSizeForEncoding(CFStringGetLength(strRef),
                                                      kCFStringEncodingUTF8) +
                    1;

  std::string result(maxSize, '\0');

  if (!CFStringGetCString(strRef, result.data(), maxSize,
                          kCFStringEncodingUTF8))
    return std::nullopt;

  result.resize(std::strlen(result.c_str()));
  return result;
}

std::optional<int64_t> SafeCFNumberToInt64(CFNumberRef numberRef) {
  if (!numberRef)
    return std::nullopt;

  int64_t value = 0;
  if (!CFNumberGetValue(numberRef, kCFNumberSInt64Type, &value))
    return std::nullopt;

  return value;
}

std::optional<std::vector<uint8_t>> SafeCFDataToRawVector(CFDataRef dataRef) {
  if (!dataRef)
    return std::nullopt;

  CFIndex length = CFDataGetLength(dataRef);
  if (length <= 0)
    return std::nullopt;

  std::vector<uint8_t> buffer(static_cast<size_t>(length));

  CFDataGetBytes(dataRef, CFRangeMake(0, length), buffer.data());

  return buffer;
}

std::optional<bool> SafeCFBooleanToBool(CFBooleanRef boolRef) {
  if (!boolRef)
    return std::nullopt;

  return CFBooleanGetValue(boolRef);
  ;
}

std::optional<std::string>
SafeIOServiceGetStringFromDictionary(CFDictionaryRef dictionary,
                                     CFStringRef key) {
  if (!dictionary || !key)
    return std::nullopt;

  CFTypeRef value = CFDictionaryGetValue(dictionary, key);
  if (!value || CFGetTypeID(value) != CFStringGetTypeID())
    return std::nullopt;

  return SafeCFStringToStdString(static_cast<CFStringRef>(value));
}

std::optional<int64_t>
SafeIOServiceGetNumberFromDictionary(CFDictionaryRef dictionary,
                                     CFStringRef key) {
  if (!dictionary || !key)
    return std::nullopt;

  CFTypeRef value = CFDictionaryGetValue(dictionary, key);
  if (!value || CFGetTypeID(value) != CFNumberGetTypeID())
    return std::nullopt;

  return SafeCFNumberToInt64(static_cast<CFNumberRef>(value));
}

std::optional<std::vector<uint8_t>>
SafeIOServiceGetDataVectorFromDictionary(CFDictionaryRef dictionary,
                                         CFStringRef key) {
  if (!dictionary || !key)
    return std::nullopt;

  CFTypeRef value = CFDictionaryGetValue(dictionary, key);
  if (!value || CFGetTypeID(value) != CFDataGetTypeID())
    return std::nullopt;

  return SafeCFDataToRawVector(static_cast<CFDataRef>(value));
}

std::optional<bool> SafeIOServiceBoolFromDictionary(CFDictionaryRef dictionary,
                                                    CFStringRef key) {
  if (!dictionary || !key)
    return std::nullopt;

  CFTypeRef value = CFDictionaryGetValue(dictionary, key);
  if (!value || CFGetTypeID(value) != CFBooleanGetTypeID())
    return std::nullopt;

  return SafeCFBooleanToBool(static_cast<CFBooleanRef>(value));
}

io_registry_entry_t IOServiceGetParent(io_registry_entry_t entry,
                                       const io_name_t plane) {
  io_iterator_t iterator = IO_OBJECT_NULL;

  if (IORegistryEntryGetParentIterator(entry, plane, &iterator) != KERN_SUCCESS)
    return IO_OBJECT_NULL;

  io_registry_entry_t parent = IOIteratorNext(iterator);
  IOObjectRelease(iterator);

  return parent; // caller must IOObjectRelease
}

bool IOServiceGenericIterator(const std::string &className,
                              IOServiceCallback callback, void *data) {
  CFDictionaryRef matching = IOServiceMatching(className.c_str());
  if (!matching)
    return false;

  io_iterator_t iterator = IO_OBJECT_NULL;
  if (IOServiceGetMatchingServices(kIOMainPortDefault, matching, &iterator) !=
      KERN_SUCCESS)
    return false;

  io_object_t service;
  while ((service = IOIteratorNext(iterator)) != IO_OBJECT_NULL) {
    bool result = false;
    if (callback)
      result = callback(service, data);

    IOObjectRelease(service);
    if (!result)
      break;
  }

  IOObjectRelease(iterator);
  return true;
}

bool IOServiceGenericChildrenIterator(io_object_t parent, const io_name_t plane,
                                      IOServiceCallback callback, void *data) {
  io_iterator_t iterator = IO_OBJECT_NULL;

  if (IORegistryEntryGetChildIterator(parent, plane, &iterator) != KERN_SUCCESS)
    return false;

  io_object_t child;
  while ((child = IOIteratorNext(iterator)) != IO_OBJECT_NULL) {
    bool result = false;
    if (callback)
      result = callback(child, data);

    IOObjectRelease(child);
    if (!result)
      break;
  }

  IOObjectRelease(iterator);
  return true;
}

extern "C" {
// create a dict ref, like for temperature sensor {"PrimaryUsagePage":0xff00,
// "PrimaryUsage":0x5}
CFDictionaryRef CreateHidMatching(int page, int usage) {
  CFNumberRef nums[2];
  CFStringRef keys[2];

  keys[0] = CFStringCreateWithCString(0, "PrimaryUsagePage", 0);
  keys[1] = CFStringCreateWithCString(0, "PrimaryUsage", 0);
  nums[0] = CFNumberCreate(0, kCFNumberSInt32Type, &page);
  nums[1] = CFNumberCreate(0, kCFNumberSInt32Type, &usage);

  CFDictionaryRef dict = CFDictionaryCreate(
      0, (const void **)keys, (const void **)nums, 2,
      &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
  CFRelease(keys[0]);
  CFRelease(keys[1]);
  CFRelease(nums[0]);
  CFRelease(nums[1]);
  return dict;
}
}