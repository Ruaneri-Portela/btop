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

#include "powermetrics.hpp"

#include <CoreFoundation/CFBase.h>
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>

#include <cstddef>
#include <cstdint>
#include <spawn.h>
#include <string>
#include <sys/types.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>
#include <sstream>
#include <iostream>
#include <algorithm>

extern char **environ;

// ----------------------------------------
// Constructor / Destructor
// ----------------------------------------
Powermetrics::Powermetrics() = default;

Powermetrics::~Powermetrics() {
    stop();
}

// ----------------------------------------
// Helpers
// ----------------------------------------
std::string Powermetrics::trim(std::string s) {
    s.erase(0, s.find_first_not_of(" \t"));
    s.erase(s.find_last_not_of(" \t") + 1);
    return s;
}

// Checks if the powermetrics process is still running
bool Powermetrics::available() const {
    if (m_pid <= 0)
        return false;

    if (kill(m_pid, 0) != 0)
        return false;

    std::string cmd = "ps -p " + std::to_string(m_pid) + " -o state= 2>/dev/null";
    FILE *pipe = popen(cmd.c_str(), "r");
    if (!pipe)
        return false;

    char buffer[16];
    if (fgets(buffer, sizeof(buffer), pipe) == nullptr) {
        pclose(pipe);
        return false;
    }
    pclose(pipe);

    std::string state(buffer);
    state.erase(std::remove_if(state.begin(), state.end(), isspace), state.end());

    return state.find('Z') == std::string::npos;
}

// ----------------------------------------
// Start / Stop powermetrics process
// ----------------------------------------
bool Powermetrics::start() {
    if (m_pid > 0)
        return true;

    int pipefd[2];
    if (pipe(pipefd) != 0)
        return false;

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);

    // Redirect stdout/stderr to pipe
    posix_spawn_file_actions_adddup2(&actions, pipefd[1], STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&actions, pipefd[1], STDERR_FILENO);
    posix_spawn_file_actions_addclose(&actions, pipefd[0]);
    posix_spawn_file_actions_addclose(&actions, pipefd[1]);

    const char *argv[] = {
        "sudo", "-n", "powermetrics", "--samplers", "gpu_power,cpu_power",
        "-i", "1000", nullptr
    };

    pid_t pid;
    int rc = posix_spawnp(&pid, "sudo", &actions, nullptr,
                          const_cast<char *const *>(argv), environ);

    posix_spawn_file_actions_destroy(&actions);
    close(pipefd[1]);

    if (rc != 0) {
        close(pipefd[0]);
        return false;
    }

    m_pid = pid;
    m_fd = pipefd[0];
    m_buffer.clear();
    fcntl(m_fd, F_SETFL, O_NONBLOCK);
    return true;
}

void Powermetrics::stop() {
    if (m_pid > 0) {
        kill(m_pid, SIGTERM);
        waitpid(m_pid, nullptr, 0);
    }

    if (m_fd >= 0)
        close(m_fd);

    m_fd = -1;
    m_pid = -1;
}

// ----------------------------------------
// Sample data from powermetrics
// ----------------------------------------
Powermetrics::Section Powermetrics::sample() {
    if (m_fd < 0)
        return None;

    char buf[4096];
    while (true) {
        ssize_t n = read(m_fd, buf, sizeof(buf));
        if (n > 0) {
            m_buffer.append(buf, n);
        } else if (n == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            break;
        } else {
            break;
        }
    }

    Section sampled_section = None;
    size_t pos;

    while ((pos = m_buffer.find('\n')) != std::string::npos) {
        std::string line = trim(m_buffer.substr(0, pos));
        m_buffer.erase(0, pos + 1);

        if (line.empty())
            continue;

        // Detect section headers
        if (line.starts_with("****")) {

            // Parse GPU section if it just ended
            if (section == GPU) {
                newGpu = parse_gpu(gpuInfo, gpu);
                gpuInfo.clear();
                if (!newGpu)
                    continue;
                if (sampled_section == None)
                    sampled_section = GPU;
                if (sampled_section == CPU)
                    sampled_section = ALL;
            }

            // Parse CPU section if it just ended
            if (section == CPU) {
                newCpu = parse_cpu(cpuInfo, cpu);
                cpuInfo.clear();
                if (!newCpu)
                    continue;
                if (sampled_section == None)
                    sampled_section = CPU;
                if (sampled_section == GPU)
                    sampled_section = ALL;
            }

            // Update current section
            section = None;
            if (line == "**** GPU usage ****") {
                section = GPU;
            } else if (line == "**** Processor usage ****") {
                section = CPU;
            }

            continue;
        }

        // Append lines to current section buffer
        if (section == GPU) {
            gpuInfo.append(line).push_back('\n');
        } else if (section == CPU) {
            cpuInfo.append(line).push_back('\n');
        }
    }

    return sampled_section;
}

// ----------------------------------------
// Sample GPU / CPU
// ----------------------------------------
bool Powermetrics::sampleGPU(GpuInfo &out) {
    sample();
    out = gpu;
    if (newGpu) {
        newGpu = false;
        return true;
    }
    return false;
}

bool Powermetrics::sampleCPU(CpuInfo &out) {
    sample();
    out = cpu;
    if (newCpu) {
        newCpu = false;
        return true;
    }
    return false;
}

// ----------------------------------------
// Parse GPU text data
// ----------------------------------------
bool Powermetrics::parse_gpu(const std::string &text, GpuInfo &out) const {
    auto clamp_pct = [](double v) -> uint64_t {
        if (v < 0.0) return 0;
        if (v > 100.0) return 100;
        return static_cast<uint64_t>(v + 0.5);
    };

    std::istringstream in(text);
    std::string line;

    while (std::getline(in, line)) {
        if (line.empty()) continue;

        if (line.starts_with("GPU HW active frequency:")) {
            std::sscanf(line.c_str(), "GPU HW active frequency: %llu MHz", &out.active_freq_mhz);
        }
        else if (line.starts_with("GPU HW active residency:")) {
            double pct = 0.0;
            std::sscanf(line.c_str(), "GPU HW active residency: %lf%%", &pct);
            out.active_residency = clamp_pct(pct);

            auto l = line.find('(');
            auto r = line.find(')');
            if (l != std::string::npos && r != std::string::npos) {
                std::istringstream iss(line.substr(l + 1, r - l - 1));
                while (!iss.eof()) {
                    uint64_t freq;
                    double p;
                    if (iss >> freq) {
                        iss.ignore(6); // skip " MHz: "
                        iss >> p;
                        iss.ignore(1); // skip '%'
                        out.hw_freq_residency[freq] = clamp_pct(p);
                    } else {
                        iss.clear();
                        iss.ignore(1);
                    }
                }
            }
        }
        else if (line.starts_with("GPU idle residency:")) {
            double pct = 0.0;
            std::sscanf(line.c_str(), "GPU idle residency: %lf%%", &pct);
            out.idle_residency = clamp_pct(pct);
        }
        else if (line.starts_with("GPU Power:")) {
            std::sscanf(line.c_str(), "GPU Power: %llu mW", &out.power_mw);
        }
        else if (line.starts_with("GPU SW state:")) {
            auto l = line.find('(');
            auto r = line.find(')');
            if (l != std::string::npos && r != std::string::npos) {
                std::istringstream iss(line.substr(l + 1, r - l - 1));
                while (!iss.eof()) {
                    std::string lbl;
                    double pct;
                    if (iss >> lbl >> pct) {
                        int idx = std::atoi(lbl.c_str() + 5); // SW_Pn
                        out.sw_states[idx] = clamp_pct(pct);
                        iss.ignore(1); // skip '%'
                    } else {
                        iss.clear();
                        iss.ignore(1);
                    }
                }
            }
        }
    }

    return true;
}

// ----------------------------------------
// Parse CPU text data
// ----------------------------------------
bool Powermetrics::parse_cpu(const std::string &text, CpuInfo &out) const {
    auto clamp_pct = [](double v) -> uint64_t {
        if (v < 0.0) return 0;
        if (v > 100.0) return 100;
        return static_cast<uint64_t>(v + 0.5);
    };

    std::istringstream in(text);
    std::string line;

    uint64_t freq_sum = 0;
    uint64_t freq_count = 0;

    while (std::getline(in, line)) {
        if (line.empty()) continue;

        if (line.starts_with("CPU average active residency:")) {
            double pct = 0.0;
            std::sscanf(line.c_str(), "CPU average active residency: %lf%%", &pct);
            out.active_residency = clamp_pct(pct);
        }
        else if (line.starts_with("CPU idle residency:")) {
            double pct = 0.0;
            std::sscanf(line.c_str(), "CPU idle residency: %lf%%", &pct);
            out.idle_residency = clamp_pct(pct);
        }
        else if (line.starts_with("CPU Power:")) {
            std::sscanf(line.c_str(), "CPU Power: %llu mW", &out.power_mw);
        }
        else if (line.starts_with("CPU ") && line.find(" frequency:") != std::string::npos) {
            uint64_t mhz = 0;
            if (std::sscanf(line.c_str(), "CPU %*d frequency: %llu MHz", &mhz) == 1) {
                freq_sum += mhz;
                freq_count++;
            }
        }
        else if (line.starts_with("CPU frequency:")) {
            uint64_t mhz = 0;
            if (std::sscanf(line.c_str(), "CPU frequency: %llu MHz", &mhz) == 1) {
                freq_sum += mhz;
                freq_count++;
            }
        }
    }

    if (freq_count > 0)
        out.avg_freq_mhz = freq_sum / freq_count;

    return true;
}
