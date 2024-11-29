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

// ���ݰ��ṹ
struct Packet {
    int seq_num = 0;                    // �������
    int ack_num = 0;                    // ȷ�����
    char name[MAX_FILENAME_SIZE];       // �ļ�����
    char data[MAX_DATA_SIZE];           // ��������
    int data_len = 0;                   // ���ݳ���
    int check_sum = 0;                  // У���
    bool ACK = false;                   // ��־λ
    bool SYN = false;                   // ��־λ
    bool FIN = false;                   // ��־λ
};

#define MAX_PACKET_SIZE (sizeof(Packet))

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

// �������ݰ�
void send_packet(SOCKET& sock, struct sockaddr_in& receiver_addr, Packet& pkt) {
    // ����У���
    pkt.check_sum = calculate_checksum(pkt.data);

    // �������ݰ�
    int sent_len = sendto(sock, (char*)&pkt, sizeof(pkt), 0, (struct sockaddr*)&receiver_addr, sizeof(receiver_addr));

    if (sent_len == SOCKET_ERROR) {
        cerr << "�������ݰ�ʧ��" << endl;
    }
    else {
        cout << "�������ݰ���������ţ�" << pkt.seq_num << "��ȷ����ţ�" << pkt.ack_num
            << ", ���ݴ�С��" << pkt.data_len << ", У��ͣ�" << pkt.check_sum
            << "��ACK��" << pkt.ACK << "��SYN��" << pkt.SYN << "��FIN��" << pkt.FIN << endl;
    }
}

//�������ݰ�
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

    // У��ͳ���
    if (pkt_received.check_sum != calculated_checksum) {
        cerr << "����У��ͣ�" << pkt_received.check_sum << "������У��ͣ�" << calculated_checksum << "��У��ʹ��󣬶������ݰ�";
        return -1;
    }

    cout << "�������ݰ���������ţ�" << pkt_received.seq_num << "��ȷ����ţ�" << pkt_received.ack_num
        << ", ���ݴ�С��" << pkt_received.data_len << ", У��ͣ�" << pkt_received.check_sum
        << "��ACK��" << pkt_received.ACK << "��SYN��" << pkt_received.SYN << "��FIN��" << pkt_received.FIN << endl;

    return 0;
}

// �������ݰ�
int handle_packet(SOCKET& sock, struct sockaddr_in& sender_addr) {

    // �������ݰ�
    Packet pkt_received;
    receive_packet(sock, sender_addr, pkt_received);

    // ������������
    if (pkt_received.SYN) {
        my_connect = true;
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

    // �����Ĵλ���
    if (pkt_received.FIN) {
        my_disconnect = true;
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

    // ������д���ļ�
    ofstream file("receive/" + string(pkt_received.name), ios::binary | ios::app);
    if (file.is_open()) {
        file.write(pkt_received.data, pkt_received.data_len);
        file.close();
        cout << "������д���ļ���·����receive/" + string(pkt_received.name) << endl;
    }
    else {
        cerr << "д���ļ�ʧ��" << endl;
    }

    // ����ACK��
    Packet pkt;
    pkt.ACK = true;
    pkt.seq_num = seq_num_share++;
    pkt.ack_num = pkt_received.seq_num + 1;
    send_packet(sock, sender_addr, pkt);
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

    // �����Է���ַ
    sockaddr_in sender_addr;
    sender_addr.sin_family = AF_INET;  // ���õ�ַ��ΪIPv4
    sender_addr.sin_port = htons(PORT);  // ���ö˿ں�
    sender_addr.sin_addr.s_addr = inet_addr("127.0.0.1");  // ����IP��ַ

    // ���׽���
    if (bind(sock, (struct sockaddr*)&sender_addr, sizeof(sender_addr)) == SOCKET_ERROR) {
        cerr << "���׽���ʧ��" << endl;
        return -1;
    }

    // ѭ������
    while (true) {
        int end = handle_packet(sock, sender_addr);  // �������ݰ�������
        if (end == -1)
            break;
    }

    closesocket(sock);
    WSACleanup();

    return 0;
}