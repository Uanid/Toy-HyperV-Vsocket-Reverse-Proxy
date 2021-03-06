#define WIN32_LEAN_AND_MEAN
#define HV_PROTOCOL_RAW 1

#include <WinSock2.h>
#include <WS2tcpip.h>
#include <stdio.h>
#include <stdlib.h>
#include <combaseapi.h>
#include <thread>
//#include <guiddef.h>


// link with Ws2_32.lib
#pragma comment(lib, "Ws2_32.lib")

//#define DEFAULT_PORT "27015"
#define DEFAULT_BUFFER_LENGTH	8192
#define FORWARD	0
#define DEFORWARD 1

struct SOCKADDR_HV
{
	ADDRESS_FAMILY Family;
	USHORT Reserved;
	GUID VmId;
	GUID ServiceId;
};

DEFINE_GUID(HV_GUID_PARENT,
	0xa42e7cda, 0xd03f, 0x480c, 0x9c, 0xc2, 0xa, 0x4, 0xde, 0x20, 0xab, 0xb8, 0x78);
DEFINE_GUID(HV_GUID_ZERO,
	0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0);

SOCKET tunnelListen(char* serviceGuid) {
	int iResult;
	SOCKADDR_HV clientService;

	// Initialize GUIDs
	CLSID ServiceID;

	wchar_t* clsid_str22 = (wchar_t*)malloc(sizeof(wchar_t) * (strlen(serviceGuid) + 1));
	for (int i = 0; i < strlen(serviceGuid); i++)
	{
		clsid_str22[i] = serviceGuid[i];
	}
	clsid_str22[strlen(serviceGuid)] = '\0';

	CLSIDFromString(clsid_str22, &ServiceID); //GUID of Service, generated by powershell
	free(clsid_str22);

	// Initialize Winsock

	struct addrinfo* result = NULL,
		hints;

	CONST GUID* serviceId = &ServiceID;

	ZeroMemory(&clientService, sizeof(clientService));
	clientService.Family = AF_HYPERV;
	clientService.VmId = HV_GUID_ZERO;
	clientService.ServiceId = *serviceId;

	ZeroMemory(&hints, sizeof(hints));
	hints.ai_family = AF_HYPERV;		// Internet address family is unspecified so that either an IPv6 or IPv4 address can be returned
	hints.ai_socktype = SOCK_STREAM;	// Requests the socket type to be a stream socket for the TCP protocol
	hints.ai_protocol = HV_PROTOCOL_RAW;

	hints.ai_addrlen = sizeof(SOCKADDR_HV);
	hints.ai_addr = reinterpret_cast<SOCKADDR*>(&clientService);
	//hints.ai_flags = AI_PASSIVE;

	// Resolve the local address and port to be used by the server
	//iResult = getaddrinfo(NULL, DEFAULT_PORT, &hints, &result);
	//if (iResult != 0)
	//{
	//	printf("getaddrinfo failed: %d\n", iResult);
	//	WSACleanup();
	//	return 1;
	//}

	SOCKET ListenSocket = INVALID_SOCKET;

	// Create a SOCKET for the server to listen for client connections
	//ListenSocket = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
	ListenSocket = socket(hints.ai_family, hints.ai_socktype, hints.ai_protocol);

	if (ListenSocket == INVALID_SOCKET)
	{
		printf("Error at socket(): %d\n", WSAGetLastError());
		freeaddrinfo(result);
		WSACleanup();
		return NULL;
	}

	// Setup the TCP listening socket
	//iResult = bind(ListenSocket, result->ai_addr, (int)result->ai_addrlen);
	iResult = bind(ListenSocket, hints.ai_addr, (int)hints.ai_addrlen);

	if (iResult == SOCKET_ERROR)
	{
		printf("bind failed: %d", WSAGetLastError());
		freeaddrinfo(result);
		closesocket(ListenSocket);
		WSACleanup();
		return NULL;
	}

	freeaddrinfo(result);

	// To listen on a socket
	if (listen(ListenSocket, SOMAXCONN) == SOCKET_ERROR)
	{
		printf("listen failed: %d\n", WSAGetLastError());
		closesocket(ListenSocket);
		WSACleanup();
		return NULL;
	}
	return ListenSocket;
}

SOCKET gatewayListen(int port) {
	SOCKET hListen;
	hListen = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);

	SOCKADDR_IN hints = {};
	hints.sin_family = AF_INET;
	hints.sin_port = htons(port);
	hints.sin_addr.S_un.S_addr = htonl(INADDR_ANY);

	bind(hListen, (SOCKADDR*)&hints, sizeof(hints));
	listen(hListen, SOMAXCONN);
	return hListen;
}

SOCKET acceptTunnelSocket(SOCKET serverSocket) {
	SOCKET ClientSocket = INVALID_SOCKET;

	// Accept a client socket
	ClientSocket = accept(serverSocket, NULL, NULL);
	if (ClientSocket == INVALID_SOCKET)
	{
		printf("accept failed: %d\n", WSAGetLastError());
		closesocket(serverSocket);
		WSACleanup();
		return NULL;
	}
	return ClientSocket;
}

SOCKET acceptGatewaySocket(SOCKET serverSocket) {
	SOCKADDR_IN clientAddr = {};
	int size = sizeof(clientAddr);
	SOCKET clientSocket = accept(serverSocket, (SOCKADDR*)&clientAddr, &size);
	return clientSocket;
}

int socketInitialize() {
	WSADATA wsaData;
	int iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (iResult != 0)
	{
		printf("WSAStartup failed: %d\n", iResult);
		return 1;
	}
	return 0;
}

int teardown() {

}

void forwardFunction(SOCKET sourceSocket, SOCKET destSocket, int type) {
	char recvbuf[DEFAULT_BUFFER_LENGTH];
	while (true) {
		int iResult = recv(sourceSocket, recvbuf, DEFAULT_BUFFER_LENGTH, 0);

		if (iResult > 0)
		{
			if (type == FORWARD) {
				printf("Forward Received: %d\n", iResult);
			}
			else if (type == DEFORWARD) {
				printf("Backward Received: %d\n", iResult);
			}

			int iSendResult = send(destSocket, recvbuf, iResult, 0);
			if (iSendResult == SOCKET_ERROR)
			{
				printf("send failed: %d\n", WSAGetLastError());
				return;
			}

			printf("Bytes sent: %ld\n", iSendResult);
		}
		else if (iResult == 0) {
			printf("Connection closed\n");
			return;
		}
		else
		{
			printf("recv failed: %d\n", WSAGetLastError());
			return;
		}
	}
}

int main(int argc, char* argv[]) {
	if (argc < 3) {
		printf("command example\n");
		printf("<port> <serviceGuid>\n");
		return 0;
	}

	printf("listenport: %s, serviceGuid: %s\n", argv[1], argv[2]);
	
	if (socketInitialize() != 0) {
		printf("Initialize failed");
		return 1;
	}

	SOCKET tunnelServerSocket = tunnelListen(argv[2]);
	if (tunnelServerSocket == NULL) {
		printf("listen tunnel socket failed(hyper-v socket)");
		return 1;
	}
	printf("tunnel listener socket created\n");
	SOCKET gatewayServerSocket = gatewayListen(atoi(argv[1]));
	if (gatewayServerSocket == NULL) {
		printf("listen gateway socket failed(tcp socket)");
		return 1;
	}
	printf("gateway listener socket created\n");

	SOCKET tunnelClientSocket = acceptTunnelSocket(tunnelServerSocket);
	if (tunnelClientSocket == NULL) {
		printf("tunnel client socket accept failed(hyper-v socket)");
		return 1;
	}
	printf("tunnel client acceppted\n");
	SOCKET gatewayClientSocket = acceptGatewaySocket(gatewayServerSocket);
	if (gatewayClientSocket == NULL) {
		printf("tunnel client socket accept failed(tcp socket)");
		return 1;
	}
	printf("gateway client acceppted\n");

	std::thread forwardThread(forwardFunction, gatewayClientSocket, tunnelClientSocket, FORWARD);
	std::thread backwardThread(forwardFunction, tunnelClientSocket, gatewayClientSocket, DEFORWARD);


	getchar();
	printf("main finish\n");
	WSACleanup();
	return 0;
}