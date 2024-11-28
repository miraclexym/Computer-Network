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

int send_file(SOCKET& sock, struct sockaddr_in& receiver_addr, string filename) {

    // ��ʱ�ش�����
    int timeout = 0;

    // ��¼�ļ��Ŀ�ʼʱ��
    auto start_time = chrono::high_resolution_clock::now();

    // ���ݴ���
    ifstream file("send/" + filename, ios::binary);
    if (!file.is_open()) {
        cerr << "���ļ�ʧ��" << endl;
        return -1;
    }

    // ��ȡ�ļ���С
    file.seekg(0, ios::end); // �ƶ����ļ�ĩβ
    long file_size = file.tellg(); // ��ȡ�ļ���С
    file.seekg(0, ios::beg); // ���ļ�ָ�����õ��ļ���ͷ

    while (!file.eof()) {
        Packet pkt;
        // �����ļ���
        strncpy_s(pkt.name, sizeof(pkt.name), filename.c_str(), sizeof(pkt.name) - 1);
        pkt.name[sizeof(pkt.name) - 1] = '\0'; // ȷ���ļ�����'\0'��β

        // ��ȡ���ݵ����ݰ���
        file.read(pkt.data, sizeof(pkt.data));
        int read_len = file.gcount();
        pkt.data_len = read_len;
        pkt.seq_num = seq_num_share++;

        // �������ݰ�
        send_packet(sock, receiver_addr, pkt);

        // �ȴ�ACK������ʱ�ش�
        struct sockaddr_in sender_addr;
        int sender_len = sizeof(sender_addr);
        char buffer[MAX_PACKET_SIZE];
        int ack_len = recvfrom(sock, buffer, sizeof(buffer), 0, (struct sockaddr*)&sender_addr, &sender_len);

        if (ack_len == SOCKET_ERROR) {
            cerr << "����ACK��ʱ�����·������ݰ���" << pkt.seq_num << endl;
            timeout++;
            seq_num_share--;
            continue;  // ��ʱ�ش�
        }

        Packet pkt_received;
        memcpy(&pkt_received, buffer, sizeof(pkt_received));

        if (pkt_received.ACK && pkt_received.data_len == 0) {
            cout << "�������ݰ���������ţ�" << pkt_received.seq_num << "��ȷ����ţ�" << pkt_received.ack_num
                << ", ���ݴ�С��" << pkt_received.data_len << ", У��ͣ�" << pkt_received.check_sum
                << "��ACK��" << pkt_received.ACK << "��SYN��" << pkt_received.SYN << "��FIN��" << pkt_received.FIN << endl;
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

    // �����Է���ַ
    sockaddr_in receiver_addr;
    receiver_addr.sin_family = AF_INET;  // ���õ�ַ��ΪIPv4
    receiver_addr.sin_port = htons(PORT);  // ���ö˿ں�
    inet_pton(AF_INET, "127.0.0.1", &receiver_addr.sin_addr.s_addr);  // ����IP��ַ

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