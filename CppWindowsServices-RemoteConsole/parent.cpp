//#undef UNICODE

#define WIN32_LEAN_AND_MEAN

#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdlib.h>

#include <windows.h> 
#include <tchar.h>
#include <stdio.h> 
#include <strsafe.h>

#include "parent.h"

// Need to link with Ws2_32.lib
#pragma comment (lib, "Ws2_32.lib")
// #pragma comment (lib, "Mswsock.lib")

#define DEFAULT_BUFLEN 512
#define DEFAULT_PORT "27015"


#define BUFSIZE 4096 

HANDLE g_hChildStd_IN_Rd = NULL;
HANDLE g_hChildStd_IN_Wr = NULL;
HANDLE g_hChildStd_OUT_Rd = NULL;
HANDLE g_hChildStd_OUT_Wr = NULL;

HANDLE g_hInputFile = NULL;

void CreateChildProcess(void);
void WriteToPipe(CHAR* message, DWORD dwRead);
bool ReadFromPipe(CHAR* message, DWORD* dwRead);
void ErrorExit(PCTSTR);
int SocketSetUp(SOCKET& ClientSocket);
int RecieveMessage(SOCKET ClientSocket, char* recvbuf, int recvbuflen);
int SendMessage(SOCKET ClientSocket, char* recvbuf, int recvbuflen);
int CloseConnection(SOCKET ClientSocket);

#define PIPE_FILE_NAME (LPCWSTR)("input.txt")

int parent(int argc, TCHAR* argv[])
{
    SECURITY_ATTRIBUTES saAttr;

    printf("\n->Start of parent execution.\n");

    // Set the bInheritHandle flag so pipe handles are inherited. 

    saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
    saAttr.bInheritHandle = TRUE;
    saAttr.lpSecurityDescriptor = NULL;

    // Create a pipe for the child process's STDOUT. 

    if (!CreatePipe(&g_hChildStd_OUT_Rd, &g_hChildStd_OUT_Wr, &saAttr, 0))
        ErrorExit(TEXT("StdoutRd CreatePipe"));

    // Ensure the read handle to the pipe for STDOUT is not inherited.

    if (!SetHandleInformation(g_hChildStd_OUT_Rd, HANDLE_FLAG_INHERIT, 0))
        ErrorExit(TEXT("Stdout SetHandleInformation"));

    // Create a pipe for the child process's STDIN. 

    if (!CreatePipe(&g_hChildStd_IN_Rd, &g_hChildStd_IN_Wr, &saAttr, 0))
        ErrorExit(TEXT("Stdin CreatePipe"));

    // Ensure the write handle to the pipe for STDIN is not inherited. 

    if (!SetHandleInformation(g_hChildStd_IN_Wr, HANDLE_FLAG_INHERIT, 0))
        ErrorExit(TEXT("Stdin SetHandleInformation"));

    // Create the child process. 

    CreateChildProcess();

    printf("file name: %s\n", PIPE_FILE_NAME);

    // Get a handle to an input file for the parent. 
    // This example assumes a plain text file and uses string output to verify data flow. 

    //if (argc == 1)
    //    ErrorExit(TEXT("Please specify an input file.\n"));

    g_hInputFile = GetStdHandle(STD_INPUT_HANDLE);

    //g_hInputFile = CreateFile(
    //    PIPE_FILE_NAME,//argv[1],
    //    GENERIC_READ,
    //    0,
    //    NULL,
    //    OPEN_EXISTING,
    //    FILE_ATTRIBUTE_READONLY,
    //    NULL);

    if (g_hInputFile == INVALID_HANDLE_VALUE)
        ErrorExit(TEXT("CreateFile"));

    // Write to the pipe that is the standard input for a child process. 
    // Data is written to the pipe's buffers, so it is not necessary to wait
    // until the child process is running before writing data.

    SOCKET ClientSocket = INVALID_SOCKET;
    if (SocketSetUp(ClientSocket))
        ErrorExit(TEXT("Socket SetUp"));

    printf("Socket setted up\n");

    char recvbuf[DEFAULT_BUFLEN];
    int recvbuflen = DEFAULT_BUFLEN;

    if (RecieveMessage(ClientSocket, recvbuf, recvbuflen))
        ErrorExit(TEXT("RecieveMessage"));

    printf("message recieved: %s\n", recvbuf);

    //DWORD dwRead;
    //bool bSuccess = ReadFile(g_hInputFile, recvbuf, BUFSIZE, &dwRead, NULL);
    //if (!bSuccess || dwRead == 0) return 0;


    printf("\nDEBUG recvbuflen: %d\n", recvbuflen);

    WriteToPipe(recvbuf, recvbuflen);
    printf("\n->Message %s written to child STDIN pipe.\n", recvbuf);//argv[1]);

    // Read from pipe that is the standard output for child process. 

    char childbuf[DEFAULT_BUFLEN];
    DWORD childbuflen;
    while (ReadFromPipe(childbuf, &childbuflen)) {
        SendMessage(ClientSocket, childbuf, childbuflen);
        memset(childbuf, 0, sizeof childbuf);
        printf("->%d bytes sent to the client\n", childbuflen);
    }

    CloseConnection(ClientSocket);

    printf("\n->End of parent execution.\n");

    // The remaining open handles are cleaned up when this process terminates. 
    // To avoid resource leaks in a larger application, close handles explicitly. 

    return 0;
}

int RecieveMessage(SOCKET ClientSocket, char *recvbuf, int recvbuflen) {
    int iResult, iSendResult;

    iResult = recv(ClientSocket, recvbuf, recvbuflen, 0);
    if (iResult > 0) {
        printf("Bytes received: %d\n", iResult);
    }
    else if (iResult == 0)
        printf("Connection closing...\n");
    else {
        printf("recv failed with error: %d\n", WSAGetLastError());
        closesocket(ClientSocket);
        WSACleanup();
        return 1;
    }

    return 0;
}

int SendMessage(SOCKET ClientSocket, char* recvbuf, int recvbuflen) {
    // Echo the buffer back to the sender
    int iSendResult;
    iSendResult = send(ClientSocket, recvbuf, recvbuflen, 0);
    if (iSendResult == SOCKET_ERROR) {
        printf("send failed with error: %d\n", WSAGetLastError());
        closesocket(ClientSocket);
        WSACleanup();
        return 1;
    }
    printf("Bytes sent: %d\n", iSendResult);

    return 0;
}

void WriteToPipe(CHAR *message, DWORD dwRead)

// Read from a file and write its contents to the pipe for the child's STDIN.
// Stop when there is no more data. 
{
    DWORD dwWritten;
    BOOL bSuccess = FALSE;

    for (;;)
    {
        //bSuccess = ReadFile(g_hInputFile, message, dwRead, &dwRead, NULL); // input from console
        //if (!bSuccess || dwRead == 0) break;

        bSuccess = WriteFile(g_hChildStd_IN_Wr, message, dwRead, &dwWritten, NULL);
        if (!bSuccess) break;

        break; //TODO remove this
    }

    // Close the pipe handle so the child process stops reading. 

    if (!CloseHandle(g_hChildStd_IN_Wr))
        ErrorExit(TEXT("StdInWr CloseHandle"));
}

bool ReadFromPipe(CHAR* message, DWORD *dwRead)

// Read output from the child process's pipe for STDOUT
// and write to the parent process's pipe for STDOUT. 
// Stop when there is no more data. 
{
    //DWORD dwWritten;
    BOOL bSuccess = FALSE;
    //HANDLE hParentStdOut = GetStdHandle(STD_OUTPUT_HANDLE);

    bSuccess = ReadFile(g_hChildStd_OUT_Rd, message, DEFAULT_BUFLEN, dwRead, NULL);

    return bSuccess && *dwRead != 0;

    //for (;;)
    //{
    //    
    //    
    //    //bSuccess = WriteFile(hParentStdOut, message,
    //    //    temp, &dwWritten, NULL);
    //    //if (!bSuccess) break;
    //    
    //    
    //    //printf("\n2DEBUG\n %d\n%s\n\n", dwRead, message);
    //    if (!bSuccess || *dwRead == 0) break;

    //    //break;
    //}
}

int SocketSetUp(SOCKET& ClientSocket) {
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
    iResult = getaddrinfo(NULL, DEFAULT_PORT, &hints, &result);
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

void CreateChildProcess()
// Create a child process that uses the previously created pipes for STDIN and STDOUT.
{
    TCHAR szCmdline[] = TEXT("C:\\Windows\\System32\\cmd.exe");// TEXT("child");
    PROCESS_INFORMATION piProcInfo;
    STARTUPINFO siStartInfo;
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
        ErrorExit(TEXT("CreateProcess"));
    else
    {
        // Close handles to the child process and its primary thread.
        // Some applications might keep these handles to monitor the status
        // of the child process, for example. 

        CloseHandle(piProcInfo.hProcess);
        CloseHandle(piProcInfo.hThread);
    }
}

void ErrorExit(PCTSTR lpszFunction)

// Format a readable error message, display a message box, 
// and exit from the application.
{
    LPVOID lpMsgBuf;
    LPVOID lpDisplayBuf;
    DWORD dw = GetLastError();

    FormatMessage(
        FORMAT_MESSAGE_ALLOCATE_BUFFER |
        FORMAT_MESSAGE_FROM_SYSTEM |
        FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL,
        dw,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPTSTR)&lpMsgBuf,
        0, NULL);

    lpDisplayBuf = (LPVOID)LocalAlloc(LMEM_ZEROINIT,
        (lstrlen((LPCTSTR)lpMsgBuf) + lstrlen((LPCTSTR)lpszFunction) + 40) * sizeof(TCHAR));
    StringCchPrintf((LPTSTR)lpDisplayBuf,
        LocalSize(lpDisplayBuf) / sizeof(TCHAR),
        TEXT("%s failed with error %d: %s"),
        lpszFunction, dw, lpMsgBuf);
    MessageBox(NULL, (LPCTSTR)lpDisplayBuf, TEXT("Error"), MB_OK);

    LocalFree(lpMsgBuf);
    LocalFree(lpDisplayBuf);
    ExitProcess(1);
}