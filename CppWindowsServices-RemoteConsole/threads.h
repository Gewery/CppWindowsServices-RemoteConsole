#pragma once


class ThreadWrapper
{
private:
    bool bStatus; // if false then handles are non-valid

    static DWORD WINAPI ThreadWrapperStartRoutine(LPVOID param)
    {
        auto p = (ThreadWrapper*)param;
        p->ThreadWorker();
        return 0;
    }

public:
    HANDLE hThread;

    ThreadWrapper(LPVOID threadParam)
    {
        DWORD tid;

        hThreadStopEvent = CreateEvent(
            NULL,    // default security attributes
            TRUE,    // manual reset event
            FALSE,   // not signaled
            NULL);   // no name

        hThread = ::CreateThread(nullptr, 0, ThreadWrapperStartRoutine, this, 0, &tid);
        threadParameter = threadParam;

        bStatus = hThread != INVALID_HANDLE_VALUE;
    }

    // there are soft/hard methods to destroy thread
    void StopThread()
    {
        SetEvent(hThreadStopEvent);
        if (WaitForSingleObject(hThread, 1000) == WAIT_TIMEOUT) {
            TerminateThread(hThread, 0);
        }
        CloseHandle(hThread);
        CloseHandle(hThreadStopEvent);
    }

protected:
    ~ThreadWrapper()
    {
        CloseHandle(hThread);
        CloseHandle(hThreadStopEvent);
    }

    HANDLE hThreadStopEvent;
    LPVOID threadParameter;
    virtual DWORD ThreadWorker() = 0; // abstract
};

class SendingResponsesThread : public ThreadWrapper {
public:
    SendingResponsesThread(LPVOID ThreadParameter) : ThreadWrapper(ThreadParameter) {}
    DWORD ThreadWorker() override;
};

class RecievingRequestsThread : public ThreadWrapper {
public:
    RecievingRequestsThread(LPVOID ThreadParameter) : ThreadWrapper(ThreadParameter) {}
    DWORD ThreadWorker() override;
};