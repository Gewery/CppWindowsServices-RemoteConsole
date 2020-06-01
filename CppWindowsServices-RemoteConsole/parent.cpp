//#undef UNICODE

#define WIN32_LEAN_AND_MEAN

#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdlib.h>

#include <windows.h> 
#include <tchar.h>
#include <stdio.h> 
#include <strsafe.h>
#include "sample.h"
#include "parent.h"
#include "threads.h"

// Need to link with Ws2_32.lib
#pragma comment (lib, "Ws2_32.lib")
// #pragma comment (lib, "Mswsock.lib")

#define DEFAULT_BUFLEN 512
#define DEFAULT_OUTPUT_PORT "27016"
#define DEFAULT_INPUT_PORT "27015"
#define BUFSIZE 4096 

#define CMDPATH "C:\\Windows\\System32\\cmd.exe"
#define LOGPATH "C:\\Users\\Danya\\Documents\\CppWindowsServices-RemoteConsole\\RemoteConsole.log"

VOID WINAPI SvcMain(DWORD dwArgc, LPTSTR* lpszArgv);
VOID LogError(LPCTSTR szFunction);
VOID WINAPI SvcCtrlHandler(DWORD dwCtrl);
VOID SvcInstall();

#define PIPE_FILE_NAME (LPCWSTR)("input.txt")

FILE* gLog;
using namespace std;

#define SVCNAME TEXT("AAA_RemoteConsole")

class RemoteConsoleService {
private:
    HANDLE g_hChildStd_IN_Rd = NULL;
    HANDLE g_hChildStd_IN_Wr = NULL;
    HANDLE g_hChildStd_OUT_Rd = NULL;
    HANDLE g_hChildStd_OUT_Wr = NULL;

    HANDLE g_hInputFile = NULL;

    SERVICE_STATUS          gSvcStatus;
    SERVICE_STATUS_HANDLE   gSvcStatusHandle;
    HANDLE                  ghSvcStopEvent = NULL;
    HANDLE hEventSource;

    SOCKET sClientInputSocket;
    SOCKET sClientOutputSocket;

    PROCESS_INFORMATION piProcInfo; // For creation child process
    STARTUPINFO siStartInfo;

    //std::unique_ptr<SendingResponsesThread> responsesThread;
    class SendingResponsesThread* responsesThread = nullptr;
    class RecievingRequestsThread* requestsThread = nullptr;

    //
    // Purpose: 
    //   Sets the current service status and reports it to the SCM.
    //
    // Parameters:
    //   dwCurrentState - The current state (see SERVICE_STATUS)
    //   dwWin32ExitCode - The system error code
    //   dwWaitHint - Estimated time for pending operation, 
    //     in milliseconds
    // 
    // Return value:
    //   None
    //
    VOID ReportSvcStatus(DWORD dwCurrentState,
        DWORD dwWin32ExitCode,
        DWORD dwWaitHint)
    {
        static DWORD dwCheckPoint = 1;

        // Fill in the SERVICE_STATUS structure.

        gSvcStatus.dwCurrentState = dwCurrentState;
        gSvcStatus.dwWin32ExitCode = dwWin32ExitCode;
        gSvcStatus.dwWaitHint = dwWaitHint;

        if (dwCurrentState == SERVICE_START_PENDING)
            gSvcStatus.dwControlsAccepted = 0;
        else gSvcStatus.dwControlsAccepted = SERVICE_ACCEPT_STOP;

        if ((dwCurrentState == SERVICE_RUNNING) ||
            (dwCurrentState == SERVICE_STOPPED))
            gSvcStatus.dwCheckPoint = 0;
        else gSvcStatus.dwCheckPoint = dwCheckPoint++;

        // Report the status of the service to the SCM.
        SetServiceStatus(gSvcStatusHandle, &gSvcStatus);
    }

    //
    // Purpose: 
    //   Logs messages about Errors into the event log with Error severity
    //
    // Parameters:
    //   szFunction - name of function that failed
    // 
    // Return value:
    //   None
    //
    // Remarks:
    //   The service must have an entry in the Application event log.
    //
    VOID SvcReportError(LPTSTR szFunction)
    {
        LPCTSTR lpszStrings[2];
        TCHAR Buffer[80];

        hEventSource = RegisterEventSource(NULL, SVCNAME);

        if (NULL != hEventSource)
        {
            StringCchPrintf(Buffer, 80, TEXT("%s failed with %s"), szFunction, GetLastError());

            lpszStrings[0] = SVCNAME;
            lpszStrings[1] = Buffer;

            ReportEvent(hEventSource,        // event log handle
                EVENTLOG_ERROR_TYPE, // event type
                0,                   // event category
                SVC_ERROR,           // event identifier
                NULL,                // no security identifier
                2,                   // size of lpszStrings array
                0,                   // no binary data
                lpszStrings,         // array of strings
                NULL);               // no binary data

            DeregisterEventSource(hEventSource);
        }
    }

    //
    // Purpose: 
    //   Logs information to the event log with Notice severity
    //
    // Parameters:
    //   message - information to place into the log
    // 
    // Return value:
    //   None
    //
    // Remarks:
    //   The service must have an entry in the Application event log.
    //
    VOID SvcReportLogInfo()
    {
        LPCTSTR lpszStrings[2];
        TCHAR Buffer[80];

        hEventSource = RegisterEventSource(NULL, SVCNAME);

        if (NULL != hEventSource)
        {
            StringCchPrintf(Buffer, 100, TEXT("The log is placed at: %s"), TEXT(LOGPATH));

            lpszStrings[0] = SVCNAME;
            lpszStrings[1] = Buffer;

            ReportEvent(hEventSource,        // event log handle
                EVENTLOG_SUCCESS, // event type
                0,                   // event category
                SVC_NOTICE,           // event identifier
                NULL,                // no security identifier
                2,                   // size of lpszStrings array
                0,                   // no binary data
                lpszStrings,         // array of strings
                NULL);               // no binary data

            DeregisterEventSource(hEventSource);
        }
    }

    // Closes all the handles that service could use
    void CloseServiceHandles() {
        CloseHandle(g_hChildStd_IN_Rd);
        CloseHandle(g_hChildStd_IN_Wr);
        CloseHandle(g_hChildStd_OUT_Rd);
        CloseHandle(g_hChildStd_OUT_Wr);
        CloseHandle(g_hInputFile);
        CloseHandle(ghSvcStopEvent);
        CloseHandle(hEventSource);
        CloseHandle(piProcInfo.hProcess);
        CloseHandle(piProcInfo.hThread);
        CloseHandle(siStartInfo.hStdError);
        CloseHandle(siStartInfo.hStdInput);
        CloseHandle(siStartInfo.hStdOutput);
    }

public:
    RemoteConsoleService() {
        fopen_s(&gLog, LOGPATH, "a+");
        SvcReportLogInfo(); // Send path of a log file

        // Register the handler function for the service

        gSvcStatusHandle = RegisterServiceCtrlHandler(
            SVCNAME,
            SvcCtrlHandler);

        if (!gSvcStatusHandle)
        {
            SvcReportError((LPTSTR)(TEXT("RegisterServiceCtrlHandler")));
            return;
        }

        // Create an event. The control handler function, SvcCtrlHandler,
        // signals this event when it receives the stop control code.

        ghSvcStopEvent = CreateEvent(
            NULL,    // default security attributes
            TRUE,    // manual reset event
            FALSE,   // not signaled
            NULL);   // no name

        if (ghSvcStopEvent == NULL)
        {
            ReportSvcStatus(SERVICE_STOPPED, NO_ERROR, 0);
            return;
        }

        // These SERVICE_STATUS members remain as set here

        gSvcStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
        gSvcStatus.dwServiceSpecificExitCode = 0;

        // Report initial status to the SCM

        ReportSvcStatus(SERVICE_START_PENDING, NO_ERROR, 3000);

        // Set the bInheritHandle flag so pipe handles are inherited. 
        SECURITY_ATTRIBUTES saAttr;
        
        saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
        saAttr.bInheritHandle = TRUE;
        saAttr.lpSecurityDescriptor = NULL;

        // Create a pipe for the child process's STDOUT. 

        if (!CreatePipe(&g_hChildStd_OUT_Rd, &g_hChildStd_OUT_Wr, &saAttr, 0))
            LogError(TEXT("StdoutRd CreatePipe"));

        // Ensure the read handle to the pipe for STDOUT is not inherited.

        if (!SetHandleInformation(g_hChildStd_OUT_Rd, HANDLE_FLAG_INHERIT, 0))
            LogError(TEXT("Stdout SetHandleInformation"));

        // Create a pipe for the child process's STDIN. 

        if (!CreatePipe(&g_hChildStd_IN_Rd, &g_hChildStd_IN_Wr, &saAttr, 0))
            LogError(TEXT("Stdin CreatePipe"));

        // Ensure the write handle to the pipe for STDIN is not inherited. 

        if (!SetHandleInformation(g_hChildStd_IN_Wr, HANDLE_FLAG_INHERIT, 0))
            LogError(TEXT("Stdin SetHandleInformation"));
    }

    void run() {
        ReportSvcStatus(SERVICE_RUNNING, NO_ERROR, 0);

        LogError(TEXT("\n->Start of parent execution.\n"));

        // Create the child process. 

        CreateChildProcess();

        // Set up sockets for connection with client
        sClientInputSocket = INVALID_SOCKET;
        if (SocketSetUp(sClientInputSocket, (char*)DEFAULT_INPUT_PORT))
            LogError(TEXT("Input Socket SetUp"));

        sClientOutputSocket = INVALID_SOCKET;
        if (SocketSetUp(sClientOutputSocket, (char*)DEFAULT_OUTPUT_PORT))
            LogError(TEXT("Output Socket SetUp"));

        LogError(TEXT("Sockets setted up\n"));

        // Setting up threads for communication with client
        responsesThread = new SendingResponsesThread((LPVOID)sClientOutputSocket);
        requestsThread = new RecievingRequestsThread((LPVOID)sClientInputSocket);

        HANDLE handles[4];
        handles[0] = responsesThread->hThread;
        handles[1] = requestsThread->hThread;
        handles[2] = ghSvcStopEvent;
        handles[3] = piProcInfo.hProcess;

        DWORD res = WaitForMultipleObjects(4, handles, false, INFINITE);

        TerminateProcess(piProcInfo.hProcess, 0);
        responsesThread->StopThread();
        requestsThread->StopThread();

        if (res - WAIT_OBJECT_0 <= 1 && WaitForSingleObject(ghSvcStopEvent, 0) != WAIT_OBJECT_0) {
            LogError(TEXT("Restarting..."));
            run();
        }

        LogError(TEXT("\n->End of parent execution."));
        StopService();
        return;
    }

    void StopService() {
        ReportSvcStatus(SERVICE_STOP_PENDING, NO_ERROR, 0);

        SetEvent(gpService->ghSvcStopEvent); // signal service to stop

        if (WaitForSingleObject(piProcInfo.hProcess, 1000) == WAIT_TIMEOUT) TerminateProcess(piProcInfo.hProcess, 0);
        responsesThread->StopThread();
        requestsThread->StopThread();

        CloseConnection(sClientOutputSocket);
        CloseConnection(sClientInputSocket);
        CloseServiceHandles();
        ReportSvcStatus(SERVICE_STOPPED, NO_ERROR, 0);
    }

    ~RemoteConsoleService() {
        CloseConnection(sClientInputSocket);
        CloseConnection(sClientOutputSocket);
        CloseServiceHandles();
    }

    void CreateChildProcess()
        // Create a child process that uses the previously created pipes for STDIN and STDOUT.
    {
        TCHAR szCmdline[] = TEXT(CMDPATH);
        BOOL bSuccess = FALSE;

        // Set up members of the PROCESS_INFORMATION structure. 

        ZeroMemory(&piProcInfo, sizeof(PROCESS_INFORMATION));

        // Set up members of the STARTUPINFO structure. 
        // This structure specifies the STDIN and STDOUT handles for redirection.

        ZeroMemory(&siStartInfo, sizeof(STARTUPINFO));
        siStartInfo.cb = sizeof(STARTUPINFO);
        siStartInfo.hStdError = g_hChildStd_OUT_Wr;
        siStartInfo.hStdOutput = g_hChildStd_OUT_Wr;
        siStartInfo.hStdInput = g_hChildStd_IN_Rd;
        siStartInfo.dwFlags |= STARTF_USESTDHANDLES;

        // Create the child process. 

        bSuccess = CreateProcess(NULL,
            szCmdline,     // command line 
            NULL,          // process security attributes 
            NULL,          // primary thread security attributes 
            TRUE,          // handles are inherited 
            0,             // creation flags 
            NULL,          // use parent's environment 
            NULL,          // use parent's current directory 
            &siStartInfo,  // STARTUPINFO pointer 
            &piProcInfo);  // receives PROCESS_INFORMATION 

         // If an error occurs, exit the application. 
        if (!bSuccess)
            LogError(TEXT("CreateProcess"));
        else
        {
            // Close handles to the child process and its primary thread.
            // Some applications might keep these handles to monitor the status
            // of the child process, for example. 

            CloseHandle(piProcInfo.hProcess);
            CloseHandle(piProcInfo.hThread);
        }
    }

    void WriteToPipe(CHAR* message, DWORD dwRead)
        // Write contents of mesage to the pipe for the child's STDIN.
        // Stop when there is no more data. 
    {
        DWORD dwWritten;
        BOOL bSuccess = FALSE;

        bSuccess = WriteFile(g_hChildStd_IN_Wr, message, dwRead, &dwWritten, NULL);
        if (!bSuccess) 
            LogError(TEXT("WriteFile"));
    }
    // TODO: choose bool or void
    bool ReadFromPipe(CHAR* message, DWORD* dwRead)
    {
        BOOL bSuccess = FALSE;

        bSuccess = ReadFile(g_hChildStd_OUT_Rd, message, DEFAULT_BUFLEN, dwRead, NULL);

        return bSuccess && *dwRead != 0;
    }

    int SocketSetUp(SOCKET& ClientSocket, char* port) {
        WSADATA wsaData;
        int iResult;

        SOCKET ListenSocket = INVALID_SOCKET;

        struct addrinfo* result = NULL;
        struct addrinfo hints;

        // Initialize Winsock
        iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
        if (iResult != 0) {
            printf("WSAStartup failed with error: %d\n", iResult);
            return 1;
        }

        ZeroMemory(&hints, sizeof(hints));
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;
        hints.ai_flags = AI_PASSIVE;

        // Resolve the server address and port
        iResult = getaddrinfo(NULL, port, &hints, &result);
        if (iResult != 0) {
            printf("getaddrinfo failed with error: %d\n", iResult);
            WSACleanup();
            return 1;
        }

        // Create a SOCKET for connecting to server
        ListenSocket = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
        if (ListenSocket == INVALID_SOCKET) {
            printf("socket failed with error: %ld\n", WSAGetLastError());
            freeaddrinfo(result);
            WSACleanup();
            return 1;
        }

        // Setup the TCP listening socket
        iResult = bind(ListenSocket, result->ai_addr, (int)result->ai_addrlen);
        if (iResult == SOCKET_ERROR) {
            printf("bind failed with error: %d\n", WSAGetLastError());
            freeaddrinfo(result);
            closesocket(ListenSocket);
            WSACleanup();
            return 1;
        }

        freeaddrinfo(result);

        printf("listening for connection...\n");

        iResult = listen(ListenSocket, SOMAXCONN);
        if (iResult == SOCKET_ERROR) {
            printf("listen failed with error: %d\n", WSAGetLastError());
            closesocket(ListenSocket);
            WSACleanup();
            return 1;
        }

        // Accept a client socket
        ClientSocket = accept(ListenSocket, NULL, NULL);
        if (ClientSocket == INVALID_SOCKET) {
            printf("accept failed with error: %d\n", WSAGetLastError());
            closesocket(ListenSocket);
            WSACleanup();
            return 1;
        }

        printf("connection accepted\n");

        // No longer need server socket
        closesocket(ListenSocket);

        return 0;
    }

    int CloseConnection(SOCKET ClientSocket) {
        int iResult;
        // shutdown the connection since we're done
        iResult = shutdown(ClientSocket, SD_SEND);
        if (iResult == SOCKET_ERROR) {
            printf("shutdown failed with error: %d\n", WSAGetLastError());
            closesocket(ClientSocket);
            WSACleanup();
            return 1;
        }

        // cleanup
        closesocket(ClientSocket);
        WSACleanup();

        return 0;
    }

    int RecieveMessage(SOCKET ClientSocket, char* recvbuf, int recvbuflen, int* bytesrecieved) {
        int iResult, iSendResult;

        *bytesrecieved = recv(ClientSocket, recvbuf, recvbuflen, 0);
        if (*bytesrecieved > 0) {
            printf("Bytes received: %d\n", *bytesrecieved);
        }
        else if (*bytesrecieved == 0)
            printf("Connection closing...\n");
        else {
            //closesocket(ClientSocket);
            return 1;
        }

        return 0;
    }

    int SendMessage(SOCKET ClientSocket, char* sendbuf, int sendbuflen) {
        // Echo the buffer back to the sender
        int iSendResult;
        iSendResult = send(ClientSocket, sendbuf, sendbuflen, 0);
        if (iSendResult == SOCKET_ERROR) {
            closesocket(ClientSocket);
            return 1;
        }
        printf("Bytes sent: %d\n", iSendResult);

        return 0;
    }

} *gpService;


DWORD SendingResponsesThread::ThreadWorker() {
    LogError(TEXT("sendingResponses started"));
    char childbuf[DEFAULT_BUFLEN];
    DWORD childbuflen;
    while (gpService->ReadFromPipe(childbuf, &childbuflen)) {
        LogError(TEXT("sending something to client"));
        gpService->SendMessage((SOCKET)this->threadParameter, childbuf, childbuflen);
        memset(childbuf, 0, sizeof childbuf);
        //printf("->%d bytes sent to the client\n", childbuflen);
        if (WaitForSingleObject(this->hThreadStopEvent, 0) == WAIT_OBJECT_0) {
            break;
        }
    }
    LogError(TEXT("sendingResponses finished"));
    return 0;
}


DWORD RecievingRequestsThread::ThreadWorker() {
    LogError(TEXT("recievingRequests started"));
    char recvbuf[DEFAULT_BUFLEN];
    int recvbuflen = DEFAULT_BUFLEN;
    int bytesRecieved;
    do {
        if (gpService->RecieveMessage((SOCKET)this->threadParameter, recvbuf, recvbuflen, &bytesRecieved)) //TODO: Recieve all the messages
            LogError(TEXT("RecieveMessage"));

        LogError(TEXT("message recieved"));

        gpService->WriteToPipe(recvbuf, bytesRecieved);

        if (WaitForSingleObject(this->hThreadStopEvent, 0) == WAIT_OBJECT_0) {
            break;
        }
        //printf("\n->Message %s written to child STDIN pipe.\n", recvbuf);//argv[1]);
    } while (bytesRecieved > 0);
    LogError(TEXT("recievingResponses finished"));
    return 0;
}

//
// Purpose: 
//   Entry point for the process
//
// Parameters:
//   None
// 
// Return value:
//   None
//
void __cdecl service(int argc, TCHAR* argv[])
{
    // If command-line parameter is "install", install the service. 
    // Otherwise, the service is probably being started by the SCM.
    if (lstrcmpi(argv[1], TEXT("install")) == 0)
    {
        SvcInstall();
        return;
    }


    SERVICE_TABLE_ENTRY DispatchTable[] =
    {
        { (LPWSTR)(SVCNAME), (LPSERVICE_MAIN_FUNCTION)SvcMain },
        { NULL, NULL }
    };

    if (!StartServiceCtrlDispatcher(DispatchTable))
    {
        LogError(TEXT("StartServiceCtrlDispatcher"));
        return;
    }
}

//
// Purpose: 
//   Entry point for the service
//
// Parameters:
//   dwArgc - Number of arguments in the lpszArgv array
//   lpszArgv - Array of strings. The first string is the name of
//     the service and subsequent strings are passed by the process
//     that called the StartService function to start the service.
// 
// Return value:
//   None.
//
VOID WINAPI SvcMain(DWORD dwArgc, LPTSTR* lpszArgv)
{
    gpService = new RemoteConsoleService();
    LogError(TEXT("Initialized\n"));
    gpService->run();
}

VOID LogError(LPCTSTR szFunction)
{
#define buffsize 160
    wchar_t buffer[buffsize];
    StringCchPrintf(buffer, buffsize, L"%s failed with %d", szFunction, GetLastError());
    if (gLog)
    {
        fprintf(gLog, "%ls \n", buffer);
    }
}

//
// Purpose: 
//   Called by SCM whenever a control code is sent to the service
//   using the ControlService function.
//
// Parameters:
//   dwCtrl - control code
// 
// Return value:
//   None
//
VOID WINAPI SvcCtrlHandler(DWORD dwCtrl)
{
    // Handle the requested control code. 

    switch (dwCtrl)
    {
    case SERVICE_CONTROL_STOP:
        gpService->StopService();
        return;

    case SERVICE_CONTROL_INTERROGATE:
        break;

    default:
        break;
    }
}

//
// Purpose: 
//   Installs a service in the SCM database
//
// Parameters:
//   None
// 
// Return value:
//   None
//
VOID SvcInstall()
{
    SC_HANDLE schSCManager;
    SC_HANDLE schService;
    TCHAR szPath[MAX_PATH];

    if (!GetModuleFileName(nullptr, szPath, MAX_PATH))
    {
        printf("Cannot install service (%d)\n", GetLastError());
        return;
    }

    // Get a handle to the SCM database. 

    schSCManager = OpenSCManager(
        NULL,                    // local computer
        NULL,                    // ServicesActive database 
        SC_MANAGER_ALL_ACCESS);  // full access rights 

    if (NULL == schSCManager)
    {
        printf("OpenSCManager failed (%d)\n", GetLastError());
        return;
    }

    // Create the service

    schService = CreateService(
        schSCManager,              // SCM database 
        SVCNAME,                   // name of service 
        SVCNAME,                   // service name to display 
        SERVICE_ALL_ACCESS,        // desired access 
        SERVICE_WIN32_OWN_PROCESS, // service type 
        SERVICE_DEMAND_START,      // start type 
        SERVICE_ERROR_NORMAL,      // error control type 
        szPath,                    // path to service's binary 
        NULL,                      // no load ordering group 
        NULL,                      // no tag identifier 
        NULL,                      // no dependencies 
        NULL,                      // LocalSystem account 
        NULL);                     // no password 

    if (schService == NULL)
    {
        printf("CreateService failed (%d)\n", GetLastError());
        CloseServiceHandle(schSCManager);
        return;
    }
    else printf("Service installed successfully\n");

    CloseServiceHandle(schService);
    CloseServiceHandle(schSCManager);
}