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
#pragma once
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOTypes.h>
#include <cstdint>
#include <string>
#include <sys/_types/_pid_t.h>
#include <tuple>
#include <unordered_map>
#include <vector>

class GPUActivities {
public:
  struct Usage {
    int64_t accumulated_gpu_time;
    int64_t last_submitted_time;
    std::string api;
  };

  std::vector<Usage> usage;
  pid_t proc;
  std::string name;
  int64_t comand_queue_count;
  int64_t comand_queue_count_max;
  GPUActivities(io_object_t io_accelerator_children);

private:
  void mapKeyToUsageString(GPUActivities::Usage &usage, const std::string &key,
                           const std::string &value);
  void mapKeyToUsageNumber(GPUActivities::Usage &usage, const std::string &key,
                           int64_t value);
};

class GPU {
public:
  struct PerformanceStatistics {
    int64_t device_utilization;

    int64_t alloc_system_memory;
    int64_t allocated_pb_size;
    int64_t in_use_system_memory;
    int64_t in_use_system_memory_driver;
    int64_t last_recovery_time;
    int64_t recovery_count;
    int64_t renderer_utilization;
    int64_t split_scene_count;
    int64_t tiled_scene_bytes;
    int64_t tiler_utilization;

    int64_t gpu_frequency;
    int64_t gpu_voltage;

    int64_t milliwatts;
    double temp_c;
  };

private:
  std::string name;
  std::string driver;
  std::string io_path;

  PerformanceStatistics statistics;

  uint64_t prevGpuSecondsElapsed = 0;
  uint64_t actualGpuInternalTime = 0;
  uint64_t lastGpuInternalTime = 0;
  std::unordered_map<pid_t, std::tuple<GPUActivities, uint64_t, double>>
      last_activities;
  std::unordered_map<pid_t, std::tuple<GPUActivities, uint64_t, double>>
      actual_activities;

  int64_t core_count;

  std::vector<std::tuple<uint32_t, uint32_t>> gpu_table;
  uint32_t max_freq = 0;
  uint32_t mav_voltage = 0;

  void LookupProcessPercentage();
  void Lookup(io_object_t ioAccelerator);
  void mapKeyToPerformanceStatistics(const std::string &key, int64_t value);
  void ParserChannels(CFDictionaryRef delta, double elapsed_seconds);

  static bool childrenIteratorCallback(io_object_t object, void *data);

  static bool appleArmIoDeviceIteratorCallback(io_object_t object, void *data);

  uint64_t prevSampleTime = 0;
  CFTypeRef subscription = nullptr;
  CFMutableDictionaryRef channels = nullptr;
  CFDictionaryRef prevSample = nullptr;

public:
  GPU(io_object_t io_accelerator);

  const std::unordered_map<pid_t, std::tuple<GPUActivities, uint64_t, double>> &
  getActivities() const;
  const PerformanceStatistics &getStatistics() const;
  const std::string &getName() const;
  const int64_t &getCoreCount() const;

  bool Refesh();

  ~GPU();
};

class IOGPU {
private:
  std::vector<GPU> gpus;
  static bool iteratorCallback(io_object_t object, void *data);
  static void ioReportTryLoad();

public:
  IOGPU();
  std::vector<GPU> &getGPUs();
};