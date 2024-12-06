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

int seq_num_share = 61;         // �����Լ��������
int seq_num_expected = 37;      // Ԥ�ڶԷ��������
bool three_handshakes = false;  // �������ֱ�־λ
bool waving_four_times = false; // �Ĵλ��ֱ�־λ

#define SERVER_PORT 20000       // �������˿�
#define CLIENT_PORT 10000       // �ͻ��˶˿�
#define IPADDR "127.0.0.1"      // IP��ַ����Ϊ127.0.0.1
#define MAX_FILENAME_SIZE 32    // ����ļ����Ƴ���
#define MAX_DATA_SIZE 1024      // ��������ݳ���
#define WINDOW_SIZE 1           // �������ڴ�С

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

// �������ݰ������նˣ�
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

//�������ݰ������նˣ�
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

    // ���յ���������ݰ�
    if (pkt_received.seq_num > seq_num_expected) {
        cerr << "������ţ�" << seq_num_expected << "��������ţ�" << pkt_received.seq_num << "����Ų���ȷ���������ݰ�" << endl;
        return -1;
    }

    // �������ݰ�������
    cout << "�������ݰ���������ţ�" << pkt_received.seq_num << "��ȷ����ţ�" << pkt_received.ack_num
        << ", ���ݴ�С��" << pkt_received.data_len << ", У��ͣ�" << pkt_received.check_sum
        << "��ACK��" << pkt_received.ACK << "��SYN��" << pkt_received.SYN << "��FIN��" << pkt_received.FIN << endl;

    // ���յ���ȷ�����ݰ�
    if (pkt_received.seq_num == seq_num_expected) {
        // ����Ԥ�ڷ������
        seq_num_expected++;
        return 0;
    }

    // ���յ��ظ������ݰ�
    if (pkt_received.seq_num < seq_num_expected) {
        return 1;
    }
}

// �������ݰ�
int handle_packet(SOCKET& sock, struct sockaddr_in& sender_addr, queue<Packet>& receive_queue) {

    // �������ݰ�
    Packet pkt_received;
    int ret = receive_packet(sock, sender_addr, pkt_received);

    // ��������ж�
    if (ret == -1) { // ���յ���������ݰ�
        return 0;
    }
    if (ret == 1) { // ���յ��ظ������ݰ�
        // ����ACK��
        Packet pkt;
        pkt.ACK = true;
        pkt.seq_num = seq_num_share++;
        pkt.ack_num = pkt_received.seq_num + 1;
        send_packet(sock, sender_addr, pkt);
        return 0;
    }

    // ������������
    if (pkt_received.SYN) {
        three_handshakes = true;
        // �յ�SYN��������SYN-ACK��
        Packet pkt;
        pkt.SYN = true;
        pkt.ACK = true;
        pkt.seq_num = seq_num_share++;
        pkt.ack_num = pkt_received.seq_num + 1;
        send_packet(sock, sender_addr, pkt);
        return 0;
    }

    // �����������ֺʹ����Ĵλ���
    if (pkt_received.ACK && pkt_received.ack_num == seq_num_share) {
        if (three_handshakes) {
            three_handshakes = false;
            // ����Ԥ�ڷ������
            seq_num_expected = pkt_received.seq_num + 1;
            return 0;       // ������������ֵĵ�����ACK��ô��return 0
        }
        if (waving_four_times) {
            waving_four_times = false;
            return -1;      // ������Ĵλ��ֵĵ��Ĳ�ACK��ô��return -1
        }
        return 0;
    }

    // �����Ĵλ���
    if (pkt_received.FIN) {
        waving_four_times = true;
        // �յ�FIN��������ACK��
        Packet pkt1;
        pkt1.ACK = true;
        pkt1.seq_num = seq_num_share++;
        pkt1.ack_num = pkt_received.seq_num + 1;
        send_packet(sock, sender_addr, pkt1);
        // �յ�FIN��������FIN-ACK��
        Packet pkt2;
        pkt2.FIN = true;
        pkt2.ACK = true;
        pkt2.seq_num = seq_num_share++;
        pkt2.ack_num = pkt_received.seq_num + 1;
        send_packet(sock, sender_addr, pkt2);
        return 0;
    }

    // �����������ֺ��Ĵλ��֣��Ǿͽ����ݰ�������մ���
    if (receive_queue.size() < WINDOW_SIZE) {
        receive_queue.push(pkt_received);
    }

    // ���մ������˻����ļ�������ϣ�������д�뵽�ļ���
    if (receive_queue.size() == WINDOW_SIZE || pkt_received.data_len < MAX_DATA_SIZE) {
        while (!receive_queue.empty()) {
            // д�����ݵ��ļ���
            Packet tempPkt = receive_queue.front();
            receive_queue.pop();
            ofstream file("receive/" + string(tempPkt.name), ios::binary | ios::app);
            if (file.is_open()) {
                file.write(tempPkt.data, tempPkt.data_len);
                file.close();
                cout << "������д���ļ���·����receive/" + string(tempPkt.name) << endl;
            }
            else {
                cerr << "д���ļ�ʧ��" << endl;
            }
            // ����ACK��
            Packet pkt;
            pkt.ACK = true;
            pkt.seq_num = seq_num_share++;
            pkt.ack_num = tempPkt.seq_num + 1;
            send_packet(sock, sender_addr, pkt);
        }
    }
    return 0;
}

// ���ն�������
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

    // ������������ַ
    sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;  // ���õ�ַ��ΪIPv4
    server_addr.sin_port = htons(SERVER_PORT);  // ���ö˿ں�Ϊ30000
    server_addr.sin_addr.s_addr = inet_addr(IPADDR);  // ����IP��ַΪ127.0.0.1

    // �����ͻ��˵�ַ
    sockaddr_in client_addr;
    client_addr.sin_family = AF_INET;  // ���õ�ַ��ΪIPv4
    client_addr.sin_port = htons(CLIENT_PORT);  // ���ÿͻ��˶˿ں�Ϊ20000
    client_addr.sin_addr.s_addr = inet_addr(IPADDR);  // ���ÿͻ���IP��ַΪ127.0.0.1

    // ���׽���
    if (bind(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        cerr << "���׽���ʧ��" << endl;
        return -1;
    }

    // �������ն��У�ʵ�ֻ���������������
    queue<Packet> receive_queue;

    // ѭ������
    while (true) {
        int end = handle_packet(sock, client_addr, receive_queue);  // �������ݰ�������
        if (end == -1)
            break;
    }

    closesocket(sock);
    WSACleanup();

    return 0;
}