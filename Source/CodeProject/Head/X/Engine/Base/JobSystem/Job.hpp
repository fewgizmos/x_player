#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <exception>
#include <functional>
#include <mutex>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>

namespace Engine::Base::JobSystem
{

using Tag  = unsigned char;
using Task = std::function<void()>;

inline constexpr int DEFAULT_ORDER = 50;

struct Job
{
    std::string Name;

    int Order = DEFAULT_ORDER;

    Task Func;

    std::uint64_t Sequence = 0;
};

struct FrameSync
{
    std::mutex Mutex;
    std::condition_variable Cv;

    int Pending = 0;

    std::exception_ptr Failure;

    std::atomic<bool> Stop = false;
};

class JobThread
{
public:
    JobThread(Tag p_Tag, bool p_IsAttached, FrameSync& p_Sync);

    JobThread(const JobThread&) = delete;
    JobThread& operator=(const JobThread&) = delete;

    Tag GetTag() const { return m_Tag; }

    bool IsAttached() const { return m_IsAttached; }

    std::thread::id GetThreadId() const { return m_ThreadId; }

    bool HasFrameTasks() const { return !m_Tasks.empty(); }

    void QueueAdd(Job p_Job);
    void QueueRemove(std::string p_Name);

    void PushBackground(Job p_Job);

    void ApplyPending();

    void StartFrame();
    void RunFrame();

private:
    struct PendingOp
    {
        bool IsAdd = false;

        Job Item;
    };

    void Loop(std::stop_token p_Stop);

    void Execute(const Task& p_Func);

    void FinishFrame();

    Tag m_Tag;
    bool m_IsAttached;

    FrameSync& m_Sync;

    std::thread::id m_ThreadId;

    std::mutex m_Mutex;
    std::condition_variable_any m_Cv;

    std::vector<Job> m_Tasks;
    std::vector<PendingOp> m_PendingOps;
    std::vector<Job> m_Background;

    std::uint64_t m_NextSequence = 0;

    bool m_FrameRequested = false;

    std::jthread m_Thread;
};

}
