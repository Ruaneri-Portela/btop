/* Copyright 2026 Ruaneri Portela (ruaneriportela@outlook.com)
   Copyright 2026 Emanuele Zattin (Emanuelez@gmail.com)

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
#include "gpu.hpp"
#include "iokit.hpp"
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <mach/mach_time.h>
#include <tuple>
#include <unordered_map>
#include <utility>

// Others
static uint64_t getTimeNs() {
  static mach_timebase_info_data_t timebase = {0, 0};
  if (timebase.denom == 0) {
    mach_timebase_info(&timebase);
  }
  return mach_absolute_time() * timebase.numer / timebase.denom;
}

// This non work with same reason what in sensors.cpp
static double GetGpuTemperature() {
  CFDictionaryRef matching = CreateHidMatching(0xff00, 5);
  IOHIDEventSystemClientRef system =
      IOHIDEventSystemClientCreate(kCFAllocatorDefault);
  IOHIDEventSystemClientSetMatching(system, matching);
  CFArrayRef services = IOHIDEventSystemClientCopyServices(system);

  double gpu_temp = 0.0;

  if (services != nullptr) {
    CFIndex count = CFArrayGetCount(services);
    for (CFIndex i = 0; i < count; i++) {
      IOHIDServiceClientRef service =
          (IOHIDServiceClientRef)CFArrayGetValueAtIndex(services, i);
      if (service != nullptr) {
        CFStringRef name =
            IOHIDServiceClientCopyProperty(service, CFSTR("Product"));
        if (name != nullptr) {
          std::string sensor_name = SafeCFStringToStdString(name).value_or("");
          CFRelease(name);

          // In M4 CPU show all cores temperatures, but need to parser

          if (sensor_name.find("GPU") != std::string::npos) {
            IOHIDEventRef event = IOHIDServiceClientCopyEvent(
                service, kIOHIDEventTypeTemperature, 0, 0);
            if (event != nullptr) {
              double temp = IOHIDEventGetFloatValue(
                  event, IOHIDEventFieldBase(kIOHIDEventTypeTemperature));
              if (temp > 0 && temp < 150) { // Sanity check
                gpu_temp += temp;
              }
              CFRelease(event);
            }
          }
        }
      }
    }
    CFRelease(services);
  }
  CFRelease(matching);
  CFRelease(system);
  return gpu_temp;
}

// Activities
void GPUActivities::mapKeyToUsageNumber(GPUActivities::Usage &usage,
                                        const std::string &key, int64_t value) {
  static const std::unordered_map<std::string, int64_t GPUActivities::Usage::*>
      map = {
          {"accumulatedGPUTime", &GPUActivities::Usage::accumulated_gpu_time},

          {"lastSubmittedTime", &GPUActivities::Usage::last_submitted_time},
      };

  if (auto it = map.find(key); it != map.end()) {
    usage.*(it->second) = value;
  }
}

void GPUActivities::mapKeyToUsageString(GPUActivities::Usage &usage,
                                        const std::string &key,
                                        const std::string &value) {
  static const std::unordered_map<std::string,
                                  std::string GPUActivities::Usage::*>
      map = {
          {"API", &GPUActivities::Usage::api},
      };

  if (auto it = map.find(key); it != map.end()) {
    usage.*(it->second) = value;
  }
}

GPUActivities::GPUActivities(io_object_t entry) {
  CFTypeRef creatorRef = IORegistryEntryCreateCFProperty(
      entry, CFSTR("IOUserClientCreator"), kCFAllocatorDefault, 0);

  if (!creatorRef)
    return;

  auto creator = SafeCFStringToStdString(static_cast<CFStringRef>(creatorRef));

  CFRelease(creatorRef);

  if (!creator)
    return;

  const std::string &s = *creator;

  const auto pidPos = s.find("pid ");
  const auto commaPos = s.find(',');

  if (pidPos == std::string::npos || commaPos == std::string::npos)
    return;

  try {
    proc = std::stoul(s.substr(pidPos + 4, commaPos - (pidPos + 4)));
    name = s.substr(commaPos + 2);
  } catch (...) {
    return;
  }

  CFTypeRef appUsageRef = IORegistryEntryCreateCFProperty(
      entry, CFSTR("AppUsage"), kCFAllocatorDefault, 0);

  if (!appUsageRef)
    return;

  if (CFGetTypeID(appUsageRef) != CFArrayGetTypeID()) {
    CFRelease(appUsageRef);
    return;
  }

  CFArrayRef appUsageArray = static_cast<CFArrayRef>(appUsageRef);
  CFIndex count = CFArrayGetCount(appUsageArray);

  for (CFIndex i = 0; i < count; ++i) {
    CFTypeRef item = CFArrayGetValueAtIndex(appUsageArray, i);

    if (!item || CFGetTypeID(item) != CFDictionaryGetTypeID())
      continue;

    CFDictionaryRef usageStats = static_cast<CFDictionaryRef>(item);
    if (usageStats) {

      CFIndex count = CFDictionaryGetCount(usageStats);

      std::vector<const void *> keys(count);
      std::vector<const void *> values(count);

      CFDictionaryGetKeysAndValues(usageStats, keys.data(), values.data());
      GPUActivities::Usage newUsage{};
      for (CFIndex i = 0; i < count; ++i) {

        CFStringRef keyRef = static_cast<CFStringRef>(keys[i]);

        auto key = SafeCFStringToStdString(keyRef);
        if (!key)
          continue;

        auto number = SafeIOServiceGetNumberFromDictionary(usageStats, keyRef);
        if (number.has_value()) {
          mapKeyToUsageNumber(newUsage, *key, *number);
          continue;
        }

        auto string = SafeIOServiceGetStringFromDictionary(usageStats, keyRef);
        if (string.has_value()) {
          mapKeyToUsageString(newUsage, *key, *string);
          continue;
        }
      }
      usage.emplace_back(newUsage);
    }
  }
  CFRelease(appUsageRef);
}

// GPU

void GPU::mapKeyToPerformanceStatistics(const std::string &key, int64_t value) {
  static const std::unordered_map<std::string, int64_t PerformanceStatistics::*>
      map = {
          {"Alloc system memory", &PerformanceStatistics::alloc_system_memory},
          {"Allocated PB Size", &PerformanceStatistics::allocated_pb_size},
          {"Device Utilization %", &PerformanceStatistics::device_utilization},
          {"In use system memory",
           &PerformanceStatistics::in_use_system_memory},
          {"In use system memory (driver)",
           &PerformanceStatistics::in_use_system_memory_driver},
          {"lastRecoveryTime", &PerformanceStatistics::last_recovery_time},
          {"recoveryCount", &PerformanceStatistics::recovery_count},
          {"Renderer Utilization %",
           &PerformanceStatistics::renderer_utilization},
          {"SplitSceneCount", &PerformanceStatistics::split_scene_count},
          {"TiledSceneBytes", &PerformanceStatistics::tiled_scene_bytes},
          {"Tiler Utilization %", &PerformanceStatistics::tiler_utilization},
      };

  if (auto it = map.find(key); it != map.end()) {
    statistics.*(it->second) = value;
  }
}

bool GPU::childrenIteratorCallback(io_object_t object, void *data) {
  auto *self = static_cast<GPU *>(data);

  GPUActivities newActivity(object);

  if (newActivity.usage.size() == 0)
    return true;

  uint64_t totalUsage = 0;

  for (const auto &u : newActivity.usage) {
    totalUsage += u.accumulated_gpu_time;
  }

  self->actualGpuInternalTime += totalUsage;

  self->actual_activities.insert_or_assign(
      newActivity.proc, std::make_tuple(std::move(newActivity), totalUsage, 0));

  return true;
}

bool GPU::appleArmIoDeviceIteratorCallback(io_object_t device, void *data) {
  auto *self = static_cast<GPU *>(data);

  io_name_t name{};
  IORegistryEntryGetName(device, name);

  if (std::strcmp(name, "pmgr") != 0)
    return true;

  CFMutableDictionaryRef properties = nullptr;
  if (IORegistryEntryCreateCFProperties(
          device, &properties, kCFAllocatorDefault, 0) != kIOReturnSuccess)
    return false;

  auto buffer = SafeIOServiceGetDataVectorFromDictionary(
      properties, CFSTR("voltage-states9"));

  if (buffer) {
    const auto &bytes = *buffer;
    const CFIndex length = bytes.size();

    if (length >= 8 && (length % 8) == 0) {
      self->gpu_table.clear();
      uint32_t max_freq = 0;
      uint32_t max_voltage = 0;

      // For 8 bytes: [freq_hz(4), voltage_uv(4)]
      for (CFIndex i = 0; i + 8 <= length; i += 8) {
        uint32_t freq_hz = 0;
        uint32_t voltage_uv = 0;

        std::memcpy(&freq_hz, bytes.data() + i, sizeof(freq_hz));
        std::memcpy(&voltage_uv, bytes.data() + i + 4, sizeof(voltage_uv));

        if (freq_hz > 0) {
          self->gpu_table.emplace_back(freq_hz, voltage_uv);

          if (freq_hz > max_freq)
            max_freq = freq_hz;
          if (voltage_uv > max_voltage)
            max_voltage = voltage_uv;
        }
      }

      self->max_freq = max_freq;
      self->mav_voltage = max_voltage;

      std::sort(self->gpu_table.begin(), self->gpu_table.end(),
                [](const auto &a, const auto &b) {
                  return std::get<0>(a) < std::get<0>(b);
                });
    }
  }

  CFRelease(properties);
  return false;
}

void GPU::LookupProcessPercentage() {
  int64_t deltaGpuInternalTime = actualGpuInternalTime - lastGpuInternalTime;

  uint64_t actualGpuSecondsElapsed = getTimeNs();
  prevGpuSecondsElapsed = actualGpuSecondsElapsed;

  if (!last_activities.empty() && deltaGpuInternalTime > 0) {
    double denom = static_cast<double>(deltaGpuInternalTime);
    double maxUtil = static_cast<double>(statistics.device_utilization);

    for (auto &[pid, currTuple] : actual_activities) {

      auto &activity = std::get<0>(currTuple);
      if (activity.name == "java")
        puts("");
      auto &totalTime = std::get<1>(currTuple);
      auto &targetPercentage = std::get<2>(currTuple);

      auto it = last_activities.find(pid);
      if (it == last_activities.end())
        continue;

      auto &totalTimePrev = std::get<1>(it->second);

      uint64_t deltaGpuTime =
          (totalTime >= totalTimePrev) ? (totalTime - totalTimePrev) : 0;

      if (deltaGpuTime == 0)
        continue;

      double relative = static_cast<double>(deltaGpuTime) / denom;

      targetPercentage = std::clamp(relative * maxUtil, 0.0, maxUtil);
    }
  }
}

void GPU::Lookup(io_object_t ioAccelerator) {
  CFDictionaryRef perfStats =
      static_cast<CFDictionaryRef>(IORegistryEntryCreateCFProperty(
          ioAccelerator, CFSTR("PerformanceStatistics"), kCFAllocatorDefault,
          0));

  if (perfStats) {
    CFIndex count = CFDictionaryGetCount(perfStats);

    std::vector<const void *> keys(count);
    std::vector<const void *> values(count);

    CFDictionaryGetKeysAndValues(perfStats, keys.data(), values.data());

    for (CFIndex i = 0; i < count; ++i) {
      auto key = SafeCFStringToStdString(static_cast<CFStringRef>(keys[i]));
      if (!key)
        continue;

      auto value = SafeCFNumberToInt64(static_cast<CFNumberRef>(values[i]));
      if (key)
        mapKeyToPerformanceStatistics(*key, *value);
    }

    CFRelease(perfStats);
  }

  last_activities.clear();

  lastGpuInternalTime = actualGpuInternalTime;
  actualGpuInternalTime = 0;
  if (actual_activities.size() > 0) {
    last_activities = std::move(actual_activities);
  }

  actual_activities.clear();

  IOServiceGenericChildrenIterator(ioAccelerator, kIOServicePlane,
                                   childrenIteratorCallback, this);
}

GPU::GPU(io_object_t ioAccelerator) {
  // Save full path to fast refesh on some stats, eg. memeory
  io_name_t pathBuffer;
  if (IORegistryEntryGetPath(ioAccelerator, kIOServicePlane, pathBuffer) !=
      KERN_SUCCESS)
    return;

  io_path = std::string(pathBuffer);

  CFStringRef nameRef =
      static_cast<CFStringRef>(IORegistryEntryCreateCFProperty(
          ioAccelerator, CFSTR("model"), kCFAllocatorDefault, 0));

  if (nameRef) {
    name = SafeCFStringToStdString(nameRef).value_or("Undefined");
    CFRelease(nameRef);
  }

  CFStringRef driverRef =
      static_cast<CFStringRef>(IORegistryEntryCreateCFProperty(
          ioAccelerator, CFSTR("IOClass"), kCFAllocatorDefault, 0));

  if (driverRef) {
    driver = SafeCFStringToStdString(driverRef).value_or("Undefined");
    CFRelease(driverRef);
  }

  CFNumberRef coreCountRef =
      static_cast<CFNumberRef>(IORegistryEntryCreateCFProperty(
          ioAccelerator, CFSTR("gpu-core-count"), kCFAllocatorDefault, 0));

  if (coreCountRef) {
    core_count = SafeCFNumberToInt64(coreCountRef).value_or(0);
    CFRelease(coreCountRef);
  }

  IOServiceGenericIterator("AppleARMIODevice", appleArmIoDeviceIteratorCallback,
                           this);

  Lookup(ioAccelerator);

  if (!IOReport::LibHandle)
    return;

  CFStringRef gpu_stats_group = CFStringCreateWithCString(
      kCFAllocatorDefault, "GPU Stats", kCFStringEncodingUTF8);
  CFDictionaryRef gpu_stats_channels =
      IOReport::CopyChannelsInGroup(gpu_stats_group, nullptr, 0, 0, 0);
  CFRelease(gpu_stats_group);

  if (gpu_stats_channels == nullptr) {
    return;
  }

  CFStringRef energy_group = CFStringCreateWithCString(
      kCFAllocatorDefault, "Energy Model", kCFStringEncodingUTF8);
  CFDictionaryRef energy_channels =
      IOReport::CopyChannelsInGroup(energy_group, nullptr, 0, 0, 0);
  CFRelease(energy_group);

  if (energy_channels == nullptr) {
    CFRelease(gpu_stats_channels);
    return;
  }

  // Merge channels
  channels =
      CFDictionaryCreateMutableCopy(kCFAllocatorDefault, 0, gpu_stats_channels);

  IOReport::MergeChannels(channels, energy_channels, nullptr);
  CFRelease(gpu_stats_channels);
  CFRelease(energy_channels);

  // Create subscription
  CFMutableDictionaryRef sub_channels = nullptr;
  subscription = IOReport::CreateSubscription(nullptr, channels, &sub_channels,
                                              0, nullptr);

  if (subscription == nullptr) {
    return;
  }

  // Take Initial Sample
  prevSample = IOReport::CreateSamples(subscription, channels, nullptr);
  CFRelease(sub_channels);
  prevSampleTime = getTimeNs();

  LookupProcessPercentage();
  return;
}

void GPU::ParserChannels(CFDictionaryRef delta, double elapsedSeconds) {
  CFArrayRef channel_array =
      (CFArrayRef)CFDictionaryGetValue(delta, CFSTR("IOReportChannels"));
  if (channel_array == nullptr or
      CFGetTypeID(channel_array) != CFArrayGetTypeID()) {
    return;
  }

  uint64_t n_joule = 0;
  // Temperature accumulation
  double temp_sum = 0.0;
  int64_t temp_count = 0;

  // Map unit to nanojoules conversion factor
  static const std::unordered_map<std::string, long long> unit_to_nj{
      {"mJ", 1'000'000}, // 1 mJ = 1e6 nJ
      {"uJ", 1'000},     // 1 µJ = 1e3 nJ
      {"nJ", 1}          // 1 nJ = 1 nJ
  };

  CFIndex count = CFArrayGetCount(channel_array);

  for (CFIndex i = 0; i < count; i++) {
    CFDictionaryRef channel =
        (CFDictionaryRef)CFArrayGetValueAtIndex(channel_array, i);

    if (!channel || CFGetTypeID(channel) != CFDictionaryGetTypeID())
      continue;

    auto driver_name =
        SafeCFStringToStdString(IOReport::ChannelGetDriverName(channel))
            .value_or("");

    // Filter driver name on Channel values
    if (driver_name.find(driver) == std::string::npos)
      continue;

    auto group = SafeCFStringToStdString(IOReport::ChannelGetGroup(channel))
                     .value_or("");
    auto subgroup =
        SafeCFStringToStdString(IOReport::ChannelGetSubGroup(channel))
            .value_or("");
    auto channel_name =
        SafeCFStringToStdString(IOReport::ChannelGetChannelName(channel))
            .value_or("");

    // GPU Performance States
    if (group == "GPU Stats" && subgroup == "GPU Performance States" &&
        channel_name == "GPUPH") {

      int32_t state_count = IOReport::StateGetCount(channel);
      int64_t total_time = 0, active_time = 0;
      int64_t weighted_freq = 0, weighted_volt = 0;

      for (int32_t s = 0; s < state_count; s++) {
        auto state_name =
            SafeCFStringToStdString(IOReport::StateGetNameForIndex(channel, s))
                .value_or("");
        int64_t residency_ns = IOReport::StateGetResidency(channel, s);
        total_time += residency_ns;

        if (state_name.empty() || state_name == "OFF" || state_name == "IDLE")
          continue;

        int64_t freq = 0, volt = 0;
        if (state_name[0] == 'P' && state_name.length() > 1) {
          try {
            int pstate_idx = std::stoi(state_name.substr(1)) - 1;
            if (pstate_idx >= 0 &&
                static_cast<size_t>(pstate_idx) < gpu_table.size()) {
              freq = std::get<0>(gpu_table[pstate_idx]);
              volt = std::get<1>(gpu_table[pstate_idx]);
            }
          } catch (...) {
          }
        } else {
          try {
            freq = std::stoll(state_name);
          } catch (...) {
          }
        }

        if (freq > 0 && residency_ns > 0) {
          weighted_freq += freq * residency_ns;
          weighted_volt += volt * residency_ns;
          active_time += residency_ns;
        }
      }

      if (active_time > 0) {
        statistics.gpu_frequency = weighted_freq / active_time;
        statistics.gpu_voltage = weighted_volt / active_time;
      }

      if (total_time > 0) {
        double usage_percent =
            (static_cast<double>(active_time) / total_time) * 100.0;
        statistics.device_utilization =
            static_cast<int64_t>(std::clamp(usage_percent, 0.0, 100.0));
      }
    }

    // Temperature
    if (group == "GPU Stats" && subgroup == "Temperature") {
      int64_t value = IOReport::SimpleGetIntegerValue(channel, 0);
      if (channel_name == "Average Sum")
        temp_sum = static_cast<double>(value);
      else if (channel_name == "Average Sum Count")
        temp_count = value;
    }

    // GPU Energy
    if (group == "Energy Model" &&
        channel_name.find("GPU Energy") != std::string::npos) {
      auto unit =
          SafeCFStringToStdString(IOReport::ChannelGetUnitLabel(channel))
              .value_or("");

      int64_t energy_value = IOReport::SimpleGetIntegerValue(channel, 0);

      long long factor = 1'000'000'000; // default to 1 nJ = 1e-9 J
      if (unit_to_nj.count(unit))
        factor = unit_to_nj.at(unit);

      n_joule += energy_value * factor;
    }
  }

  // Compute power in mW
  if (elapsedSeconds > 0 && n_joule > 0) {
    statistics.milliwatts =
        static_cast<double>(n_joule) * 1e-6 / elapsedSeconds;
  }

  // This no work in my M4 pro
  // Calculate average temperature
  // IOReport temperature values are in centiCelsius (hundredths of a degree)
  if (temp_count > 0 && temp_sum > 0) {
    statistics.temp_c = (temp_sum / static_cast<double>(temp_count)) / 100.0;
  }
}

bool GPU::Refesh() {
  io_object_t ioAccelerator =
      IORegistryEntryFromPath(kIOMainPortDefault, io_path.c_str());

  if (ioAccelerator == IO_OBJECT_NULL) {
    return false;
  }
  Lookup(ioAccelerator);
  IOObjectRelease(ioAccelerator);

  if (IOReport::LibHandle) {
    CFDictionaryRef currentSample =
        IOReport::CreateSamples(subscription, channels, nullptr);

    uint64_t currentTime = getTimeNs();
    double elapsedSeconds = (currentTime - prevSampleTime) / 1e9;

    CFDictionaryRef delta =
        IOReport::CreateSamplesDelta(prevSample, currentSample, nullptr);

    if (delta) {
      ParserChannels(delta, elapsedSeconds);
      CFRelease(delta);
    }

    if (currentSample) {
      CFRelease(prevSample);
      prevSample = currentSample;
    }
    prevSampleTime = currentTime;

    // If IOReport didn't provide temperature, fall back to IOHIDSensors
    if (statistics.temp_c <= 0.0) {
      statistics.temp_c = GetGpuTemperature();
    }
  }

  LookupProcessPercentage();
  return true;
}

GPU::~GPU() {
  if (prevSample)
    CFRelease(prevSample);

  if (channels)
    CFRelease(channels);
}

// IOGPU

bool IOGPU::iteratorCallback(io_object_t object, void *data) {
  auto *self = static_cast<IOGPU *>(data);
  self->gpus.emplace_back(object);
  return true;
}

// In theory, if you do this on an Intel Mac with a dedicated GPU, it may show
// the other accelerator, in which case there may be bugs as I haven't tested
// it.
IOGPU::IOGPU() {
  IOReport::TryLoad();
  IOServiceGenericIterator("IOAccelerator", iteratorCallback, this);
}

const std::unordered_map<pid_t, std::tuple<GPUActivities, uint64_t, double>> &
GPU::getActivities() const {
  return actual_activities;
}

const GPU::PerformanceStatistics &GPU::getStatistics() const {
  return statistics;
}

const std::string &GPU::getName() const { return name; }

const int64_t &GPU::getCoreCount() const { return core_count; }

std::vector<GPU> &IOGPU::getGPUs() { return gpus; }
