// sender.cpp
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iostream>
#include <fstream>
#include <cstring>
#include <string>
#include <chrono>
#include <queue>
#include <iomanip>
using namespace std;

// 打开log.txt文件，使用ofstream对象
ofstream logFile("log.txt", ios::out | ios::app);  // ios::out 打开文件用于写，ios::app 表示追加内容

int seq_num_share = 37;                     // 共享自己发送序号
int ack_num_expected = seq_num_share + 1;   // 预期对方确认序号
int timeout_ms = 1000;                      // 超时重传时间
int Congestion_Avoidance_Count = 0;         // 拥塞避免计数
enum State { Slow_Start, Congestion_Avoidance, Quick_Recovery };
State Reno_State = Slow_Start;              // RENO算法的状态机
int dupACKcount = 0;                        // 重复ACK的次数记录
int ssthresh = 16;                          // 拥塞控制的慢启动阈值
int window_size = 1;                        // 设置窗口大小
bool Congestion_Control = false;             // 是否拥塞控制

#define SERVER_PORT 20000       // 服务器端口
#define CLIENT_PORT 10000       // 客户端端口
#define IPADDR "127.0.0.1"      // IP地址设置为127.0.0.1
#define MAX_FILENAME_SIZE 32    // 最大文件名称长度
#define MAX_DATA_SIZE 1024      // 最大发送数据长度

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

// 发送数据包（发送端）
int send_packet(SOCKET& sock, struct sockaddr_in& receiver_addr, Packet& pkt) {
    // 计算校验和
    pkt.check_sum = calculate_checksum(pkt.data);

    // 发送数据包
    int sent_len = sendto(sock, (char*)&pkt, sizeof(pkt), 0, (struct sockaddr*)&receiver_addr, sizeof(receiver_addr));

    if (sent_len == SOCKET_ERROR) {
        cout << "发送数据包失败" << endl;
        return -1;
    }
    else {
        cout << "发送数据包，发送序号：" << pkt.seq_num << "，确认序号：" << pkt.ack_num
            << ", 数据大小：" << pkt.data_len << ", 校验和：" << pkt.check_sum
            << "，ACK：" << pkt.ACK << "，SYN：" << pkt.SYN << "，FIN：" << pkt.FIN << endl;
        return 0;
    }
}

//接收数据包（发送端）
int receive_packet(SOCKET& sock, struct sockaddr_in& sender_addr, Packet& pkt_received) {
    int len = sizeof(sender_addr);
    char buffer[MAX_PACKET_SIZE];
    int recv_len = recvfrom(sock, buffer, sizeof(buffer), 0, (struct sockaddr*)&sender_addr, &len);

    if (recv_len == SOCKET_ERROR) {
        cout << "接收数据包失败" << endl;
        return 0;
    }

    // 提取数据包内容
    memcpy(&pkt_received, buffer, sizeof(pkt_received));

    // 计算校验和
    unsigned short calculated_checksum = calculate_checksum(pkt_received.data);

    // 检查校验和
    if (pkt_received.check_sum != calculated_checksum) {
        cout << "接收校验和：" << pkt_received.check_sum << "，计算校验和：" << calculated_checksum << "，校验和错误，丢弃数据包" << endl;
        return 0;
    }

    // 发送数据包的内容
    cout << "接收数据包，发送序号：" << pkt_received.seq_num << "，确认序号：" << pkt_received.ack_num
        << ", 数据大小：" << pkt_received.data_len << ", 校验和：" << pkt_received.check_sum
        << "，ACK：" << pkt_received.ACK << "，SYN：" << pkt_received.SYN << "，FIN：" << pkt_received.FIN << endl;

    // 检查确认序号（客户端发送数据包，服务器返回确认序号）
    if (pkt_received.ACK && !pkt_received.FIN && !pkt_received.SYN) { // 服务器返回确认序号
        if (pkt_received.ack_num == ack_num_expected) { // 如果恰好是预期的ACK
            // 更新预期确认序号
            ack_num_expected++; // 接收到一个ACK，滑动窗口 (pkt_received.ack_num + 1 - ack_num_expected)
            return 1; // 接收到一个ACK，滑动窗口 (pkt_received.ack_num + 1 - ack_num_expected)
        }
        else if (pkt_received.ack_num > ack_num_expected) { // 累积确认实现
            // 更新预期确认序号
            ack_num_expected = pkt_received.ack_num + 1; // 接收到一个ACK，滑动窗口 (pkt_received.ack_num + 1 - ack_num_expected)
            return (pkt_received.ack_num + 1 - ack_num_expected); // 接收到一个ACK，滑动窗口 (pkt_received.ack_num + 1 - ack_num_expected)
        }
        else if (pkt_received.ack_num < ack_num_expected) { //快速重传 // 例如预期：ACK3，但是返回ACK2、ACK2、ACK2
            return -1;
        }
    }
    // 接收超时
    return 0;
}

// 发送文件
double send_file(SOCKET& sock, struct sockaddr_in& receiver_addr, string filename) {
    // 创建发送队列，实现滑动窗口流量控制
    queue<Packet> send_queue;

    // 超时重传次数
    int timeout = 0;

    // 快速重传次数
    int quickcount = 0;

    // 记录文件的开始时间
    auto start_time = chrono::high_resolution_clock::now();

    // 打开文件
    ifstream file("send/" + filename, ios::binary);
    if (!file.is_open()) {
        cout << "打开文件失败" << endl;
        return -1;
    }

    // 获取文件大小用以计算吞吐率
    file.seekg(0, ios::end); // 移动到文件末尾
    long file_size = file.tellg(); // 获取文件大小
    file.seekg(0, ios::beg); // 将文件指针重置到文件开头

    // 设置接收超时时间，单位为毫秒
    int timeout_ms = 100;  // 毫秒超时
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout_ms, sizeof(timeout_ms));

    while (!file.eof() || !send_queue.empty()) { // 读取文件，滑动窗口发送数据包，接收确认数据包

        // 发送窗口填充数据包
        while (send_queue.size() < window_size && !file.eof()) {
            Packet pkt;
            strncpy_s(pkt.name, sizeof(pkt.name), filename.c_str(), sizeof(pkt.name) - 1);  // 设置文件名称
            pkt.name[sizeof(pkt.name) - 1] = '\0';  // 文件名称
            file.read(pkt.data, sizeof(pkt.data));  // 读取数据
            int read_len = file.gcount();           // 读取长度
            pkt.data_len = read_len;                // 数据长度
            pkt.seq_num = seq_num_share++;          // 发送序号
            send_queue.push(pkt);                   // 添加队列
            cout << "发送窗口填入新数据包" << endl;
            int able_num = window_size - send_queue.size() > 0 ? window_size - send_queue.size() : 0;
            cout << "当前发送窗口大小：" << window_size << "，发送窗口阈值：" << ssthresh
                << "，已用分组数量：" << send_queue.size() << "，可用分组数量：" << able_num << endl;
        }

        // 更新预期确认序号
        ack_num_expected = send_queue.front().seq_num + 1;

        // 发送窗口发送数据包
        queue<Packet> tempQueue = send_queue;  // 复制队列，避免改变原队列内容
        for (int i = 0; i < window_size && !tempQueue.empty(); i++) {
            send_packet(sock, receiver_addr, tempQueue.front());  // 发送队头数据包
            tempQueue.pop();  // 删除队头数据包（不会影响原队列）
        }

        // 发送窗口接收数据包
        int receive_num = send_queue.size();
        // 如果接收服务器的数据包超时（1ms），就重新把发送窗口里面剩下的数据包发送一次
        for (int i = 0; i < receive_num; i++) {
            Packet pkt_received;
            int result = receive_packet(sock, receiver_addr, pkt_received);
            if (result > 0) { // 成功接收
                for (int i = 0; i < result; i++) {
                    send_queue.pop();
                    if (Reno_State == Slow_Start && Congestion_Control) { // 慢启动阶段
                        window_size++; // 设置窗口大小
                        dupACKcount = 0; // 设置重复ACK次数
                        if (window_size > ssthresh) {
                            Reno_State = Congestion_Avoidance; // 拥塞避免阶段
                            Congestion_Avoidance_Count = 0; // 清空拥塞避免阶段增加窗口大小的计数器
                            cout << "发送端进入拥塞避免状态" << endl;
                        }
                    }
                    else if (Reno_State == Congestion_Avoidance && Congestion_Control) { // 拥塞避免阶段
                        Congestion_Avoidance_Count++;
                        cout << "当前拥塞避免阶段关于增加窗口大小的进度为：" << Congestion_Avoidance_Count << "/" << window_size << endl;
                        if (Congestion_Avoidance_Count >= window_size) {
                            Congestion_Avoidance_Count = 0; // 清空拥塞避免阶段增加窗口大小的计数器
                            window_size++; // 设置窗口大小
                        }
                        dupACKcount = 0; // 设置重复ACK次数
                    }
                    else if(Congestion_Control) { // Reno_State == Quick_Recovery // 快速恢复阶段
                        Reno_State = Congestion_Avoidance; // 拥塞避免阶段
                        cout << "发送端进入拥塞避免状态" << endl;
                        window_size = ssthresh; // 设置窗口大小
                        dupACKcount = 0; // 设置重复ACK次数
                    }
                    cout << "接收窗口确认新数据包" << endl;
                    int able_num = window_size - send_queue.size() > 0 ? window_size - send_queue.size() : 0;
                    cout << "当前发送窗口大小：" << window_size << "，发送窗口阈值：" << ssthresh
                        << "，已用分组数量：" << send_queue.size() << "，可用分组数量：" << able_num << endl;
                }
            }
            else if (result == -1) { // 快速重传
                if (Reno_State == Quick_Recovery && Congestion_Control) {
                    window_size++; // 设置窗口大小
                }
                else if(Congestion_Control) {
                    dupACKcount++;
                    if (dupACKcount == 3) {
                        quickcount++;
                        Reno_State = Quick_Recovery; // 快速恢复阶段
                        cout << "发送端进入快速恢复状态" << endl;
                        ssthresh = window_size / 2 > 0 ? window_size / 2 : 1; // 设置阈值
                        window_size = ssthresh + 3; // 设置窗口大小
                        dupACKcount = 0; // 设置重复ACK次数
                        int able_num = window_size - send_queue.size() > 0 ? window_size - send_queue.size() : 0;
                        cout << "当前发送窗口大小：" << window_size << "，发送窗口阈值：" << ssthresh
                            << "，已用分组数量：" << send_queue.size() << "，可用分组数量：" << able_num << endl;
                        break; // 下一次 while 循环，重新发送窗口内数据包
                    }
                }
            }
            else if(Congestion_Control) { // 超时处理：如果接收超时，进入慢启动阶段
                timeout++; // 记录超时次数
                cout << "ACK超时，重新发送窗口内数据包，当前累积重传次数：" << timeout << endl;
                Reno_State = Slow_Start; // 慢启动阶段
                cout << "发送端进入慢启动状态" << endl;
                ssthresh = window_size / 2 > 0 ? window_size / 2 : 1; // 设置阈值
                window_size = 1; // 设置窗口大小
                dupACKcount = 0; // 设置重复ACK次数
                int able_num = window_size - send_queue.size() > 0 ? window_size - send_queue.size() : 0;
                cout << "当前发送窗口大小：" << window_size << "，发送窗口阈值：" << ssthresh
                    << "，已用分组数量：" << send_queue.size() << "，可用分组数量：" << able_num << endl;
                break; // 下一次 while 循环，重新发送窗口内数据包
            }
        }
    }

    // 记录文件传输结束时间
    auto end_time = chrono::high_resolution_clock::now();
    chrono::duration<double> duration = end_time - start_time;

    // 输出超时重传次数
    cout << "超时重传次数为：" << timeout << " 次" << endl;

    // 输出快速重传次数
    cout << "快速重传次数为：" << quickcount << " 次" << endl;

    // 计算吞吐率（文件大小 / 传输时间）
    double throughput = (double)file_size / duration.count(); // 吞吐率，单位字节/秒
    cout << "文件传输时间: " << duration.count() << " 秒" << endl;
    cout << "吞吐率: " << throughput / 1024 << " KB/s" << endl; // 吞吐率单位：KB/s

    file.close();

    return duration.count();
}

// 发送端主函数

#pragma comment(lib,"ws2_32.lib")
int main() {
    // 初始化WinSock
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        cout << "WinSock 初始化失败" << endl;
        return -1;
    }

    // 创建UDP套接字
    SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET) {
        cout << "创建套接字失败" << endl;
        return -1;
    }

    // 创建接收方地址
    sockaddr_in receiver_addr;
    receiver_addr.sin_family = AF_INET;  // 设置地址族为IPv4
    receiver_addr.sin_port = htons(SERVER_PORT);  // 设置服务器端口号为20000
    receiver_addr.sin_addr.s_addr = inet_addr(IPADDR);  // 设置IP地址为127.0.0.1

    // 创建本地客户端地址
    sockaddr_in client_addr;
    client_addr.sin_family = AF_INET;  // 设置地址族为IPv4
    client_addr.sin_port = htons(CLIENT_PORT);  // 设置客户端端口号为10000
    client_addr.sin_addr.s_addr = inet_addr(IPADDR);  // 设置客户端IP地址为127.0.0.1

    // 绑定客户端套接字到本地端口
    if (bind(sock, (struct sockaddr*)&client_addr, sizeof(client_addr)) == SOCKET_ERROR) {
        cout << "绑定套接字失败" << endl;
        return -1;
    }

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
    string filename = "1.jpg";

    // 窗口大小设置
    int windows[11] = { 1,10,20,30,40,50,60,70,80,90,100 };

    // 发送时间记录
    double times[11]{ 0,0,0,0,0,0,0,0,0,0,0 };
    
    // 循环发送文件
    for (int i = 0; i < 11; i++) {
        // 持续读取用户输入的文件名
        //cout << "请输入要发送的文件名（输入exit退出）: ";
        //getline(cin, filename);
        // 设置窗口大小
        window_size = windows[i];
        if (filename == "exit")
            break;  // 用户输入exit时退出
        else {
            // 获得发送文件的
            times[i] = send_file(sock, receiver_addr, filename);  // 调用send_file发送文件
        }
    }

    // 输出滑动窗口大小、传输时间和吞吐率
    std::cout << "滑动窗口大小、传输文件时间与吞吐率:\n";
    std::cout << std::setw(15) << "窗口大小" << std::setw(20) << "传输时间 (秒)" << std::setw(20) << "吞吐率 (字节/秒)" << std::endl;
    for (int i = 0; i < 11; i++) {
        // 计算吞吐率：吞吐率 = 1814.0 / 传输时间
        double throughput = 1814.0 / times[i];

        // 输出窗口大小、传输时间和吞吐率
        std::cout << std::setw(15) << windows[i]
            << std::setw(20) << times[i]
            << std::setw(20) << throughput << std::endl;
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