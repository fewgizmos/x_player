#include "X/Engine/Base/JobSystem/Job.hpp"

#include "X/Engine/Util/Base/Assert.hpp"

#include <algorithm>
#include <utility>

namespace Engine::Base::JobSystem
{

namespace
{

bool RunsBefore(const Job& p_A, const Job& p_B)
{
    if (p_A.Order != p_B.Order) {
        return p_A.Order > p_B.Order;
    }

    return p_A.Sequence < p_B.Sequence;
}

void InsertSorted(std::vector<Job>& p_Jobs, Job&& p_Job)
{
    const auto Position = std::upper_bound(p_Jobs.begin(), p_Jobs.end(), p_Job, RunsBefore);

    p_Jobs.insert(Position, std::move(p_Job));
}

}

JobThread::JobThread(Tag p_Tag, bool p_IsAttached, FrameSync& p_Sync)
    : m_Tag(p_Tag)
    , m_IsAttached(p_IsAttached)
    , m_Sync(p_Sync)
{
    if (m_IsAttached) {
        m_ThreadId = std::this_thread::get_id();

        return;
    }

    m_Thread = std::jthread([this](std::stop_token p_Stop) { Loop(p_Stop); });

    m_ThreadId = m_Thread.get_id();
}

void JobThread::QueueAdd(Job p_Job)
{
    std::lock_guard<std::mutex> Lock(m_Mutex);

    p_Job.Sequence = m_NextSequence++;

    m_PendingOps.push_back({ true, std::move(p_Job) });
}

void JobThread::QueueRemove(std::string p_Name)
{
    std::lock_guard<std::mutex> Lock(m_Mutex);

    Job Item;
    Item.Name = std::move(p_Name);

    m_PendingOps.push_back({ false, std::move(Item) });
}

void JobThread::PushBackground(Job p_Job)
{
    X_ASSERT(m_IsAttached, "Background jobs are not supported on the attached thread: " + p_Job.Name);

    {
        std::lock_guard<std::mutex> Lock(m_Mutex);

        p_Job.Sequence = m_NextSequence++;

        InsertSorted(m_Background, std::move(p_Job));
    }

    m_Cv.notify_one();
}

void JobThread::ApplyPending()
{
    std::lock_guard<std::mutex> Lock(m_Mutex);

    for (PendingOp& Op : m_PendingOps) {
        if (Op.IsAdd) {
            InsertSorted(m_Tasks, std::move(Op.Item));

            continue;
        }

        std::erase_if(m_Tasks, [&Op](const Job& p_Candidate) { return p_Candidate.Name == Op.Item.Name; });
    }

    m_PendingOps.clear();
}

void JobThread::StartFrame()
{
    {
        std::lock_guard<std::mutex> Lock(m_Mutex);

        m_FrameRequested = true;
    }

    m_Cv.notify_one();
}

void JobThread::RunFrame()
{
    for (const Job& Item : m_Tasks) {
        Execute(Item.Func);
    }
}

void JobThread::Loop(std::stop_token p_Stop)
{
    std::unique_lock<std::mutex> Lock(m_Mutex);

    while (true) {
        m_Cv.wait(Lock, p_Stop, [this] { return m_FrameRequested || !m_Background.empty(); });

        if (p_Stop.stop_requested()) {
            return;
        }

        if (m_FrameRequested) {
            m_FrameRequested = false;

            Lock.unlock();

            RunFrame();

            FinishFrame();

            Lock.lock();

            continue;
        }

        Job Next = std::move(m_Background.front());

        m_Background.erase(m_Background.begin());

        Lock.unlock();

        Execute(Next.Func);

        Lock.lock();
    }
}

void JobThread::Execute(const Task& p_Func)
{
    if (m_Sync.Stop.load()) {
        return;
    }

    try {
        p_Func();
    }

    catch (...) {
        std::lock_guard<std::mutex> Lock(m_Sync.Mutex);

        if (!m_Sync.Failure) {
            m_Sync.Failure = std::current_exception();
        }

        m_Sync.Stop.store(true);
    }
}

void JobThread::FinishFrame()
{
    {
        std::lock_guard<std::mutex> Lock(m_Sync.Mutex);

        --m_Sync.Pending;
    }

    m_Sync.Cv.notify_all();
}

}
