#include <iostream>
#include <fstream>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <algorithm>
#include <cstdint>
#include <string>

#pragma comment(lib, "ws2_32.lib")
using namespace std;

const char *SERVER_IP = "127.0.0.1";
const int PORT = 54000;
const int BUFFER_SIZE = 4096;

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

bool sendAll(SOCKET sock, const char *data, int totalLen)
{
    int sent = 0;
    while (sent < totalLen)
    {
        int r = send(sock, data + sent, totalLen - sent, 0);
        if (r <= 0)
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

bool downloadFile(SOCKET sock)
{
    uint64_t fileSize = 0;
    if (!recvExact(sock, reinterpret_cast<char *>(&fileSize), sizeof(fileSize)))
        return false;
    if (fileSize == 0)
    {
        cerr << "[Client] File not available.\n";
        return false;
    }

    cout << "[Client] Downloading " << fileSize << " bytes...\n";

    ofstream outFile("received.txt", ios::binary);
    char buffer[BUFFER_SIZE];
    uint32_t crc = 0;
    uint64_t totalReceived = 0;

    while (totalReceived < fileSize)
    {
        int bytesToRead = (int)min<uint64_t>(sizeof(buffer), fileSize - totalReceived);
        int bytesRead = recv(sock, buffer, bytesToRead, 0);
        if (bytesRead <= 0)
            return false;
        outFile.write(buffer, bytesRead);
        crc = crc32(buffer, bytesRead, crc);
        totalReceived += bytesRead;
    }

    uint32_t receivedCRC = 0;
    if (!recvExact(sock, reinterpret_cast<char *>(&receivedCRC), sizeof(receivedCRC)))
        return false;

    cout << "[Client] CRC: computed=" << crc << ", received=" << receivedCRC << "\n";
    if (crc == receivedCRC)
        cout << "[Client] Integrity verified.\n";
    else
        cout << "[Client] Integrity mismatch!\n";

    outFile.close();
    return true;
}

bool uploadFile(SOCKET sock)
{
    ifstream file("upload.txt", ios::binary);
    if (!file.is_open())
    {
        cerr << "[Client] upload.txt not found.\n";
        return false;
    }

    file.seekg(0, ios::end);
    uint64_t fileSize = static_cast<uint64_t>(file.tellg());
    file.seekg(0, ios::beg);

    sendAll(sock, reinterpret_cast<const char *>(&fileSize), sizeof(fileSize));

    char buffer[BUFFER_SIZE];
    uint32_t crc = 0;
    while (file)
    {
        file.read(buffer, sizeof(buffer));
        streamsize bytesRead = file.gcount();
        if (bytesRead > 0)
        {
            crc = crc32(buffer, bytesRead, crc);
            sendAll(sock, buffer, static_cast<int>(bytesRead));
        }
    }

    uint32_t serverCRC = 0;
    if (!recvExact(sock, reinterpret_cast<char *>(&serverCRC), sizeof(serverCRC)))
        return false;

    cout << "[Client] Upload CRC: computed=" << crc << ", server=" << serverCRC << "\n";
    if (crc == serverCRC)
        cout << "[Client] Integrity verified.\n";
    else
        cout << "[Client] Integrity mismatch!\n";

    file.close();
    return true;
}

int main()
{
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);

    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(PORT);
    inet_pton(AF_INET, SERVER_IP, &serverAddr.sin_addr);

    if (connect(sock, (sockaddr *)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR)
    {
        cerr << "[Client] Connection failed.\n";
        return 1;
    }

    // Receive UUID from server
    uint32_t uuidLen = 0;
    if (!recvExact(sock, reinterpret_cast<char *>(&uuidLen), sizeof(uuidLen)))
        return 1;

    string clientUUID(uuidLen, 0);
    if (!recvExact(sock, clientUUID.data(), uuidLen))
        return 1;

    cout << "[Client] Connected. UUID: " << clientUUID << "\n";

    while (true)
    {
        cout << "Enter command (D=Download, U=Upload, Q=Quit): ";
        char cmd;
        cin >> cmd;
        send(sock, &cmd, 1, 0);

        if (cmd == 'D')
            downloadFile(sock);
        else if (cmd == 'U')
            uploadFile(sock);
        else if (cmd == 'Q')
            break;
        else
            cout << "[Client] Unknown command.\n";
    }

    closesocket(sock);
    WSACleanup();
    return 0;
}
