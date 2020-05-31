#include <iostream>
#include <windows.h>

#include "Pipe-client.h"
#include "Pipe-server.h"
#include "Pipe-service.h"
#include "parent.h"
#include "child.h"
#include "socket-client.h"


using namespace std;

int main(int argc, char* argv[]) {
    service(argc, (TCHAR**)argv);
    //socket_client(argc, argv);
}