#pragma once
#include "ProjectState.h"
#include <string>
#include <atomic>
#include <memory>
#include <future>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <filesystem>
#include <thread>
#include <map>
#include <vector>
#include <sstream>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>

// RenderJob - representa un trabajo de render determinista
struct RenderJob {
    uint64_t version;           // Version del estado a renderizar
    float speed;                // Speed del snapshot
    int fps;                   // FPS del snapshot
    int width;                 // Width del snapshot
    int height;                // Height del snapshot
    std::string scale_flags;   // Scale flags del snapshot
    bool enable_unsharp;       // Unsharp enable del snapshot
    float unsharp_amount;      // Unsharp amount del snapshot
    float unsharp_radius;      // Unsharp radius del snapshot
    std::string active_file;   // Archivo del snapshot
    std::string output_file;   // Archivo de salida del snapshot
    std::string temp_file;     // Archivo temporal
    RenderMode render_mode;    // Render mode (preview vs export)
    
    RenderJob(uint64_t v, const ProjectState& s)
        : version(v)
        , speed(s.speed)
        , fps(s.fps)
        , width(s.width)
        , height(s.height)
        , scale_flags(s.scale_flags)
        , enable_unsharp(s.enable_unsharp)
        , unsharp_amount(s.unsharp_amount)
        , unsharp_radius(s.unsharp_radius)
        , active_file(s.active_file)
        , output_file(s.output_file)
        , render_mode(s.render_mode) {
        // Generar nombre de archivo temporal manteniendo extensión .mp4
        // Ej: output.mp4 -> output.tmp.1.mp4
        size_t dot_pos = output_file.find_last_of('.');
        if (dot_pos != std::string::npos) {
            std::string base = output_file.substr(0, dot_pos);
            std::string ext = output_file.substr(dot_pos);
            temp_file = base + ".tmp." + std::to_string(v) + ext;
        } else {
            temp_file = output_file + ".tmp." + std::to_string(v);
        }
    }
    
    // Estado del job
    enum class Status {
        PENDING,
        PROCESSING,
        COMPLETED,
        FAILED,
        CANCELLED
    };
    
    std::atomic<Status> status{Status::PENDING};
    std::string error_message;
    pid_t ffmpeg_pid = -1;  // Process ID for FFmpeg subprocess
    std::atomic<bool> cancelled{false};
};

// RenderQueue - cola de jobs con thread-safe operations
class RenderQueue {
public:
    void enqueue(std::shared_ptr<RenderJob> job) {
        std::lock_guard<std::mutex> lock(mutex);
        queue.push(job);
        cv.notify_one();
    }
    
    std::shared_ptr<RenderJob> dequeue() {
        std::unique_lock<std::mutex> lock(mutex);
        cv.wait(lock, [this] { return !queue.empty() || shutdown; });
        
        if (shutdown) return nullptr;
        
        auto job = queue.front();
        queue.pop();
        return job;
    }
    
    void shutdown_queue() {
        std::lock_guard<std::mutex> lock(mutex);
        shutdown = true;
        cv.notify_all();
    }
    
    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex);
        return queue.size();
    }
    
private:
    std::queue<std::shared_ptr<RenderJob>> queue;
    mutable std::mutex mutex;
    std::condition_variable cv;
    bool shutdown = false;
};

// RenderWorker - worker thread para FFmpeg render
class RenderWorker {
public:
    RenderWorker()
        : worker_thread(&RenderWorker::worker_loop, this)
        , monitor_thread(&RenderWorker::monitor_completed_jobs, this) {}

    // Callback for when a render completes successfully
    // WARNING: This is called from the monitor thread, not the main thread!
    // Vulkan operations must be deferred to the main render thread.
    std::function<void(const std::string&)> on_render_complete;

    void perform_atomic_swap(std::shared_ptr<RenderJob> job) {
        // Log original file metadata before swap
        std::cout << "[Render] Original file: " << job->active_file << std::endl;
        std::string probe_cmd = "ffprobe -v error -select_streams v:0 -show_entries stream=codec_name,width,height,r_frame_rate,avg_frame_rate,duration -of default=noprint_wrappers=1 \"" + job->active_file + "\"";
        std::cout << "[Render] Running ffprobe on original: " << probe_cmd << std::endl;
        system(probe_cmd.c_str());

        // Atomic swap: renombrar temp a final
        std::filesystem::rename(job->temp_file, job->output_file);

        // Replace original source file with rendered output
        // This ensures the new version is the "official" version
        if (job->output_file != job->active_file) {
            std::cout << "[Render] Replacing original file: " << job->active_file << " with " << job->output_file << std::endl;
            std::filesystem::rename(job->output_file, job->active_file);

            // Log new file metadata after swap
            std::cout << "[Render] New file: " << job->active_file << std::endl;
            std::string probe_cmd_new = "ffprobe -v error -select_streams v:0 -show_entries stream=codec_name,width,height,r_frame_rate,avg_frame_rate,duration -of default=noprint_wrappers=1 \"" + job->active_file + "\"";
            std::cout << "[Render] Running ffprobe on new file: " << probe_cmd_new << std::endl;
            system(probe_cmd_new.c_str());
        }
    }
    
    ~RenderWorker() {
        // Signal both threads to shutdown
        monitor_shutdown = true;
        worker_shutdown = true;

        // Cancel all running jobs and kill FFmpeg processes
        cancel_all_jobs();
        queue.shutdown_queue();

        if (worker_thread.joinable()) {
            worker_thread.join();
        }
        if (monitor_thread.joinable()) {
            monitor_thread.join();
        }
    }
    
    RenderQueue& get_queue() {
        return queue;
    }

    // Get status information for UI display
    struct RenderStatus {
        size_t active_job_count = 0;
        size_t pending_job_count = 0;
        uint64_t current_version = 0;
        std::string last_error;
    };

    RenderStatus get_status() {
        RenderStatus status;
        std::lock_guard<std::mutex> lock(active_jobs_mutex);
        status.active_job_count = active_jobs.size();
        status.pending_job_count = queue.size();
        status.current_version = g_project_state.get_version();

        // Get last error from any failed job
        for (const auto& [version, job] : active_jobs) {
            if (job->status == RenderJob::Status::FAILED && !job->error_message.empty()) {
                status.last_error = job->error_message;
                break;
            }
        }
        return status;
    }

    // Enqueue un job de render
    std::future<bool> enqueue_job(std::shared_ptr<RenderJob> job) {
        auto promise = std::make_shared<std::promise<bool>>();
        auto future = promise->get_future();
        
        {
            std::lock_guard<std::mutex> lock(futures_mutex);
            futures[job->version] = std::move(promise);
        }
        
        queue.enqueue(job);
        return future;
    }
    
    // Cancel jobs newer than a specific version
    void cancel_jobs_newer_than(uint64_t version) {
        std::lock_guard<std::mutex> lock(active_jobs_mutex);
        for (auto& [job_version, job] : active_jobs) {
            if (job_version > version && job->status == RenderJob::Status::PROCESSING) {
                cancel_job(job);
            }
        }
    }
    
    // Cancel jobs older than a specific version
    void cancel_jobs_older_than(uint64_t version) {
        std::lock_guard<std::mutex> lock(active_jobs_mutex);
        for (auto& [job_version, job] : active_jobs) {
            if (job_version < version && job->status == RenderJob::Status::PROCESSING) {
                cancel_job(job);
            }
        }
    }
    
    // Cancel a specific job
    void cancel_job(std::shared_ptr<RenderJob> job) {
        if (job->ffmpeg_pid != -1) {
            // Kill the FFmpeg subprocess
            kill(job->ffmpeg_pid, SIGTERM);
            job->cancelled = true;
            job->status = RenderJob::Status::CANCELLED;
        }
    }
    
    // Cancel all active jobs
    void cancel_all_jobs() {
        std::lock_guard<std::mutex> lock(active_jobs_mutex);
        for (auto& [version, job] : active_jobs) {
            cancel_job(job);
        }
        active_jobs.clear();
    }
    
private:
    void worker_loop() {
        while (!worker_shutdown) {
            // Esperar a que el estado esté dirty
            if (!g_project_state.is_dirty()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }
            
            // Marcar como clean (para evitar duplicados)
            g_project_state.mark_clean();
            
            // Cancel jobs older than current version (obsolete renders)
            uint64_t current_version = g_project_state.get_version();
            cancel_jobs_older_than(current_version);
            
            // Crear snapshot del estado actual
            ProjectState snapshot;
            snapshot.speed = g_project_state.speed;
            snapshot.fps = g_project_state.fps;
            snapshot.width = g_project_state.width;
            snapshot.height = g_project_state.height;
            snapshot.scale_flags = g_project_state.scale_flags;
            snapshot.enable_unsharp = g_project_state.enable_unsharp;
            snapshot.unsharp_amount = g_project_state.unsharp_amount;
            snapshot.unsharp_radius = g_project_state.unsharp_radius;
            snapshot.active_file = g_project_state.active_file;
            snapshot.output_file = g_project_state.output_file;
            snapshot.render_mode = g_project_state.render_mode;
            snapshot.version.store(current_version);
            
            // Crear job
            auto job = std::make_shared<RenderJob>(current_version, snapshot);
            job->status = RenderJob::Status::PROCESSING;
            
            // Track active job
            {
                std::lock_guard<std::mutex> lock(active_jobs_mutex);
                active_jobs[current_version] = job;
            }
            
            // Ejecutar FFmpeg en subprocess (non-blocking)
            bool success = execute_ffmpeg_render_subprocess(job);

            if (!success) {
                // Failed to spawn process
                job->status = RenderJob::Status::FAILED;
                std::cerr << "[FFmpeg] Failed to spawn process for version " << current_version << std::endl;
                std::lock_guard<std::mutex> lock(active_jobs_mutex);
                active_jobs.erase(current_version);
            } else {
                std::cout << "[FFmpeg] Job started for version " << current_version << " with PID " << job->ffmpeg_pid << std::endl;
            }
            // If success, monitor thread will handle completion
        }
    }
    
    bool execute_ffmpeg_render_subprocess(std::shared_ptr<RenderJob> job) {
        // Construir comando FFmpeg desde el snapshot
        std::string cmd = build_ffmpeg_command(job);

        std::cout << "[FFmpeg] Executing: " << cmd << std::endl;

        // Fork and exec to run FFmpeg in separate process
        pid_t pid = fork();

        if (pid == -1) {
            // Fork failed
            job->error_message = "Failed to fork FFmpeg process";
            std::cerr << "[FFmpeg] Fork failed" << std::endl;
            return false;
        }

        if (pid == 0) {
            // Child process: execute FFmpeg using execlp for safer execution
            // Parse command into arguments
            std::vector<char*> args;
            std::istringstream iss(cmd);
            std::string token;

            while (iss >> token) {
                // Handle quoted strings
                if (token.front() == '"' && token.back() == '"') {
                    token = token.substr(1, token.length() - 2);
                }
                char* arg = strdup(token.c_str());
                args.push_back(arg);
            }
            args.push_back(nullptr);

            // Execute FFmpeg
            execvp(args[0], args.data());

            // If execvp returns, it failed
            _exit(1);
        } else {
            // Parent process: store PID and return immediately (non-blocking)
            job->ffmpeg_pid = pid;
            std::cout << "[FFmpeg] Spawned with PID: " << pid << std::endl;
            return true;
        }
    }
    
    // Monitor thread to wait for completed FFmpeg processes
    void monitor_completed_jobs() {
        while (!monitor_shutdown) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            
            std::vector<uint64_t> completed_versions;
            
            {
                std::lock_guard<std::mutex> lock(active_jobs_mutex);
                for (auto& [version, job] : active_jobs) {
                    if (job->ffmpeg_pid != -1) {
                        // Check if process has completed
                        int status;
                        pid_t result = waitpid(job->ffmpeg_pid, &status, WNOHANG);
                        
                        if (result == -1) {
                            // Error waiting for process
                            job->error_message = "Failed to wait for FFmpeg process";
                            job->status = RenderJob::Status::FAILED;
                            job->ffmpeg_pid = -1;
                            completed_versions.push_back(version);
                        } else if (result == job->ffmpeg_pid) {
                            // Process has completed
                            job->ffmpeg_pid = -1;
                            
                            if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
                                if (!job->cancelled) {
                                    // Atomic swap: temp -> final
                                    perform_atomic_swap(job);
                                    job->status = RenderJob::Status::COMPLETED;

                                    // Notify callback that render completed
                                    // Pass the original file path (which now contains the new version)
                                    if (on_render_complete) {
                                        on_render_complete(job->active_file);
                                    }

                                    // Removed auto-increment to prevent infinite render loop
                                    // Version should only be incremented on user-initiated state changes
                                }
                            } else {
                                job->status = RenderJob::Status::FAILED;
                            }
                            completed_versions.push_back(version);
                        }
                    }
                }
                
                // Remove completed jobs from active map
                for (uint64_t version : completed_versions) {
                    active_jobs.erase(version);
                }
            }
        }
    }
    
    std::string build_ffmpeg_command(std::shared_ptr<RenderJob> job) {
        std::string cmd = "ffmpeg -i \"" + job->active_file + "\"";

        // Agregar efectos del job
        std::string filter = build_filter_from_job(job);
        if (!filter.empty()) {
            cmd += " -vf \"" + filter + "\"";
        }

        // Codec settings
        cmd += " -c:v libx264";
        cmd += " -crf 18";

        // Use faster preset for preview mode
        if (job->render_mode == RenderMode::PREVIEW) {
            cmd += " -preset veryfast";
        } else {
            cmd += " -preset slow";
        }

        // Only set -r if fps is explicitly set (not auto-detect)
        if (job->fps > 0) {
            cmd += " -r " + std::to_string(job->fps);
        }

        // Output temporal
        cmd += " \"" + job->temp_file + "\"";

        return cmd;
    }
    
    std::string build_filter_from_job(std::shared_ptr<RenderJob> job) {
        std::string filter;
        bool first = true;

        // FPS interpolation - PRIMERO (only if explicitly set)
        if (job->fps > 0) {
            if (job->render_mode == RenderMode::PREVIEW) {
                // Preview mode: simple fps duplication (much faster)
                filter += "fps=" + std::to_string(job->fps);
            } else {
                // Export mode: use expensive minterpolate (optical flow)
                filter += "minterpolate=fps=" + std::to_string(job->fps);
            }
            first = false;
        }

        // Speed (setpts) - SEGUNDO
        if (job->speed != 1.0f) {
            if (!first) filter += ",";
            filter += "setpts=" + std::to_string(job->speed) + "*PTS";
            first = false;
        }

        // Scale - TERCERO (only if explicitly set)
        if (job->width > 0 && job->height > 0) {
            if (!first) filter += ",";
            filter += "scale=" + std::to_string(job->width) + ":" + std::to_string(job->height);
            if (!job->scale_flags.empty()) {
                filter += ":flags=" + job->scale_flags;
            }
            first = false;
        }

        // Unsharp filter - CUARTO
        if (job->enable_unsharp) {
            if (!first) filter += ",";
            // unsharp=5:5:0.7:3:3:0.3 format
            filter += "unsharp=" + std::to_string(job->unsharp_radius) + ":" +
                     std::to_string(job->unsharp_radius) + ":" +
                     std::to_string(job->unsharp_amount) + ":3:3:0.3";
        }

        return filter;
    }

private:
    RenderQueue queue;
    std::thread worker_thread;
    std::thread monitor_thread;

    std::mutex futures_mutex;
    std::map<uint64_t, std::shared_ptr<std::promise<bool>>> futures;

    std::mutex active_jobs_mutex;
    std::map<uint64_t, std::shared_ptr<RenderJob>> active_jobs;

    std::atomic<bool> monitor_shutdown{false};
    std::atomic<bool> worker_shutdown{false};
};
