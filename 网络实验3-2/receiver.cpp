// receiver.cpp
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iostream>
#include <fstream>
#include <cstring>
#include <windows.h>
#include <queue>

using namespace std;

int seq_num_share = 61;         // 共享自己发送序号
int seq_num_expected = 37;      // 预期对方发送序号
bool three_handshakes = false;  // 三次握手标志位
bool waving_four_times = false; // 四次挥手标志位

#define SERVER_PORT 20000       // 服务器端口
#define CLIENT_PORT 10000       // 客户端端口
#define IPADDR "127.0.0.1"      // IP地址设置为127.0.0.1
#define MAX_FILENAME_SIZE 32    // 最大文件名称长度
#define MAX_DATA_SIZE 1024      // 最大发送数据长度
#define WINDOW_SIZE 1           // 滑动窗口大小

// 数据包结构
struct Packet {
    int seq_num = 0;                    // 发送序号
    int ack_num = 0;                    // 确认序号
    char name[MAX_FILENAME_SIZE];       // 文件名称
    char data[MAX_DATA_SIZE];           // 发送数据
    int data_len = 0;                   // 数据长度
    int check_sum = 0;                  // 校验和
    bool ACK = false;                   // ACK_标志位
    bool SYN = false;                   // SYN_标志位
    bool FIN = false;                   // FIN_标志位
};

#define MAX_PACKET_SIZE (sizeof(Packet))    // 最大数据包长度

// 计算校验和（反码求和）
unsigned short calculate_checksum(char* data) {
    unsigned short len = strlen(data);
    unsigned short checksum = 0;
    unsigned short* ptr = (unsigned short*)data;  // 将数据视为16位单位的数组

    // 对数据进行16位加法求和
    for (int i = 0; i < len / 2; i++) {
        checksum += ptr[i];

        // 如果有进位，进行回卷
        if (checksum > 0xFFFF) {
            checksum = (checksum & 0xFFFF) + 1;
        }
    }

    // 如果数据长度是奇数，处理剩余的最后一个字节
    if (len % 2 != 0) {
        checksum += (unsigned short)(data[len - 1] << 8);

        // 如果有进位，进行回卷
        if (checksum > 0xFFFF) {
            checksum = (checksum & 0xFFFF) + 1;
        }
    }

    // 返回反码
    return ~checksum;
}

// 发送数据包（接收端）
int send_packet(SOCKET& sock, struct sockaddr_in& receiver_addr, Packet& pkt) {
    // 计算校验和
    pkt.check_sum = calculate_checksum(pkt.data);

    // 发送数据包
    int sent_len = sendto(sock, (char*)&pkt, sizeof(pkt), 0, (struct sockaddr*)&receiver_addr, sizeof(receiver_addr));

    if (sent_len == SOCKET_ERROR) {
        cerr << "发送数据包失败" << endl;
        return -1;
    }
    else {
        cout << "发送数据包，发送序号：" << pkt.seq_num << "，确认序号：" << pkt.ack_num
            << ", 数据大小：" << pkt.data_len << ", 校验和：" << pkt.check_sum
            << "，ACK：" << pkt.ACK << "，SYN：" << pkt.SYN << "，FIN：" << pkt.FIN << endl;
        return 0;
    }
}

//接收数据包（接收端）
int receive_packet(SOCKET& sock, struct sockaddr_in& sender_addr, Packet& pkt_received) {
    int len = sizeof(sender_addr);
    char buffer[MAX_PACKET_SIZE];
    int recv_len = recvfrom(sock, buffer, sizeof(buffer), 0, (struct sockaddr*)&sender_addr, &len);

    if (recv_len == SOCKET_ERROR) {
        cerr << "接收数据包失败" << endl;
        return -1;
    }

    // 提取数据包内容
    memcpy(&pkt_received, buffer, sizeof(pkt_received));

    // 计算校验和
    unsigned short calculated_checksum = calculate_checksum(pkt_received.data);

    // 检查校验和
    if (pkt_received.check_sum != calculated_checksum) {
        cerr << "接收校验和：" << pkt_received.check_sum << "，计算校验和：" << calculated_checksum << "，校验和错误，丢弃数据包" << endl;
        return -1;
    }

    // 接收到错误的数据包
    if (pkt_received.seq_num > seq_num_expected) {
        cerr << "期望序号：" << seq_num_expected << "，接收序号：" << pkt_received.seq_num << "，序号不正确，丢弃数据包" << endl;
        return -1;
    }

    // 发送数据包的内容
    cout << "接收数据包，发送序号：" << pkt_received.seq_num << "，确认序号：" << pkt_received.ack_num
        << ", 数据大小：" << pkt_received.data_len << ", 校验和：" << pkt_received.check_sum
        << "，ACK：" << pkt_received.ACK << "，SYN：" << pkt_received.SYN << "，FIN：" << pkt_received.FIN << endl;

    // 接收到正确的数据包
    if (pkt_received.seq_num == seq_num_expected) {
        // 更新预期发送序号
        seq_num_expected++;
        return 0;
    }

    // 接收到重复的数据包
    if (pkt_received.seq_num < seq_num_expected) {
        return 1;
    }
}

// 处理数据包
int handle_packet(SOCKET& sock, struct sockaddr_in& sender_addr, queue<Packet>& receive_queue) {

    // 接收数据包
    Packet pkt_received;
    int ret = receive_packet(sock, sender_addr, pkt_received);

    // 特殊情况判断
    if (ret == -1) { // 接收到错误的数据包
        return 0;
    }
    if (ret == 1) { // 接收到重复的数据包
        // 发送ACK包
        Packet pkt;
        pkt.ACK = true;
        pkt.seq_num = seq_num_share++;
        pkt.ack_num = pkt_received.seq_num + 1;
        send_packet(sock, sender_addr, pkt);
        return 0;
    }

    // 处理三次握手
    if (pkt_received.SYN) {
        three_handshakes = true;
        // 收到SYN包，发送SYN-ACK包
        Packet pkt;
        pkt.SYN = true;
        pkt.ACK = true;
        pkt.seq_num = seq_num_share++;
        pkt.ack_num = pkt_received.seq_num + 1;
        send_packet(sock, sender_addr, pkt);
        return 0;
    }

    // 处理三次握手和处理四次挥手
    if (pkt_received.ACK && pkt_received.ack_num == seq_num_share) {
        if (three_handshakes) {
            three_handshakes = false;
            // 更新预期发送序号
            seq_num_expected = pkt_received.seq_num + 1;
            return 0;       // 如果是三次握手的第三步ACK那么就return 0
        }
        if (waving_four_times) {
            waving_four_times = false;
            return -1;      // 如果是四次挥手的第四步ACK那么就return -1
        }
        return 0;
    }

    // 处理四次挥手
    if (pkt_received.FIN) {
        waving_four_times = true;
        // 收到FIN包，发送ACK包
        Packet pkt1;
        pkt1.ACK = true;
        pkt1.seq_num = seq_num_share++;
        pkt1.ack_num = pkt_received.seq_num + 1;
        send_packet(sock, sender_addr, pkt1);
        // 收到FIN包，发送FIN-ACK包
        Packet pkt2;
        pkt2.FIN = true;
        pkt2.ACK = true;
        pkt2.seq_num = seq_num_share++;
        pkt2.ack_num = pkt_received.seq_num + 1;
        send_packet(sock, sender_addr, pkt2);
        return 0;
    }

    // 不是三次握手和四次挥手，那就将数据包存入接收窗口
    if (receive_queue.size() < WINDOW_SIZE) {
        receive_queue.push(pkt_received);
    }

    // 接收窗口满了或者文件传输完毕，把数据写入到文件中
    if (receive_queue.size() == WINDOW_SIZE || pkt_received.data_len < MAX_DATA_SIZE) {
        while (!receive_queue.empty()) {
            // 写入数据到文件中
            Packet tempPkt = receive_queue.front();
            receive_queue.pop();
            ofstream file("receive/" + string(tempPkt.name), ios::binary | ios::app);
            if (file.is_open()) {
                file.write(tempPkt.data, tempPkt.data_len);
                file.close();
                cout << "数据已写入文件。路径：receive/" + string(tempPkt.name) << endl;
            }
            else {
                cerr << "写入文件失败" << endl;
            }
            // 发送ACK包
            Packet pkt;
            pkt.ACK = true;
            pkt.seq_num = seq_num_share++;
            pkt.ack_num = tempPkt.seq_num + 1;
            send_packet(sock, sender_addr, pkt);
        }
    }
    return 0;
}

// 接收端主函数
#pragma comment(lib,"ws2_32.lib")
int main() {
    // 初始化WinSock
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        cerr << "WinSock 初始化失败" << endl;
        return -1;
    }

    // 创建UDP套接字
    SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET) {
        cerr << "创建套接字失败" << endl;
        return -1;
    }

    // 创建服务器地址
    sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;  // 设置地址族为IPv4
    server_addr.sin_port = htons(SERVER_PORT);  // 设置端口号为30000
    server_addr.sin_addr.s_addr = inet_addr(IPADDR);  // 设置IP地址为127.0.0.1

    // 创建客户端地址
    sockaddr_in client_addr;
    client_addr.sin_family = AF_INET;  // 设置地址族为IPv4
    client_addr.sin_port = htons(CLIENT_PORT);  // 设置客户端端口号为20000
    client_addr.sin_addr.s_addr = inet_addr(IPADDR);  // 设置客户端IP地址为127.0.0.1

    // 绑定套接字
    if (bind(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        cerr << "绑定套接字失败" << endl;
        return -1;
    }

    // 创建接收队列，实现滑动窗口流量控制
    queue<Packet> receive_queue;

    // 循环处理
    while (true) {
        int end = handle_packet(sock, client_addr, receive_queue);  // 接收数据包并处理
        if (end == -1)
            break;
    }

    closesocket(sock);
    WSACleanup();

    return 0;
}