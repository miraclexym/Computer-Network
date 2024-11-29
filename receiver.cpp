// receiver.cpp
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iostream>
#include <fstream>
#include <cstring>
#include <windows.h>

using namespace std;

#define PORT 12345
#define MAX_FILENAME_SIZE 32
#define MAX_DATA_SIZE 1024

int seq_num_share = 61;
bool my_connect = false;
bool my_disconnect = false;

// 数据包结构
struct Packet {
    int seq_num = 0;                    // 发送序号
    int ack_num = 0;                    // 确认序号
    char name[MAX_FILENAME_SIZE];       // 文件名称
    char data[MAX_DATA_SIZE];           // 发送数据
    int data_len = 0;                   // 数据长度
    int check_sum = 0;                  // 校验和
    bool ACK = false;                   // 标志位
    bool SYN = false;                   // 标志位
    bool FIN = false;                   // 标志位
};

#define MAX_PACKET_SIZE (sizeof(Packet))

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

// 发送数据包
void send_packet(SOCKET& sock, struct sockaddr_in& receiver_addr, Packet& pkt) {
    // 计算校验和
    pkt.check_sum = calculate_checksum(pkt.data);

    // 发送数据包
    int sent_len = sendto(sock, (char*)&pkt, sizeof(pkt), 0, (struct sockaddr*)&receiver_addr, sizeof(receiver_addr));

    if (sent_len == SOCKET_ERROR) {
        cerr << "发送数据包失败" << endl;
    }
    else {
        cout << "发送数据包，发送序号：" << pkt.seq_num << "，确认序号：" << pkt.ack_num
            << ", 数据大小：" << pkt.data_len << ", 校验和：" << pkt.check_sum
            << "，ACK：" << pkt.ACK << "，SYN：" << pkt.SYN << "，FIN：" << pkt.FIN << endl;
    }
}

//接收数据包
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

    // 校验和出错
    if (pkt_received.check_sum != calculated_checksum) {
        cerr << "接收校验和：" << pkt_received.check_sum << "，计算校验和：" << calculated_checksum << "，校验和错误，丢弃数据包";
        return -1;
    }

    cout << "接收数据包，发送序号：" << pkt_received.seq_num << "，确认序号：" << pkt_received.ack_num
        << ", 数据大小：" << pkt_received.data_len << ", 校验和：" << pkt_received.check_sum
        << "，ACK：" << pkt_received.ACK << "，SYN：" << pkt_received.SYN << "，FIN：" << pkt_received.FIN << endl;

    return 0;
}

// 处理数据包
int handle_packet(SOCKET& sock, struct sockaddr_in& sender_addr) {

    // 接收数据包
    Packet pkt_received;
    receive_packet(sock, sender_addr, pkt_received);

    // 处理三次握手
    if (pkt_received.SYN) {
        my_connect = true;
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
        if (my_connect) {
            my_connect = false;
            return 0;
        }
        if (my_disconnect) {
            my_disconnect = false;
            return -1;
        }
        return 0;
    }

    // 处理四次挥手
    if (pkt_received.FIN) {
        my_disconnect = true;
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

    // 将数据写入文件
    ofstream file("receive/" + string(pkt_received.name), ios::binary | ios::app);
    if (file.is_open()) {
        file.write(pkt_received.data, pkt_received.data_len);
        file.close();
        cout << "数据已写入文件。路径：receive/" + string(pkt_received.name) << endl;
    }
    else {
        cerr << "写入文件失败" << endl;
    }

    // 发送ACK包
    Packet pkt;
    pkt.ACK = true;
    pkt.seq_num = seq_num_share++;
    pkt.ack_num = pkt_received.seq_num + 1;
    send_packet(sock, sender_addr, pkt);
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

    // 创建对方地址
    sockaddr_in sender_addr;
    sender_addr.sin_family = AF_INET;  // 设置地址族为IPv4
    sender_addr.sin_port = htons(PORT);  // 设置端口号
    sender_addr.sin_addr.s_addr = inet_addr("127.0.0.1");  // 设置IP地址

    // 绑定套接字
    if (bind(sock, (struct sockaddr*)&sender_addr, sizeof(sender_addr)) == SOCKET_ERROR) {
        cerr << "绑定套接字失败" << endl;
        return -1;
    }

    // 循环处理
    while (true) {
        int end = handle_packet(sock, sender_addr);  // 接收数据包并处理
        if (end == -1)
            break;
    }

    closesocket(sock);
    WSACleanup();

    return 0;
}