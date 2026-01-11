#include "iogpu.hpp"
#include <cstring>

// ----------------------------------------
// GPU Implementation
// ----------------------------------------
GPU::GPU(const std::string &modelName, const PerformanceStatistics &statsData)
    : model(modelName), stats(statsData) {}

const std::string &GPU::GetModel() const {
    return model;
}

const GPU::PerformanceStatistics &GPU::GetStats() const {
    return stats;
}

// ----------------------------------------
// IOGPU Implementation
// ----------------------------------------
IOGPU::IOGPU() {
    Update(); // Initialize the GPU list
}

const std::vector<GPU> &IOGPU::GetGPUList() const {
    return gpu_list;
}

// Updates the GPU list by reading from IOService
void IOGPU::Update() {
    gpu_list.clear(); // Clear old list

    CFMutableDictionaryRef matchDict = IOServiceMatching("IOAccelerator");
    if (!matchDict)
        return;

    io_iterator_t iter;
    kern_return_t kr = IOServiceGetMatchingServices(kIOMainPortDefault, matchDict, &iter);
    if (kr != KERN_SUCCESS)
        return;

    io_object_t service;
    while ((service = IOIteratorNext(iter)) != 0) {
        auto modelName = ReadGPUModel(service);
        auto statsData = ReadGPUPerformanceStats(service);
        gpu_list.emplace_back(modelName, statsData);
        IOObjectRelease(service);
    }

    IOObjectRelease(iter);
}

// Converts a CFStringRef to std::string
std::string IOGPU::CFStringToStdString(CFStringRef cfStr) const {
    if (!cfStr)
        return {};

    CFIndex size = CFStringGetLength(cfStr);
    std::string str(size + 1, '\0');

    if (CFStringGetCString(cfStr, str.data(), size + 1, kCFStringEncodingUTF8)) {
        str.resize(strlen(str.data()));
        return str;
    }

    return {};
}

// Reads the GPU model name from an IOService
std::string IOGPU::ReadGPUModel(io_object_t service) const {
    CFStringRef nameRef = static_cast<CFStringRef>(
        IORegistryEntryCreateCFProperty(service, CFSTR("model"), kCFAllocatorDefault, 0)
    );

    std::string modelName;
    if (nameRef) {
        modelName = CFStringToStdString(nameRef);
        CFRelease(nameRef);
    }

    return modelName;
}

// Reads GPU performance statistics from an IOService
GPU::PerformanceStatistics IOGPU::ReadGPUPerformanceStats(io_object_t service) const {
    GPU::PerformanceStatistics stats;

    CFDictionaryRef perfStats = static_cast<CFDictionaryRef>(
        IORegistryEntryCreateCFProperty(service, CFSTR("PerformanceStatistics"), kCFAllocatorDefault, 0)
    );
    if (!perfStats)
        return stats;

    CFIndex count = CFDictionaryGetCount(perfStats);
    std::vector<const void *> keys(count);
    std::vector<const void *> values(count);
    CFDictionaryGetKeysAndValues(perfStats, keys.data(), values.data());

    for (CFIndex i = 0; i < count; ++i) {
        CFStringRef keyStr = static_cast<CFStringRef>(keys[i]);
        CFTypeRef value = values[i];
        std::string key = CFStringToStdString(keyStr);

        if (CFGetTypeID(value) == CFNumberGetTypeID()) {
            uint64_t num = 0;
            CFNumberGetValue(static_cast<CFNumberRef>(value), kCFNumberSInt64Type, &num);
            MapKeyToStats(stats, key, num);
        }
    }

    CFRelease(perfStats);
    return stats;
}

// Maps string keys to the corresponding fields in PerformanceStatistics
void IOGPU::MapKeyToStats(GPU::PerformanceStatistics &stats, const std::string &key, uint64_t value) const {
    if (key == "Alloc system memory")
        stats.alloc_system_memory = value;
    else if (key == "Allocated PB Size")
        stats.allocated_pb_size = value;
    else if (key == "Device Utilization %")
        stats.device_utilization = value;
    else if (key == "In use system memory")
        stats.in_use_system_memory = value;
    else if (key == "In use system memory (driver)")
        stats.in_use_system_memory_driver = value;
    else if (key == "lastRecoveryTime")
        stats.last_recovery_time = value;
    else if (key == "recoveryCount")
        stats.recovery_count = value;
    else if (key == "Renderer Utilization %")
        stats.renderer_utilization = value;
    else if (key == "SplitSceneCount")
        stats.split_scene_count = value;
    else if (key == "TiledSceneBytes")
        stats.tiled_scene_bytes = value;
    else if (key == "Tiler Utilization %")
        stats.tiler_utilization = value;
}
