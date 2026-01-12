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

#include <cstdint>
#include <map>
#include <string>

class Powermetrics {
public:
    // Sections of metrics
    enum Section { None, CPU, GPU, ALL };

    // GPU information
    struct GpuInfo {
        uint64_t active_freq_mhz = 0;
        uint64_t active_residency = 0; // %
        uint64_t idle_residency = 0;   // %
        uint64_t power_mw = 0;

        std::map<uint64_t, uint64_t> hw_freq_residency; // MHz → %
        std::map<int, uint64_t> sw_states;              // SW_Pn → %
    };

    // CPU information
    struct CpuInfo {
        uint64_t active_residency = 0; // %
        uint64_t idle_residency = 0;   // %
        uint64_t power_mw = 0;         // mW
        uint64_t avg_freq_mhz = 0;     // MHz
    };
    
public:
    Powermetrics();
    ~Powermetrics();

    // Start/stop monitoring
    bool start();
    void stop();
    bool available() const;

    // Sampling
    Section sample();
    bool sampleGPU(GpuInfo &out);
    bool sampleCPU(CpuInfo &out);

public:
    CpuInfo cpu;
    GpuInfo gpu;

    Section section = None;

    std::string cpuInfo;
    std::string gpuInfo;
    bool newCpu = false;
    bool newGpu = false;

private:
    int m_fd = -1;
    pid_t m_pid = -1;
    std::string m_buffer;

    // Helpers
    static std::string trim(std::string s);

    bool parse_gpu(const std::string &text, GpuInfo &out) const;
    bool parse_cpu(const std::string &text, CpuInfo &out) const;
};
