#pragma once

#include <cstdint>
#include <map>
#include <string>

class Powermetrics {
public:
    enum Section{
        None,
        CPU,
        GPU,
        ALL
    };

    struct GpuInfo {
        uint64_t active_freq_mhz   = 0;
        uint64_t active_residency  = 0; // %
        uint64_t idle_residency    = 0; // %
        uint64_t power_mw          = 0;

        std::map<uint64_t, uint64_t> hw_freq_residency; // MHz → %
        std::map<int, uint64_t>      sw_states;         // SW_Pn → %
    };

    struct CpuInfo {
        uint64_t active_residency = 0; // %
        uint64_t idle_residency   = 0; // %
        uint64_t power_mw         = 0; // mW
        uint64_t avg_freq_mhz     = 0; //
    };

    Powermetrics();
    ~Powermetrics();

    bool start();  
    void stop();
    bool available() const;

    CpuInfo cpu;
    GpuInfo gpu;

    Section section = None;

    std::string cpuInfo;
    std::string gpuInfo;
    bool newCpu = false, newGpu = false;


    Section sample();

    bool sampleGPU(GpuInfo& out);
    bool sampleCPU(CpuInfo& out);

private:
    int   m_fd  = -1;
    pid_t m_pid = -1;
    std::string m_buffer;

    static std::string trim(std::string s);

    bool parse_gpu(const std::string& text, GpuInfo& out) const;
    bool parse_cpu(const std::string& text, CpuInfo& out) const;
};

