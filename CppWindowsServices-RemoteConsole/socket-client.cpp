#define WIN32_LEAN_AND_MEAN
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdio.h>


// Need to link with Ws2_32.lib, Mswsock.lib, and Advapi32.lib
#pragma comment (lib, "Ws2_32.lib")
#pragma comment (lib, "Mswsock.lib")
#pragma comment (lib, "AdvApi32.lib")


#define DEFAULT_BUFLEN 512
#define DEFAULT_OUTPUT_PORT "27015"
#define DEFAULT_INPUT_PORT "27016"

DWORD WINAPI sendingMessages(LPVOID sOutputSocket) {
    char sendbuf[1024];
    //char* sendbuf;
    int iResult;

    // Send until the connection closes
    do {
        //scanf("%s", sendbuf);
        fgets(sendbuf, 1024, stdin);
        //sendbuf = (char*)"dir\n";

        // Send a buffer
        iResult = send((SOCKET)(sOutputSocket), sendbuf, (int)strlen(sendbuf), 0);
        if (iResult == SOCKET_ERROR) {
            printf("send failed with error: %d\n", WSAGetLastError());
            closesocket((SOCKET)(sOutputSocket));
            WSACleanup();
            return 1;
        }

        //printf("Bytes Sent: %ld\n", iResult);
    } while (iResult > 0);

    return 0;
}

DWORD WINAPI recievingMessages(LPVOID sInputSocket) {
    char recvbuf[DEFAULT_BUFLEN];
    int recvbuflen = DEFAULT_BUFLEN;
    int iResult;

    //printf("receiving starting...\n");
    // Receive until the peer closes the connection
    do {
        iResult = recv((SOCKET)(sInputSocket), recvbuf, recvbuflen, 0);
        //printf("received something...\n");
        if (iResult == 0)
            printf("Connection closed\n");
        else if (iResult < 0)
            printf("recv failed with error: %d\n", WSAGetLastError());

        for (int i = 0; i < iResult; i++)
            printf("%c", recvbuf[i]);

    } while (iResult > 0);
    //printf("receiving finished\n");

    return 0;
}

int establishConnection(SOCKET &ConnectSocket, char* ip, char* port_number) {
    WSADATA wsaData;
    struct addrinfo* result = NULL, 
        * ptr = NULL, 
        hints;
    int iResult;

    // Initialize Winsock
    iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (iResult != 0) {
        printf("WSAStartup failed with error: %d\n", iResult);
        return 1;
    }

    ZeroMemory(&hints, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    // Resolve the server address and port
    iResult = getaddrinfo(ip, port_number, &hints, &result);
    if (iResult != 0) {
        printf("getaddrinfo failed with error: %d\n", iResult);
        WSACleanup();
        return 1;
    }

    // Attempt to connect to an address until one succeeds
    for (ptr = result; ptr != NULL; ptr = ptr->ai_next) {

        // Create a SOCKET for connecting to server
        ConnectSocket = socket(ptr->ai_family, ptr->ai_socktype,
            ptr->ai_protocol);
        if (ConnectSocket == INVALID_SOCKET) {
            printf("socket failed with error: %ld\n", WSAGetLastError());
            WSACleanup();
            return 1;
        }

        // Connect to server.
        iResult = connect(ConnectSocket, ptr->ai_addr, (int)ptr->ai_addrlen);
        if (iResult == SOCKET_ERROR) {
            closesocket(ConnectSocket);
            ConnectSocket = INVALID_SOCKET;
            continue;
        }
        break;
    }

    freeaddrinfo(result);

    if (ConnectSocket == INVALID_SOCKET) {
        printf("Unable to connect to server!\n");
        WSACleanup();
        return 1;
    }
}

int socket_client(int argc, char* argv[])
{
    SOCKET sOutputSocket = INVALID_SOCKET;
    establishConnection(sOutputSocket, argv[1], (char*)DEFAULT_OUTPUT_PORT);
    SOCKET sInputSocket = INVALID_SOCKET;
    establishConnection(sInputSocket, argv[1], (char*)DEFAULT_INPUT_PORT);

    DWORD dwSendingThreadId;
    HANDLE hSendingThread = CreateThread(
        NULL,              // no security attribute 
        0,                 // default stack size 
        sendingMessages,    // thread proc
        (LPVOID)(sOutputSocket),    // thread parameter 
        0,                 // not suspended 
        &dwSendingThreadId);      // returns thread ID 

    if (hSendingThread == NULL)
    {
        printf("CreateThread for sending failed, GLE=%d.\n", GetLastError());
        return -1;
    }
    
    DWORD dwRecievingThreadId;
    HANDLE hRecievingThread = CreateThread(
        NULL,              // no security attribute 
        0,                 // default stack size 
        recievingMessages,    // thread proc
        (LPVOID)(sInputSocket),    // thread parameter 
        0,                 // not suspended 
        &dwRecievingThreadId);      // returns thread ID 
    
    if (hRecievingThread == NULL)
    {
        printf("CreateThread for recieving failed, GLE=%d.\n", GetLastError());
        return -1;
    }

    // Wait for threads stops
    WaitForSingleObject(hRecievingThread, INFINITE);
    WaitForSingleObject(hSendingThread, INFINITE);


    CloseHandle(hRecievingThread);
    CloseHandle(hSendingThread);


    // cleanup
    closesocket(sOutputSocket);
    closesocket(sInputSocket);
    WSACleanup();

    return 0;
}