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
#include <IOKit/IOKitLib.h>
#include <IOKit/IOTypes.h>
#include <cstdint>
#include <string>
#include <vector>

// ----------------------------------------
// GPU representation
// ----------------------------------------
class GPU {
public:
    struct PerformanceStatistics {
        uint64_t alloc_system_memory = 0;
        uint64_t allocated_pb_size = 0;
        uint64_t device_utilization = 0;
        uint64_t in_use_system_memory = 0;
        uint64_t in_use_system_memory_driver = 0;
        uint64_t last_recovery_time = 0;
        uint64_t recovery_count = 0;
        uint64_t renderer_utilization = 0;
        uint64_t split_scene_count = 0;
        uint64_t tiled_scene_bytes = 0;
        uint64_t tiler_utilization = 0;
    };

public:
    GPU(const std::string &modelName, const PerformanceStatistics &statsData);

    const std::string &GetModel() const;
    const PerformanceStatistics &GetStats() const;

private:
    std::string model;
    PerformanceStatistics stats;
};

// ----------------------------------------
// IO-based GPU enumeration
// ----------------------------------------
class IOGPU {
public:
    IOGPU();

    // Returns the current list of GPUs
    const std::vector<GPU> &GetGPUList() const;

    // Updates the GPU list by reading from IOService
    void Update();

private:
    std::vector<GPU> gpu_list;

    // Helpers for interacting with IOKit and CoreFoundation
    std::string CFStringToStdString(CFStringRef cfStr) const;
    std::string ReadGPUModel(io_object_t service) const;
    GPU::PerformanceStatistics ReadGPUPerformanceStats(io_object_t service) const;
    void MapKeyToStats(GPU::PerformanceStatistics &stats, const std::string &key,
                       uint64_t value) const;
};
