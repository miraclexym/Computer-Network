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

using namespace std;

int seq_num_share = 37;                     // �����Լ��������
int ack_num_expected = seq_num_share + 1;   // Ԥ�ڶԷ�ȷ�����
int timeout_ms = 1000;                      // ��ʱ�ش�ʱ��

#define SERVER_PORT 20000       // �������˿�
#define CLIENT_PORT 10000       // �ͻ��˶˿�
#define IPADDR "127.0.0.1"      // IP��ַ����Ϊ127.0.0.1
#define MAX_FILENAME_SIZE 32    // ����ļ����Ƴ���
#define MAX_DATA_SIZE 1024      // ��������ݳ���
#define WINDOW_SIZE 10          // �������ڴ�С

// ���ݰ��ṹ
struct Packet {
    int seq_num = 0;                    // �������
    int ack_num = 0;                    // ȷ�����
    char name[MAX_FILENAME_SIZE];       // �ļ�����
    char data[MAX_DATA_SIZE];           // ��������
    int data_len = 0;                   // ���ݳ���
    int check_sum = 0;                  // У���
    bool ACK = false;                   // ACK_��־λ
    bool SYN = false;                   // SYN_��־λ
    bool FIN = false;                   // FIN_��־λ
};

#define MAX_PACKET_SIZE (sizeof(Packet))    // ������ݰ�����

// ����У��ͣ�������ͣ�
unsigned short calculate_checksum(char* data) {
    unsigned short len = strlen(data);
    unsigned short checksum = 0;
    unsigned short* ptr = (unsigned short*)data;  // ��������Ϊ16λ��λ������

    // �����ݽ���16λ�ӷ����
    for (int i = 0; i < len / 2; i++) {
        checksum += ptr[i];

        // ����н�λ�����лؾ�
        if (checksum > 0xFFFF) {
            checksum = (checksum & 0xFFFF) + 1;
        }
    }

    // ������ݳ���������������ʣ������һ���ֽ�
    if (len % 2 != 0) {
        checksum += (unsigned short)(data[len - 1] << 8);

        // ����н�λ�����лؾ�
        if (checksum > 0xFFFF) {
            checksum = (checksum & 0xFFFF) + 1;
        }
    }

    // ���ط���
    return ~checksum;
}

// �������ݰ������Ͷˣ�
int send_packet(SOCKET& sock, struct sockaddr_in& receiver_addr, Packet& pkt) {
    // ����У���
    pkt.check_sum = calculate_checksum(pkt.data);

    // �������ݰ�
    int sent_len = sendto(sock, (char*)&pkt, sizeof(pkt), 0, (struct sockaddr*)&receiver_addr, sizeof(receiver_addr));

    if (sent_len == SOCKET_ERROR) {
        cerr << "�������ݰ�ʧ��" << endl;
        return -1;
    }
    else {
        cout << "�������ݰ���������ţ�" << pkt.seq_num << "��ȷ����ţ�" << pkt.ack_num
            << ", ���ݴ�С��" << pkt.data_len << ", У��ͣ�" << pkt.check_sum
            << "��ACK��" << pkt.ACK << "��SYN��" << pkt.SYN << "��FIN��" << pkt.FIN << endl;
        return 0;
    }
}

//�������ݰ������Ͷˣ�
int receive_packet(SOCKET& sock, struct sockaddr_in& sender_addr, Packet& pkt_received) {
    int len = sizeof(sender_addr);
    char buffer[MAX_PACKET_SIZE];
    int recv_len = recvfrom(sock, buffer, sizeof(buffer), 0, (struct sockaddr*)&sender_addr, &len);

    if (recv_len == SOCKET_ERROR) {
        cerr << "�������ݰ�ʧ��" << endl;
        return -1;
    }

    // ��ȡ���ݰ�����
    memcpy(&pkt_received, buffer, sizeof(pkt_received));

    // ����У���
    unsigned short calculated_checksum = calculate_checksum(pkt_received.data);

    // ���У���
    if (pkt_received.check_sum != calculated_checksum) {
        cerr << "����У��ͣ�" << pkt_received.check_sum << "������У��ͣ�" << calculated_checksum << "��У��ʹ��󣬶������ݰ�" << endl;
        return -1;
    }

    // ���ȷ������Ƿ���ȷ���ͻ��˷������ݰ�������������ȷ����ţ�
    if (pkt_received.ACK && pkt_received.ack_num != ack_num_expected && !pkt_received.FIN && !pkt_received.SYN) {
        cout << "Ԥ��ȷ����ţ�" << ack_num_expected << "��ʵ��ȷ����ţ�" << pkt_received.ack_num << "��ȷ����Ų���ȷ���������ݰ�" << endl;
        return -1;
    }

    // ����Ԥ��ȷ�����
    ack_num_expected++;

    // �������ݰ�������
    cout << "�������ݰ���������ţ�" << pkt_received.seq_num << "��ȷ����ţ�" << pkt_received.ack_num
        << ", ���ݴ�С��" << pkt_received.data_len << ", У��ͣ�" << pkt_received.check_sum
        << "��ACK��" << pkt_received.ACK << "��SYN��" << pkt_received.SYN << "��FIN��" << pkt_received.FIN << endl;

    return 0;
}

// �����ļ�
int send_file(SOCKET& sock, struct sockaddr_in& receiver_addr, string filename) {
    // �������Ͷ��У�ʵ�ֻ���������������
    queue<Packet> send_queue;

    // ��ʱ�ش�����
    int timeout = 0;

    // ��¼�ļ��Ŀ�ʼʱ��
    auto start_time = chrono::high_resolution_clock::now();

    // ���ļ�
    ifstream file("send/" + filename, ios::binary);
    if (!file.is_open()) {
        cerr << "���ļ�ʧ��" << endl;
        return -1;
    }

    // ��ȡ�ļ���С���Լ���������
    file.seekg(0, ios::end); // �ƶ����ļ�ĩβ
    long file_size = file.tellg(); // ��ȡ�ļ���С
    file.seekg(0, ios::beg); // ���ļ�ָ�����õ��ļ���ͷ

    // ���ý��ճ�ʱʱ�䣬��λΪ����
    int timeout_ms = 100;  // ���볬ʱ
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout_ms, sizeof(timeout_ms));

    while (!file.eof()) { // ��ȡ�ļ����������ڷ������ݰ�������ȷ�����ݰ�
        // ����Ԥ��ȷ�����
        ack_num_expected = seq_num_share + 1;

        // ���ʹ���������ݰ�
        while (send_queue.size() < WINDOW_SIZE && !file.eof()) {
            Packet pkt;
            strncpy_s(pkt.name, sizeof(pkt.name), filename.c_str(), sizeof(pkt.name) - 1);  // �����ļ�����
            pkt.name[sizeof(pkt.name) - 1] = '\0';  // �ļ�����
            file.read(pkt.data, sizeof(pkt.data));  // ��ȡ����
            int read_len = file.gcount();           // ��ȡ����
            pkt.data_len = read_len;                // ���ݳ���
            pkt.seq_num = seq_num_share++;          // �������
            send_queue.push(pkt);                   // ��Ӷ���
            cout << "���ʹ������������ݰ������ʹ��ڴ�С��" << WINDOW_SIZE 
                << "�����÷���������" << send_queue.size() << "�����÷���������" << WINDOW_SIZE - send_queue.size() << endl;
        }

        // ���ʹ��ڷ������ݰ�
        queue<Packet> tempQueue = send_queue;  // ���ƶ��У�����ı�ԭ��������
        while (!tempQueue.empty()) {
            send_packet(sock, receiver_addr, tempQueue.front());  // ���Ͷ�ͷ���ݰ�
            tempQueue.pop();  // ɾ����ͷ���ݰ�������Ӱ��ԭ���У�
        }

        // ���ʹ��ڽ������ݰ�
        int receive_num = send_queue.size();
        // ������շ����������ݰ���ʱ��1ms���������°ѷ��ʹ�������ʣ�µ����ݰ�����һ��
        for (int i = 0; i < receive_num; i++) {
            Packet pkt_received;
            int result = receive_packet(sock, receiver_addr, pkt_received);
            if (result == 0) {
                send_queue.pop();
                cout << "���մ���ȷ�������ݰ������ʹ��ڴ�С��" << WINDOW_SIZE
                    << "�����÷���������" << send_queue.size() << "�����÷���������" << WINDOW_SIZE - send_queue.size() << endl;
            }
            else {
                // ��ʱ����������ճ�ʱ��������ش�
                timeout++;
                cout << "ACK��ʱ�����·��ʹ��������ݰ�����ǰ�ۻ��ش�������" << timeout << endl;
                // break; // ��һ�� while ѭ�������·��ʹ��������ݰ�
            }
        }
    }

    // ��¼�ļ��������ʱ��
    auto end_time = chrono::high_resolution_clock::now();
    chrono::duration<double> duration = end_time - start_time;

    // �����ʱ�ش�����
    cout << "��ʱ�ش�����Ϊ��" << timeout << " ��" << endl;

    // ���������ʣ��ļ���С / ����ʱ�䣩
    double throughput = (double)file_size / duration.count(); // �����ʣ���λ�ֽ�/��
    cout << "�ļ�����ʱ��: " << duration.count() << " ��" << endl;
    cout << "������: " << throughput / 1024 << " KB/s" << endl; // �����ʵ�λ��KB/s

    file.close();

    return 0;
}

// ���Ͷ�������

#pragma comment(lib,"ws2_32.lib")
int main() {
    // ��ʼ��WinSock
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        cerr << "WinSock ��ʼ��ʧ��" << endl;
        return -1;
    }

    // ����UDP�׽���
    SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET) {
        cerr << "�����׽���ʧ��" << endl;
        return -1;
    }

    // �������շ���ַ
    sockaddr_in receiver_addr;
    receiver_addr.sin_family = AF_INET;  // ���õ�ַ��ΪIPv4
    receiver_addr.sin_port = htons(SERVER_PORT);  // ���÷������˿ں�Ϊ20000
    receiver_addr.sin_addr.s_addr = inet_addr(IPADDR);  // ����IP��ַΪ127.0.0.1

    // �������ؿͻ��˵�ַ
    sockaddr_in client_addr;
    client_addr.sin_family = AF_INET;  // ���õ�ַ��ΪIPv4
    client_addr.sin_port = htons(CLIENT_PORT);  // ���ÿͻ��˶˿ں�Ϊ10000
    client_addr.sin_addr.s_addr = inet_addr(IPADDR);  // ���ÿͻ���IP��ַΪ127.0.0.1

    // �󶨿ͻ����׽��ֵ����ض˿�
    if (bind(sock, (struct sockaddr*)&client_addr, sizeof(client_addr)) == SOCKET_ERROR) {
        cerr << "���׽���ʧ��" << endl;
        return -1;
    }

    // �������ֵ�һ��������SYN��
    Packet pkt0;
    pkt0.SYN = true;
    pkt0.seq_num = seq_num_share++;
    // cout << "����SYN��������������" << endl;
    send_packet(sock, receiver_addr, pkt0);

    // �������ֵڶ���������SYN-ACK��
    Packet pkt_received0;
    receive_packet(sock, receiver_addr, pkt_received0);

    // �������ֵ�����������ACK��
    if (pkt_received0.SYN && pkt_received0.ACK && pkt_received0.ack_num == seq_num_share) {
        Packet pkt1;
        pkt1.ACK = true;
        pkt1.seq_num = seq_num_share++;
        pkt1.ack_num = pkt_received0.seq_num + 1;
        send_packet(sock, receiver_addr, pkt1);
    }

    // �����ļ�����
    string filename;

    // ѭ�������ļ�
    while (true) {
        // ������ȡ�û�������ļ���
        cout << "������Ҫ���͵��ļ���������exit�˳���: ";
        getline(cin, filename);

        if (filename == "exit")
            break;  // �û�����exitʱ�˳�
        else
            send_file(sock, receiver_addr, filename);  // ����send_file�����ļ�
    }

    // �������ļ��󣬿�ʼ�Ĵλ��ֹ���

    // �Ĵλ��ֵ�һ��������FIN��
    Packet pkt2;
    pkt2.FIN = true;
    pkt2.seq_num = seq_num_share++;
    send_packet(sock, receiver_addr, pkt2);

    // �Ĵλ��ֵڶ���������ACK��
    Packet pkt_received1;
    receive_packet(sock, receiver_addr, pkt_received1);

    // �Ĵλ��ֵ�����������FIN-ACK��
    Packet pkt_received2;
    receive_packet(sock, receiver_addr, pkt_received2);

    // �Ĵλ��ֵ��Ĳ�������ACK��
    Packet pkt3;
    pkt3.ACK = true;
    pkt3.seq_num = seq_num_share++;
    pkt3.ack_num = pkt_received2.seq_num + 1;
    send_packet(sock, receiver_addr, pkt3);

    closesocket(sock);
    WSACleanup();

    return 0;
}