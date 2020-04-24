////
//// Created by Anonymous275 on 3/3/2020.
////

#define ENET_IMPLEMENTATION
#include <condition_variable>
#include "include/enet.h"
#include <WinSock2.h>
#include <WS2tcpip.h>
#include <iostream>
#include <string>
#include <thread>
#include <queue>

#define DEFAULT_PORT "4445"
typedef struct {
    ENetHost *host;
    ENetPeer *peer;
} Client;
std::vector<std::string> Split(const std::string& String,const std::string& delimiter);
std::chrono::time_point<std::chrono::steady_clock> PingStart,PingEnd;
extern std::vector<std::string> GlobalInfo;
std::queue<std::string> RUDPToSend;
std::queue<std::string> RUDPData;
bool TCPTerminate = false;
bool Terminate = false;
bool CServer = true;
int ping = 0;

[[noreturn]] void CoreNetworkThread();

void AutoPing(ENetPeer*peer){
    while(!Terminate && peer != nullptr){
        enet_peer_send(peer, 0, enet_packet_create("p", 2, ENET_PACKET_FLAG_RELIABLE));
        PingStart = std::chrono::high_resolution_clock::now();
        std::this_thread::sleep_for(std::chrono::seconds (1));
    }
}
void NameRespond(ENetPeer*peer){
    std::string Packet = "NR" + GlobalInfo.at(0)+":"+GlobalInfo.at(2);
    enet_peer_send(peer, 0, enet_packet_create(Packet.c_str(), Packet.length()+1, ENET_PACKET_FLAG_RELIABLE));
}

void RUDPParser(const std::string& Data,ENetPeer*peer){
    char Code = Data.at(0),SubCode = 0;
    if(Data.length() > 1)SubCode = Data.at(1);
    switch (Code) {
        case 'p':
            PingEnd = std::chrono::high_resolution_clock::now();
            ping = std::chrono::duration_cast<std::chrono::milliseconds>(PingEnd-PingStart).count();
            return;
        case 'N':
            if(SubCode == 'R')NameRespond(peer);
            return;
    }
    ///std::cout << "Received: " << Data << std::endl;
    RUDPData.push(Data);
}

void HandleEvent(ENetEvent event,Client client){
    switch (event.type){
        case ENET_EVENT_TYPE_CONNECT:
            std::cout << "(Launcher->Server) Client Connected!" << std::endl;
            //printf("Client Connected port : %u.\n",event.peer->address.port);
            event.peer->data = (void*)"Connected Server";
            break;
        case ENET_EVENT_TYPE_RECEIVE:
            RUDPParser((char*)event.packet->data,event.peer);
            enet_packet_destroy(event.packet);
            break;
        case ENET_EVENT_TYPE_DISCONNECT:
            printf ("%s disconnected.\n", (char *)event.peer->data);
            event.peer->data = nullptr;
            break;

        case ENET_EVENT_TYPE_DISCONNECT_TIMEOUT:
            printf ("%s timeout.\n", (char *)event.peer->data);
            CServer = true;
            Terminate = true;
            event.peer->data = nullptr;
            break;

        case ENET_EVENT_TYPE_NONE: break;
    }
}
void RUDPClientThread(const std::string& IP, int Port){
    if (enet_initialize() != 0) {
       std::cout << "An error occurred while initializing RUDP.\n";
    }
    Client client;
    ENetAddress address = {0};

    address.host = ENET_HOST_ANY;
    address.port = Port;
    std::cout << "(Launcher->Server) Connecting...\n";

    enet_address_set_host(&address, IP.c_str());
    client.host = enet_host_create(nullptr, 1, 2, 0, 0);
    client.peer = enet_host_connect(client.host, &address, 2, 0);
    if (client.peer == nullptr) {
        std::cout << "could not connect\n";
    }
    std::thread Ping(AutoPing,client.peer);
    Ping.detach();
    ENetEvent event;
    do {
        enet_host_service(client.host, &event, 0);
        HandleEvent(event,client);
        while (!RUDPToSend.empty()){
                int Rel = 8;
                char C = RUDPToSend.front().at(0);
                if (C == 's' || C == 'd' || C == 'm' || C == 'r')Rel = 1;
                ENetPacket *packet = enet_packet_create(RUDPToSend.front().c_str(),
                                                        RUDPToSend.front().length() + 1,
                                                        Rel);
                enet_peer_send(client.peer, 0, packet);
                if (RUDPToSend.front().length() > 1000) {
                    std::cout << "(Launcher->Server) Bytes sent: " << RUDPToSend.front().length() << " : "
                              << RUDPToSend.front().substr(0, 10)
                              << RUDPToSend.front().substr(RUDPToSend.front().length() - 10) << std::endl;
                }
            RUDPToSend.pop();
        }
        std::this_thread::sleep_for(std::chrono::nanoseconds(1));
    } while (!Terminate);
    enet_peer_disconnect(client.peer,0);
    enet_host_service(client.host, &event, 0);
    HandleEvent(event,client);
    CServer = true;
    std::cout << "(Launcher->Server) Terminated!" << std::endl;
}

void TCPRespond(const SOCKET *CS){
    SOCKET ClientSocket = *CS;
    int iSendResult;
    std::mutex m;
    while(!TCPTerminate){
        while (!RUDPData.empty()) {
            std::string Data =  RUDPData.front() + "\n";
            iSendResult = send(ClientSocket, Data.c_str(), Data.length(), 0);
            if (iSendResult == SOCKET_ERROR) {
                std::cout << "(Proxy) send failed with error: " << WSAGetLastError() << std::endl;
                break;
            } else {
                if(iSendResult > 1000){
                    std::cout << "(Launcher->Game) Bytes sent: " << iSendResult <<  " : " <<  RUDPData.front().substr(0,10)
                    << RUDPData.front().substr(RUDPData.front().length()-10) << std::endl;
                }
                RUDPData.pop();
            }
        }
        std::this_thread::sleep_for(std::chrono::nanoseconds(1));
    }
}

void TCPServerThread(const std::string& IP, int Port){
    std::cout << "Proxy Started! " << IP << ":" << Port << std::endl;
    do {
        std::cout << "Proxy on Start" << std::endl;
        WSADATA wsaData;
        int iResult;
        SOCKET ListenSocket = INVALID_SOCKET;
        SOCKET ClientSocket = INVALID_SOCKET;

        struct addrinfo *result = nullptr;
        struct addrinfo hints{};
        char recvbuf[10000];
        int recvbuflen = 10000;

        // Initialize Winsock
        iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
        if (iResult != 0) {
            std::cout << "(Proxy) WSAStartup failed with error: " << iResult << std::endl;
            std::cin.get();
            exit(-1);
        }

        ZeroMemory(&hints, sizeof(hints));
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;
        hints.ai_flags = AI_PASSIVE;

        // Resolve the server address and port
        iResult = getaddrinfo(nullptr, DEFAULT_PORT, &hints, &result);
        if (iResult != 0) {
            std::cout << "(Proxy) getaddrinfo failed with error: " << iResult << std::endl;
            WSACleanup();
            break;
        }

        // Create a socket for connecting to server
        ListenSocket = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
        if (ListenSocket == INVALID_SOCKET) {
            std::cout << "(Proxy) socket failed with error: " << WSAGetLastError() << std::endl;
            freeaddrinfo(result);
            WSACleanup();
            break;
        }

        // Setup the TCP listening socket
        iResult = bind(ListenSocket, result->ai_addr, (int) result->ai_addrlen);
        if (iResult == SOCKET_ERROR) {
            std::cout << "(Proxy) bind failed with error: " << WSAGetLastError() << std::endl;
            freeaddrinfo(result);
            closesocket(ListenSocket);
            WSACleanup();
            break;
        }

        freeaddrinfo(result);

        iResult = listen(ListenSocket, SOMAXCONN);
        if (iResult == SOCKET_ERROR) {
            std::cout << "(Proxy) listen failed with error: " << WSAGetLastError() << std::endl;
            closesocket(ListenSocket);
            WSACleanup();
            continue;
        }
        ClientSocket = accept(ListenSocket, NULL, NULL);
        if (ClientSocket == INVALID_SOCKET) {
            std::cout << "(Proxy) accept failed with error: " << WSAGetLastError() << std::endl;
            closesocket(ListenSocket);
            WSACleanup();
            continue;
        }
        closesocket(ListenSocket);
        std::cout << "(Proxy) Game Connected!" << std::endl;
        if(CServer){
            std::thread t1(RUDPClientThread, IP, Port);
            t1.detach();
            CServer = false;
        }
        std::thread TCPSend(TCPRespond,&ClientSocket);
        TCPSend.detach();
        do {
            //std::cout << "(Proxy) Waiting for Game Data..." << std::endl;
            iResult = recv(ClientSocket, recvbuf, recvbuflen, 0);
            if (iResult > 0) {
                std::string buff;
                buff.resize(iResult*2);
                memcpy(&buff[0],recvbuf,iResult);
                buff.resize(iResult);

                if(buff.length() > 3)RUDPToSend.push(buff);

                //std::cout << "(Game->Launcher) Data : " << buff.length() << std::endl;
            } else if (iResult == 0) {
                std::cout << "(Proxy) Connection closing...\n";
                closesocket(ClientSocket);
                WSACleanup();

                continue;
            } else {
                std::cout << "(Proxy) recv failed with error: " << WSAGetLastError() << std::endl;
                closesocket(ClientSocket);
                WSACleanup();
                continue;
            }
        } while (iResult > 0);

        iResult = shutdown(ClientSocket, SD_SEND);
        if (iResult == SOCKET_ERROR) {
            std::cout << "(Proxy) shutdown failed with error: " << WSAGetLastError() << std::endl;
            closesocket(ClientSocket);
            WSACleanup();
            continue;
        }
        closesocket(ClientSocket);
        WSACleanup();
    }while (!TCPTerminate);
}



void ProxyStart(){
    std::thread t1(CoreNetworkThread);
    std::cout << "Core Network Started!\n";
    t1.join();
}
void VehicleNetworkStart();
void ProxyThread(const std::string& IP, int Port){
    Terminate = false;
    TCPTerminate = false;
    std::thread t1(TCPServerThread,IP,Port);
    t1.detach();
    std::thread t2(VehicleNetworkStart);
    t2.detach();
}