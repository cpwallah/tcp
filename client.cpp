#include <iostream> // Includes the standard input-output stream library for console I/O
#include <fstream> // Includes the file stream library for file operations
#include <winsock2.h> // Includes the Winsock 2 library for socket programming
#include <ws2tcpip.h> // Includes additional Winsock functions for IP address handling
#include <algorithm> // Includes the algorithm library for functions like std::min
#include <cstdint> // Includes standard integer types like uint32_t and uint64_t
#include <string> // Includes the string library for std::string operations

#pragma comment(lib, "ws2_32.lib") // Links the Winsock library to the program
using namespace std; // Uses the standard namespace to avoid prefixing std::

const char *SERVER_IP = "127.0.0.1"; // Defines the server IP address (localhost)
const int PORT = 54000; // Defines the port number for the server
const int BUFFER_SIZE = 4096; // Defines the buffer size for data transfer

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

// Sends all data in the buffer, handling partial sends
bool sendAll(SOCKET sock, const char *data, int totalLen)
{
    int sent = 0; // Tracks the number of bytes sent
    while (sent < totalLen) // Continues until all bytes are sent
    {
        int r = send(sock, data + sent, totalLen - sent, 0); // Sends remaining data
        if (r <= 0) // Checks for errors or disconnection
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

// Handles file download from the server
bool downloadFile(SOCKET sock)
{
    uint64_t fileSize = 0; // Stores the size of the file to be downloaded
    if (!recvExact(sock, reinterpret_cast<char *>(&fileSize), sizeof(fileSize))) // Receives the file size
        return false; // Returns false if receiving file size fails
    if (fileSize == 0) // Checks if the file size is zero (file not available)
    {
        cerr << "[Client] File not available.\n"; // Prints error message to console
        return false; // Returns false to indicate failure
    }

    cout << "[Client] Downloading " << fileSize << " bytes...\n"; // Prints download start message

    ofstream outFile("received.txt", ios::binary); // Opens output file in binary mode
    char buffer[BUFFER_SIZE]; // Buffer for receiving file data
    uint32_t crc = 0; // Initializes CRC for integrity check
    uint64_t totalReceived = 0; // Tracks total bytes received

    while (totalReceived < fileSize) // Continues until all file bytes are received
    {
        int bytesToRead = (int)min<uint64_t>(sizeof(buffer), fileSize - totalReceived); // Calculates bytes to read
        int bytesRead = recv(sock, buffer, bytesToRead, 0); // Receives data into buffer
        if (bytesRead <= 0) // Checks for errors or disconnection
            return false; // Returns false if receive fails
        outFile.write(buffer, bytesRead); // Writes received data to file
        crc = crc32(buffer, bytesRead, crc); // Updates CRC with received data
        totalReceived += bytesRead; // Updates total bytes received
    }

    uint32_t receivedCRC = 0; // Stores the CRC received from the server
    if (!recvExact(sock, reinterpret_cast<char *>(&receivedCRC), sizeof(receivedCRC))) // Receives the server's CRC
        return false; // Returns false if receiving CRC fails

    cout << "[Client] CRC: computed=" << crc << ", received=" << receivedCRC << "\n"; // Prints computed and received CRCs
    if (crc == receivedCRC) // Checks if CRCs match
        cout << "[Client] Integrity verified.\n"; // Prints success message
    else
        cout << "[Client] Integrity mismatch!\n"; // Prints failure message

    outFile.close(); // Closes the output file
    return true; // Returns true to indicate successful download
}

// Handles file upload to the server
bool uploadFile(SOCKET sock)
{
    ifstream file("upload.txt", ios::binary); // Opens the file to upload in binary mode
    if (!file.is_open()) // Checks if the file was opened successfully
    {
        cerr << "[Client] upload.txt not found.\n"; // Prints error message if file not found
        return false; // Returns false to indicate failure
    }

    file.seekg(0, ios::end); // Moves file pointer to the end to get file size
    uint64_t fileSize = static_cast<uint64_t>(file.tellg()); // Gets the file size
    file.seekg(0, ios::beg); // Moves file pointer back to the beginning

    sendAll(sock, reinterpret_cast<const char *>(&fileSize), sizeof(fileSize)); // Sends file size to server

    char buffer[BUFFER_SIZE]; // Buffer for reading file data
    uint32_t crc = 0; // Initializes CRC for integrity check
    while (file) // Continues until the entire file is read
    {
        file.read(buffer, sizeof(buffer)); // Reads data into buffer
        streamsize bytesRead = file.gcount(); // Gets the number of bytes read
        if (bytesRead > 0) // Checks if data was read
        {
            crc = crc32(buffer, bytesRead, crc); // Updates CRC with read data
            sendAll(sock, buffer, static_cast<int>(bytesRead)); // Sends data to server
        }
    }

    uint32_t serverCRC = 0; // Stores the CRC received from the server
    if (!recvExact(sock, reinterpret_cast<char *>(&serverCRC), sizeof(serverCRC))) // Receives server's CRC
        return false; // Returns false if receiving CRC fails

    cout << "[Client] Upload CRC: computed=" << crc << ", server=" << serverCRC << "\n"; // Prints computed and server CRCs
    if (crc == serverCRC) // Checks if CRCs match
        cout << "[Client] Integrity verified.\n"; // Prints success message
    else
        cout << "[Client] Integrity mismatch!\n"; // Prints failure message

    file.close(); // Closes the input file
    return true; // Returns true to indicate successful upload
}

// Main function, entry point of the program
int main()
{
    WSADATA wsaData; // Structure to hold Winsock initialization data
    WSAStartup(MAKEWORD(2, 2), &wsaData); // Initializes Winsock version 2.2

    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0); // Creates a TCP socket
    sockaddr_in serverAddr{}; // Structure to hold server address information
    serverAddr.sin_family = AF_INET; // Sets address family to IPv4
    serverAddr.sin_port = htons(PORT); // Sets port number (converts to network byte order)
    inet_pton(AF_INET, SERVER_IP, &serverAddr.sin_addr); // Converts IP string to binary

    if (connect(sock, (sockaddr *)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) // Connects to the server
    {
        cerr << "[Client] Connection failed.\n"; // Prints error message if connection fails
        return 1; // Exits with error code
    }

    // Receive UUID from server
    uint32_t uuidLen = 0; // Stores the length of the UUID
    if (!recvExact(sock, reinterpret_cast<char *>(&uuidLen), sizeof(uuidLen))) // Receives UUID length
        return 1; // Exits with error code if receiving fails

    string clientUUID(uuidLen, 0); // Creates a string to hold the UUID
    if (!recvExact(sock, clientUUID.data(), uuidLen)) // Receives the UUID
        return 1; // Exits with error code if receiving fails

    cout << "[Client] Connected. UUID: " << clientUUID << "\n"; // Prints the assigned UUID

    while (true) // Main loop for user commands
    {
        cout << "Enter command (D=Download, U=Upload, Q=Quit): "; // Prompts user for command
        char cmd; // Stores the user command
        cin >> cmd; // Reads the command from user input
        send(sock, &cmd, 1, 0); // Sends the command to the server

        if (cmd == 'D') // If user selects download
            downloadFile(sock); // Calls the download function
        else if (cmd == 'U') // If user selects upload
            uploadFile(sock); // Calls the upload function
        else if (cmd == 'Q') // If user selects quit
            break; // Exits the loop
        else
            cout << "[Client] Unknown command.\n"; // Prints error for invalid command
    }

    closesocket(sock); // Closes the socket
    WSACleanup(); // Cleans up Winsock resources
    return 0; // Exits the program successfully
}
