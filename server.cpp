#include <iostream> // Includes the standard input-output stream library for console I/O
#include <fstream> // Includes the file stream library for file operations
#include <thread> // Includes the thread library for handling multiple clients
#include <vector> // Includes the vector library for dynamic arrays
#include <winsock2.h> // Includes the Winsock 2 library for socket programming
#include <ws2tcpip.h> // Includes additional Winsock functions for IP address handling
#include <cstdint> // Includes standard integer types like uint32_t and uint64_t
#include <random> // Includes the random library for UUID generation
#include <string> // Includes the string library for std::string operations

#pragma comment(lib, "ws2_32.lib") // Links the Winsock library to the program
using namespace std; // Uses the standard namespace to avoid prefixing std::

#define PORT 54000 // Defines the port number for the server
#define BUFFER_SIZE 4096 // Defines the buffer size for data transfer

// Computes the CRC32 checksum for data integrity verification
uint32_t crc32(const char *data, size_t length, uint32_t prev = 0)
{
    uint32_t crc = ~prev; // Inverts the previous CRC value (or 0 if none)
    for (size_t i = 0; i < length; ++i) // Iterates over each byte of the data
    {
        crc ^= static_cast<uint8_t>(data[i]); // XORs the current byte with the CRC
        for (int j = 0; j < 8; ++j) // Processes each bit of the byte
            crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1)); // Updates CRC using the polynomial
    }
    return ~crc; // Returns the inverted CRC value
}

// Generates a random UUID version 4
string generate_uuid_v4()
{
    random_device rd; // Initializes a random device for seed generation
    mt19937_64 gen(rd()); // Initializes a 64-bit Mersenne Twister random number generator
    uniform_int_distribution<uint64_t> dist(0, UINT64_MAX); // Defines a uniform distribution for 64-bit integers

    uint64_t a = dist(gen); // Generates the first 64-bit random number
    uint64_t b = dist(gen); // Generates the second 64-bit random number

    a &= 0xFFFFFFFFFFFF0FFFULL; // Clears version bits for UUID version 4
    a |= 0x0000000000004000ULL; // Sets version 4 bits
    b &= 0x3FFFFFFFFFFFFFFFULL; // Clears variant bits
    b |= 0x8000000000000000ULL; // Sets variant bits for RFC 4122 compliance

    char buf[37]; // Buffer to hold the formatted UUID string
    sprintf_s(buf, sizeof(buf), // Formats the UUID into a string
              "%08x-%04x-%04x-%04x-%012llx",
              (unsigned int)(a >> 32), // First 32 bits
              (unsigned int)((a >> 16) & 0xFFFF), // Next 16 bits
              (unsigned int)(a & 0xFFFF), // Next 16 bits
              (unsigned int)(b >> 48), // Next 16 bits
              (unsigned long long)(b & 0xFFFFFFFFFFFFULL)); // Last 48 bits
    return string(buf); // Returns the UUID as a string
}

// Sends all data in the buffer, handling partial sends
bool sendAll(SOCKET sock, const char *data, int totalLen)
{
    int sent = 0; // Tracks the number of bytes sent
    while (sent < totalLen) // Continues until all bytes are sent
    {
        int r = send(sock, data + sent, totalLen - sent, 0); // Sends remaining data
        if (r == SOCKET_ERROR || r == 0) // Checks for errors or disconnection
            return false; // Returns false if send fails
        sent += r; // Updates the number of bytes sent
    }
    return true; // Returns true if all data is sent successfully
}

// Receives exactly the specified number of bytes
bool recvExact(SOCKET sock, char *buffer, int bytesToRecv)
{
    int received = 0; // Tracks the number of bytes received
    while (received < bytesToRecv) // Continues until all bytes are received
    {
        int r = recv(sock, buffer + received, bytesToRecv - received, 0); // Receives remaining data
        if (r <= 0) // Checks for errors or disconnection
            return false; // Returns false if receive fails
        received += r; // Updates the number of bytes received
    }
    return true; // Returns true if all data is received successfully
}

// Handles file download requests from a client
void handleDownload(SOCKET clientSocket, const string &clientUUID)
{
    cout << "[Server][" << clientUUID << "] Download request received.\n"; // Prints download request message

    ifstream file("testfile.txt", ios::binary); // Opens the file to send in binary mode
    if (!file) // Checks if the file was opened successfully
    {
        cerr << "[Server][" << clientUUID << "] Cannot open file.\n"; // Prints error message if file not found
        uint64_t zero = 0; // Sets file size to zero to indicate failure
        sendAll(clientSocket, reinterpret_cast<const char *>(&zero), sizeof(zero)); // Sends zero file size
        return; // Exits the function
    }

    file.seekg(0, ios::end); // Moves file pointer to the end to get file size
    uint64_t fileSize = static_cast<uint64_t>(file.tellg()); // Gets the file size
    file.seekg(0, ios::beg); // Moves file pointer back to the beginning

    vector<char> fileBuffer(static_cast<size_t>(fileSize)); // Creates a buffer to hold the entire file
    file.read(fileBuffer.data(), fileSize); // Reads the file into the buffer
    uint32_t crc = crc32(fileBuffer.data(), (size_t)fileSize, 0); // Computes CRC for the file
    file.clear(); // Clears any error flags
    file.seekg(0, ios::beg); // Moves file pointer back to the beginning

    sendAll(clientSocket, reinterpret_cast<const char *>(&fileSize), sizeof(fileSize)); // Sends file size to client

    char buffer[BUFFER_SIZE]; // Buffer for sending file data
    while (file) // Continues until the entire file is read
    {
        file.read(buffer, sizeof(buffer)); // Reads data into buffer
        streamsize bytesRead = file.gcount(); // Gets the number of bytes read
        if (bytesRead > 0) // Checks if data was read
            sendAll(clientSocket, buffer, static_cast<int>(bytesRead)); // Sends data to client
    }

    sendAll(clientSocket, reinterpret_cast<const char *>(&crc), sizeof(crc)); // Sends CRC to client
    cout << "[Server][" << clientUUID << "] File sent. CRC: " << crc << "\n"; // Prints completion message
}

// Handles file upload requests from a client
void handleUpload(SOCKET clientSocket, const string &clientUUID)
{
    cout << "[Server][" << clientUUID << "] Upload request received.\n"; // Prints upload request message

    ofstream file("uploaded_from_client.txt", ios::binary); // Opens output file in binary mode
    if (!file) // Checks if the file was opened successfully
    {
        cerr << "[Server][" << clientUUID << "] Cannot open file for upload.\n"; // Prints error message
        return; // Exits the function
    }

    uint64_t fileSize = 0; // Stores the size of the file to be uploaded
    if (!recvExact(clientSocket, reinterpret_cast<char *>(&fileSize), sizeof(fileSize))) // Receives file size
    {
        cerr << "[Server][" << clientUUID << "] Failed to receive file size.\n"; // Prints error message
        return; // Exits the function
    }

    char buffer[BUFFER_SIZE]; // Buffer for receiving file data
    uint32_t crc = 0; // Initializes CRC for integrity check
    uint64_t totalReceived = 0; // Tracks total bytes received

    while (totalReceived < fileSize) // Continues until all file bytes are received
    {
        int bytesToRead = (int)min<uint64_t>(sizeof(buffer), fileSize - totalReceived); // Calculates bytes to read
        int r = recv(clientSocket, buffer, bytesToRead, 0); // Receives data into buffer
        if (r <= 0) // Checks for errors or disconnection
        {
            cerr << "[Server][" << clientUUID << "] Connection lost during upload.\n"; // Prints error message
            return; // Exits the function
        }
        file.write(buffer, r); // Writes received data to file
        crc = crc32(buffer, r, crc); // Updates CRC with received data
        totalReceived += r; // Updates total bytes received
    }

    sendAll(clientSocket, reinterpret_cast<const char *>(&crc), sizeof(crc)); // Sends CRC to client
    cout << "[Server][" << clientUUID << "] Upload complete. CRC: " << crc << "\n"; // Prints completion message
}

// Handles a single client connection
void handleClient(SOCKET clientSocket)
{
    string clientUUID = generate_uuid_v4(); // Generates a unique UUID for the client

    uint32_t uuidLen = static_cast<uint32_t>(clientUUID.size()); // Gets the length of the UUID
    sendAll(clientSocket, reinterpret_cast<const char *>(&uuidLen), sizeof(uuidLen)); // Sends UUID length to client
    sendAll(clientSocket, clientUUID.c_str(), (int)clientUUID.size()); // Sends UUID to client

    cout << "[Server] Assigned UUID to client: " << clientUUID << "\n"; // Prints assigned UUID

    while (true) // Main loop for handling client commands
    {
        char cmd = 0; // Stores the client command
        int r = recv(clientSocket, &cmd, 1, 0); // Receives the command
        if (r <= 0) // Checks for errors or disconnection
        {
            cout << "[Server][" << clientUUID << "] Client disconnected.\n"; // Prints disconnection message
            break; // Exits the loop
        }

        if (cmd == 'D') // If client requests download
            handleDownload(clientSocket, clientUUID); // Calls download handler
        else if (cmd == 'U') // If client requests upload
            handleUpload(clientSocket, clientUUID); // Calls upload handler
        else if (cmd == 'Q') // If client requests quit
        {
            cout << "[Server][" << clientUUID << "] Client requested QUIT.\n"; // Prints quit message
            break; // Exits the loop
        }
        else
            cerr << "[Server][" << clientUUID << "] Unknown command: " << cmd << "\n"; // Prints error for invalid command
    }

    closesocket(clientSocket); // Closes the client socket
    cout << "[Server][" << clientUUID << "] Connection closed.\n"; // Prints connection closed message
}

// Main function, entry point of the program
int main()
{
    WSADATA wsaData; // Structure to hold Winsock initialization data
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) // Initializes Winsock version 2.2
        return 1; // Exits with error code if initialization fails

    SOCKET serverSocket = socket(AF_INET, SOCK_STREAM, 0); // Creates a TCP server socket
    sockaddr_in serverAddr{}; // Structure to hold server address information
    serverAddr.sin_family = AF_INET; // Sets address family to IPv4
    serverAddr.sin_port = htons(PORT); // Sets port number (converts to network byte order)
    serverAddr.sin_addr.s_addr = INADDR_ANY; // Binds to any available network interface

    bind(serverSocket, (sockaddr *)&serverAddr, sizeof(serverAddr)); // Binds the socket to the address
    listen(serverSocket, SOMAXCONN); // Sets the socket to listen for connections

    cout << "[Server] Waiting for clients...\n"; // Prints server start message

    while (true) // Main loop for accepting client connections
    {
        SOCKET clientSocket = accept(serverSocket, nullptr, nullptr); // Accepts a new client connection
        if (clientSocket == INVALID_SOCKET) // Checks for errors
            break; // Exits the loop if accept fails

        cout << "[Server] Client connected.\n"; // Prints client connection message
        thread t(handleClient, clientSocket); // Creates a new thread to handle the client
        t.detach(); // Detaches the thread to run independently
    }

    closesocket(serverSocket); // Closes the server socket
    WSACleanup(); // Cleans up Winsock resources
    return 0; // Exits the program successfully
}

