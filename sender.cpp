// sender.cpp

#include <iostream>
#include <fstream>
#include <cstring>
#include <winsock2.h>
#include <Ws2tcpip.h>
#include <string>
#include <chrono>

using namespace std;

#define PORT 12345
#define MAX_FILENAME_SIZE 32
#define MAX_DATA_SIZE 1024

int seq_num_share = 37;

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

int send_file(SOCKET& sock, struct sockaddr_in& receiver_addr, string filename) {

    // 超时重传次数
    int timeout = 0;

    // 记录文件的开始时间
    auto start_time = chrono::high_resolution_clock::now();

    // 数据传输
    ifstream file("send/" + filename, ios::binary);
    if (!file.is_open()) {
        cerr << "打开文件失败" << endl;
        return -1;
    }

    // 获取文件大小
    file.seekg(0, ios::end); // 移动到文件末尾
    long file_size = file.tellg(); // 获取文件大小
    file.seekg(0, ios::beg); // 将文件指针重置到文件开头

    while (!file.eof()) {
        Packet pkt;
        // 设置文件名
        strncpy_s(pkt.name, sizeof(pkt.name), filename.c_str(), sizeof(pkt.name) - 1);
        pkt.name[sizeof(pkt.name) - 1] = '\0'; // 确保文件名以'\0'结尾

        // 读取数据到数据包中
        file.read(pkt.data, sizeof(pkt.data));
        int read_len = file.gcount();
        pkt.data_len = read_len;
        pkt.seq_num = seq_num_share++;

        // 发送数据包
        send_packet(sock, receiver_addr, pkt);

        // 等待ACK并处理超时重传
        struct sockaddr_in sender_addr;
        int sender_len = sizeof(sender_addr);
        char buffer[MAX_PACKET_SIZE];
        int ack_len = recvfrom(sock, buffer, sizeof(buffer), 0, (struct sockaddr*)&sender_addr, &sender_len);

        if (ack_len == SOCKET_ERROR) {
            cerr << "接收ACK超时，重新发送数据包：" << pkt.seq_num << endl;
            timeout++;
            seq_num_share--;
            continue;  // 超时重传
        }

        Packet pkt_received;
        memcpy(&pkt_received, buffer, sizeof(pkt_received));

        if (pkt_received.ACK && pkt_received.data_len == 0) {
            cout << "接收数据包，发送序号：" << pkt_received.seq_num << "，确认序号：" << pkt_received.ack_num
                << ", 数据大小：" << pkt_received.data_len << ", 校验和：" << pkt_received.check_sum
                << "，ACK：" << pkt_received.ACK << "，SYN：" << pkt_received.SYN << "，FIN：" << pkt_received.FIN << endl;
        }
    }

    // 记录文件传输结束时间
    auto end_time = chrono::high_resolution_clock::now();
    chrono::duration<double> duration = end_time - start_time;

    // 输出超时重传次数
    cout << "超时重传次数为：" << timeout << " 次" << endl;

    // 计算吞吐率（文件大小 / 传输时间）
    double throughput = (double)file_size / duration.count(); // 吞吐率，单位字节/秒
    cout << "文件传输时间: " << duration.count() << " 秒" << endl;
    cout << "吞吐率: " << throughput / 1024 << " KB/s" << endl; // 吞吐率单位：KB/s

    file.close();

    return 0;
}

// 发送端主函数

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
    sockaddr_in receiver_addr;
    receiver_addr.sin_family = AF_INET;  // 设置地址族为IPv4
    receiver_addr.sin_port = htons(PORT);  // 设置端口号
    inet_pton(AF_INET, "127.0.0.1", &receiver_addr.sin_addr.s_addr);  // 设置IP地址

    // 三次握手第一步：发送SYN包
    Packet pkt0;
    pkt0.SYN = true;
    pkt0.seq_num = seq_num_share++;
    // cout << "发送SYN包，请求建立连接" << endl;
    send_packet(sock, receiver_addr, pkt0);

    // 三次握手第二步：接收SYN-ACK包
    Packet pkt_received0;
    receive_packet(sock, receiver_addr, pkt_received0);

    // 三次握手第三步：发送ACK包
    if (pkt_received0.SYN && pkt_received0.ACK && pkt_received0.ack_num == seq_num_share) {
        Packet pkt1;
        pkt1.ACK = true;
        pkt1.seq_num = seq_num_share++;
        pkt1.ack_num = pkt_received0.seq_num + 1;
        send_packet(sock, receiver_addr, pkt1);
    }

    // 创建文件名称
    string filename;

    // 循环发送文件
    while (true) {
        // 持续读取用户输入的文件名
        cout << "请输入要发送的文件名（输入exit退出）: ";
        getline(cin, filename);

        if (filename == "exit")
            break;  // 用户输入exit时退出
        else
            send_file(sock, receiver_addr, filename);  // 调用send_file发送文件
    }

    // 发送完文件后，开始四次挥手过程

    // 四次挥手第一步：发送FIN包
    Packet pkt2;
    pkt2.FIN = true;
    pkt2.seq_num = seq_num_share++;
    send_packet(sock, receiver_addr, pkt2);

    // 四次挥手第二步：接收ACK包
    Packet pkt_received1;
    receive_packet(sock, receiver_addr, pkt_received1);

    // 四次挥手第三步：接收FIN-ACK包
    Packet pkt_received2;
    receive_packet(sock, receiver_addr, pkt_received2);

    // 四次挥手第四步：发送ACK包
    Packet pkt3;
    pkt3.ACK = true;
    pkt3.seq_num = seq_num_share++;
    pkt3.ack_num = pkt_received2.seq_num + 1;
    send_packet(sock, receiver_addr, pkt3);

    closesocket(sock);
    WSACleanup();

    return 0;
}