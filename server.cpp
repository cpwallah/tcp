#include <iostream>
#include <fstream>
#include <thread>
#include <vector>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <cstdint>
#include <random>
#include <string>

#pragma comment(lib, "ws2_32.lib")
using namespace std;

#define PORT 54000
#define BUFFER_SIZE 4096

uint32_t crc32(const char *data, size_t length, uint32_t prev = 0)
{
    uint32_t crc = ~prev;
    for (size_t i = 0; i < length; ++i)
    {
        crc ^= static_cast<uint8_t>(data[i]);
        for (int j = 0; j < 8; ++j)
            crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
    }
    return ~crc;
}

string generate_uuid_v4()
{
    random_device rd;
    mt19937_64 gen(rd());
    uniform_int_distribution<uint64_t> dist(0, UINT64_MAX);

    uint64_t a = dist(gen);
    uint64_t b = dist(gen);

    a &= 0xFFFFFFFFFFFF0FFFULL; // version 4
    a |= 0x0000000000004000ULL;
    b &= 0x3FFFFFFFFFFFFFFFULL;
    b |= 0x8000000000000000ULL; // variant

    char buf[37];
    sprintf_s(buf, sizeof(buf),
              "%08x-%04x-%04x-%04x-%012llx",
              (unsigned int)(a >> 32),
              (unsigned int)((a >> 16) & 0xFFFF),
              (unsigned int)(a & 0xFFFF),
              (unsigned int)(b >> 48),
              (unsigned long long)(b & 0xFFFFFFFFFFFFULL));
    return string(buf);
}

bool sendAll(SOCKET sock, const char *data, int totalLen)
{
    int sent = 0;
    while (sent < totalLen)
    {
        int r = send(sock, data + sent, totalLen - sent, 0);
        if (r == SOCKET_ERROR || r == 0)
            return false;
        sent += r;
    }
    return true;
}

bool recvExact(SOCKET sock, char *buffer, int bytesToRecv)
{
    int received = 0;
    while (received < bytesToRecv)
    {
        int r = recv(sock, buffer + received, bytesToRecv - received, 0);
        if (r <= 0)
            return false;
        received += r;
    }
    return true;
}

void handleDownload(SOCKET clientSocket, const string &clientUUID)
{
    cout << "[Server][" << clientUUID << "] Download request received.\n";

    ifstream file("testfile.txt", ios::binary);
    if (!file)
    {
        cerr << "[Server][" << clientUUID << "] Cannot open file.\n";
        uint64_t zero = 0;
        sendAll(clientSocket, reinterpret_cast<const char *>(&zero), sizeof(zero));
        return;
    }

    file.seekg(0, ios::end);
    uint64_t fileSize = static_cast<uint64_t>(file.tellg());
    file.seekg(0, ios::beg);

    vector<char> fileBuffer(static_cast<size_t>(fileSize));
    file.read(fileBuffer.data(), fileSize);
    uint32_t crc = crc32(fileBuffer.data(), (size_t)fileSize, 0);
    file.clear();
    file.seekg(0, ios::beg);

    sendAll(clientSocket, reinterpret_cast<const char *>(&fileSize), sizeof(fileSize));

    char buffer[BUFFER_SIZE];
    while (file)
    {
        file.read(buffer, sizeof(buffer));
        streamsize bytesRead = file.gcount();
        if (bytesRead > 0)
            sendAll(clientSocket, buffer, static_cast<int>(bytesRead));
    }

    sendAll(clientSocket, reinterpret_cast<const char *>(&crc), sizeof(crc));
    cout << "[Server][" << clientUUID << "] File sent. CRC: " << crc << "\n";
}

void handleUpload(SOCKET clientSocket, const string &clientUUID)
{
    cout << "[Server][" << clientUUID << "] Upload request received.\n";

    ofstream file("uploaded_from_client.txt", ios::binary);
    if (!file)
    {
        cerr << "[Server][" << clientUUID << "] Cannot open file for upload.\n";
        return;
    }

    uint64_t fileSize = 0;
    if (!recvExact(clientSocket, reinterpret_cast<char *>(&fileSize), sizeof(fileSize)))
    {
        cerr << "[Server][" << clientUUID << "] Failed to receive file size.\n";
        return;
    }

    char buffer[BUFFER_SIZE];
    uint32_t crc = 0;
    uint64_t totalReceived = 0;

    while (totalReceived < fileSize)
    {
        int bytesToRead = (int)min<uint64_t>(sizeof(buffer), fileSize - totalReceived);
        int r = recv(clientSocket, buffer, bytesToRead, 0);
        if (r <= 0)
        {
            cerr << "[Server][" << clientUUID << "] Connection lost during upload.\n";
            return;
        }
        file.write(buffer, r);
        crc = crc32(buffer, r, crc);
        totalReceived += r;
    }

    sendAll(clientSocket, reinterpret_cast<const char *>(&crc), sizeof(crc));
    cout << "[Server][" << clientUUID << "] Upload complete. CRC: " << crc << "\n";
}

void handleClient(SOCKET clientSocket)
{
    string clientUUID = generate_uuid_v4();

    uint32_t uuidLen = static_cast<uint32_t>(clientUUID.size());
    sendAll(clientSocket, reinterpret_cast<const char *>(&uuidLen), sizeof(uuidLen));
    sendAll(clientSocket, clientUUID.c_str(), (int)clientUUID.size());

    cout << "[Server] Assigned UUID to client: " << clientUUID << "\n";

    while (true)
    {
        char cmd = 0;
        int r = recv(clientSocket, &cmd, 1, 0);
        if (r <= 0)
        {
            cout << "[Server][" << clientUUID << "] Client disconnected.\n";
            break;
        }

        if (cmd == 'D')
            handleDownload(clientSocket, clientUUID);
        else if (cmd == 'U')
            handleUpload(clientSocket, clientUUID);
        else if (cmd == 'Q')
        {
            cout << "[Server][" << clientUUID << "] Client requested QUIT.\n";
            break;
        }
        else
            cerr << "[Server][" << clientUUID << "] Unknown command: " << cmd << "\n";
    }

    closesocket(clientSocket);
    cout << "[Server][" << clientUUID << "] Connection closed.\n";
}

int main()
{
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
        return 1;

    SOCKET serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(PORT);
    serverAddr.sin_addr.s_addr = INADDR_ANY;

    bind(serverSocket, (sockaddr *)&serverAddr, sizeof(serverAddr));
    listen(serverSocket, SOMAXCONN);

    cout << "[Server] Waiting for clients...\n";

    while (true)
    {
        SOCKET clientSocket = accept(serverSocket, nullptr, nullptr);
        if (clientSocket == INVALID_SOCKET)
            break;

        cout << "[Server] Client connected.\n";
        thread t(handleClient, clientSocket);
        t.detach();
    }

    closesocket(serverSocket);
    WSACleanup();
    return 0;
}
