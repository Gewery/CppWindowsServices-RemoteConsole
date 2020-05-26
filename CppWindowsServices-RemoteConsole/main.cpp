#include <iostream>
#include <windows.h>

#include "Pipe-client.h"
#include "Pipe-server.h"
#include "Pipe-service.h"
#include "parent.h"
#include "child.h"

using namespace std;

int main(int argc, char* argv[]) {
    if (argc != 2) {
        cout << "Please specify --client --server or --service\n";
        return 0;
    }

    if ((string)(argv[1]) == "--server") {
        cout << "Server mode on\n";
        parent();
        //pipe_server();
    }
    else if ((string)(argv[1]) == "--client") {
        cout << "Client mode on\n";
        child();
        //pipe_client();
    }
    else if ((string)(argv[1]) == "--service") {
        cout << "Service mode on\n";
        service();
    }
}