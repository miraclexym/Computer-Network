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

// ��log.txt�ļ���ʹ��ofstream����
ofstream logFile("log.txt", ios::out | ios::app);  // ios::out ���ļ�����д��ios::app ��ʾ׷������

int seq_num_share = 37;                     // �����Լ��������
int ack_num_expected = seq_num_share + 1;   // Ԥ�ڶԷ�ȷ�����
int timeout_ms = 1000;                      // ��ʱ�ش�ʱ��
int Congestion_Avoidance_Count = 0;         // ӵ���������
enum State { Slow_Start, Congestion_Avoidance, Quick_Recovery };
State Reno_State = Slow_Start;              // RENO�㷨��״̬��
int dupACKcount = 0;                        // �ظ�ACK�Ĵ�����¼
int ssthresh = 16;                          // ӵ�����Ƶ���������ֵ
int window_size = 1;                        // ���ô��ڴ�С
bool Congestion_Control = false;             // �Ƿ�ӵ������

#define SERVER_PORT 20000       // �������˿�
#define CLIENT_PORT 10000       // �ͻ��˶˿�
#define IPADDR "127.0.0.1"      // IP��ַ����Ϊ127.0.0.1
#define MAX_FILENAME_SIZE 32    // ����ļ����Ƴ���
#define MAX_DATA_SIZE 1024      // ��������ݳ���

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
        cout << "�������ݰ�ʧ��" << endl;
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
        cout << "�������ݰ�ʧ��" << endl;
        return 0;
    }

    // ��ȡ���ݰ�����
    memcpy(&pkt_received, buffer, sizeof(pkt_received));

    // ����У���
    unsigned short calculated_checksum = calculate_checksum(pkt_received.data);

    // ���У���
    if (pkt_received.check_sum != calculated_checksum) {
        cout << "����У��ͣ�" << pkt_received.check_sum << "������У��ͣ�" << calculated_checksum << "��У��ʹ��󣬶������ݰ�" << endl;
        return 0;
    }

    // �������ݰ�������
    cout << "�������ݰ���������ţ�" << pkt_received.seq_num << "��ȷ����ţ�" << pkt_received.ack_num
        << ", ���ݴ�С��" << pkt_received.data_len << ", У��ͣ�" << pkt_received.check_sum
        << "��ACK��" << pkt_received.ACK << "��SYN��" << pkt_received.SYN << "��FIN��" << pkt_received.FIN << endl;

    // ���ȷ����ţ��ͻ��˷������ݰ�������������ȷ����ţ�
    if (pkt_received.ACK && !pkt_received.FIN && !pkt_received.SYN) { // ����������ȷ�����
        if (pkt_received.ack_num == ack_num_expected) { // ���ǡ����Ԥ�ڵ�ACK
            // ����Ԥ��ȷ�����
            ack_num_expected++; // ���յ�һ��ACK���������� (pkt_received.ack_num + 1 - ack_num_expected)
            return 1; // ���յ�һ��ACK���������� (pkt_received.ack_num + 1 - ack_num_expected)
        }
        else if (pkt_received.ack_num > ack_num_expected) { // �ۻ�ȷ��ʵ��
            // ����Ԥ��ȷ�����
            ack_num_expected = pkt_received.ack_num + 1; // ���յ�һ��ACK���������� (pkt_received.ack_num + 1 - ack_num_expected)
            return (pkt_received.ack_num + 1 - ack_num_expected); // ���յ�һ��ACK���������� (pkt_received.ack_num + 1 - ack_num_expected)
        }
        else if (pkt_received.ack_num < ack_num_expected) { //�����ش� // ����Ԥ�ڣ�ACK3�����Ƿ���ACK2��ACK2��ACK2
            return -1;
        }
    }
    // ���ճ�ʱ
    return 0;
}

// �����ļ�
double send_file(SOCKET& sock, struct sockaddr_in& receiver_addr, string filename) {
    // �������Ͷ��У�ʵ�ֻ���������������
    queue<Packet> send_queue;

    // ��ʱ�ش�����
    int timeout = 0;

    // �����ش�����
    int quickcount = 0;

    // ��¼�ļ��Ŀ�ʼʱ��
    auto start_time = chrono::high_resolution_clock::now();

    // ���ļ�
    ifstream file("send/" + filename, ios::binary);
    if (!file.is_open()) {
        cout << "���ļ�ʧ��" << endl;
        return -1;
    }

    // ��ȡ�ļ���С���Լ���������
    file.seekg(0, ios::end); // �ƶ����ļ�ĩβ
    long file_size = file.tellg(); // ��ȡ�ļ���С
    file.seekg(0, ios::beg); // ���ļ�ָ�����õ��ļ���ͷ

    // ���ý��ճ�ʱʱ�䣬��λΪ����
    int timeout_ms = 100;  // ���볬ʱ
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout_ms, sizeof(timeout_ms));

    while (!file.eof() || !send_queue.empty()) { // ��ȡ�ļ����������ڷ������ݰ�������ȷ�����ݰ�

        // ���ʹ���������ݰ�
        while (send_queue.size() < window_size && !file.eof()) {
            Packet pkt;
            strncpy_s(pkt.name, sizeof(pkt.name), filename.c_str(), sizeof(pkt.name) - 1);  // �����ļ�����
            pkt.name[sizeof(pkt.name) - 1] = '\0';  // �ļ�����
            file.read(pkt.data, sizeof(pkt.data));  // ��ȡ����
            int read_len = file.gcount();           // ��ȡ����
            pkt.data_len = read_len;                // ���ݳ���
            pkt.seq_num = seq_num_share++;          // �������
            send_queue.push(pkt);                   // ��Ӷ���
            cout << "���ʹ������������ݰ�" << endl;
            int able_num = window_size - send_queue.size() > 0 ? window_size - send_queue.size() : 0;
            cout << "��ǰ���ʹ��ڴ�С��" << window_size << "�����ʹ�����ֵ��" << ssthresh
                << "�����÷���������" << send_queue.size() << "�����÷���������" << able_num << endl;
        }

        // ����Ԥ��ȷ�����
        ack_num_expected = send_queue.front().seq_num + 1;

        // ���ʹ��ڷ������ݰ�
        queue<Packet> tempQueue = send_queue;  // ���ƶ��У�����ı�ԭ��������
        for (int i = 0; i < window_size && !tempQueue.empty(); i++) {
            send_packet(sock, receiver_addr, tempQueue.front());  // ���Ͷ�ͷ���ݰ�
            tempQueue.pop();  // ɾ����ͷ���ݰ�������Ӱ��ԭ���У�
        }

        // ���ʹ��ڽ������ݰ�
        int receive_num = send_queue.size();
        // ������շ����������ݰ���ʱ��1ms���������°ѷ��ʹ�������ʣ�µ����ݰ�����һ��
        for (int i = 0; i < receive_num; i++) {
            Packet pkt_received;
            int result = receive_packet(sock, receiver_addr, pkt_received);
            if (result > 0) { // �ɹ�����
                for (int i = 0; i < result; i++) {
                    send_queue.pop();
                    if (Reno_State == Slow_Start && Congestion_Control) { // �������׶�
                        window_size++; // ���ô��ڴ�С
                        dupACKcount = 0; // �����ظ�ACK����
                        if (window_size > ssthresh) {
                            Reno_State = Congestion_Avoidance; // ӵ������׶�
                            Congestion_Avoidance_Count = 0; // ���ӵ������׶����Ӵ��ڴ�С�ļ�����
                            cout << "���Ͷ˽���ӵ������״̬" << endl;
                        }
                    }
                    else if (Reno_State == Congestion_Avoidance && Congestion_Control) { // ӵ������׶�
                        Congestion_Avoidance_Count++;
                        cout << "��ǰӵ������׶ι������Ӵ��ڴ�С�Ľ���Ϊ��" << Congestion_Avoidance_Count << "/" << window_size << endl;
                        if (Congestion_Avoidance_Count >= window_size) {
                            Congestion_Avoidance_Count = 0; // ���ӵ������׶����Ӵ��ڴ�С�ļ�����
                            window_size++; // ���ô��ڴ�С
                        }
                        dupACKcount = 0; // �����ظ�ACK����
                    }
                    else if(Congestion_Control) { // Reno_State == Quick_Recovery // ���ٻָ��׶�
                        Reno_State = Congestion_Avoidance; // ӵ������׶�
                        cout << "���Ͷ˽���ӵ������״̬" << endl;
                        window_size = ssthresh; // ���ô��ڴ�С
                        dupACKcount = 0; // �����ظ�ACK����
                    }
                    cout << "���մ���ȷ�������ݰ�" << endl;
                    int able_num = window_size - send_queue.size() > 0 ? window_size - send_queue.size() : 0;
                    cout << "��ǰ���ʹ��ڴ�С��" << window_size << "�����ʹ�����ֵ��" << ssthresh
                        << "�����÷���������" << send_queue.size() << "�����÷���������" << able_num << endl;
                }
            }
            else if (result == -1) { // �����ش�
                if (Reno_State == Quick_Recovery && Congestion_Control) {
                    window_size++; // ���ô��ڴ�С
                }
                else if(Congestion_Control) {
                    dupACKcount++;
                    if (dupACKcount == 3) {
                        quickcount++;
                        Reno_State = Quick_Recovery; // ���ٻָ��׶�
                        cout << "���Ͷ˽�����ٻָ�״̬" << endl;
                        ssthresh = window_size / 2 > 0 ? window_size / 2 : 1; // ������ֵ
                        window_size = ssthresh + 3; // ���ô��ڴ�С
                        dupACKcount = 0; // �����ظ�ACK����
                        int able_num = window_size - send_queue.size() > 0 ? window_size - send_queue.size() : 0;
                        cout << "��ǰ���ʹ��ڴ�С��" << window_size << "�����ʹ�����ֵ��" << ssthresh
                            << "�����÷���������" << send_queue.size() << "�����÷���������" << able_num << endl;
                        break; // ��һ�� while ѭ�������·��ʹ��������ݰ�
                    }
                }
            }
            else if(Congestion_Control) { // ��ʱ����������ճ�ʱ�������������׶�
                timeout++; // ��¼��ʱ����
                cout << "ACK��ʱ�����·��ʹ��������ݰ�����ǰ�ۻ��ش�������" << timeout << endl;
                Reno_State = Slow_Start; // �������׶�
                cout << "���Ͷ˽���������״̬" << endl;
                ssthresh = window_size / 2 > 0 ? window_size / 2 : 1; // ������ֵ
                window_size = 1; // ���ô��ڴ�С
                dupACKcount = 0; // �����ظ�ACK����
                int able_num = window_size - send_queue.size() > 0 ? window_size - send_queue.size() : 0;
                cout << "��ǰ���ʹ��ڴ�С��" << window_size << "�����ʹ�����ֵ��" << ssthresh
                    << "�����÷���������" << send_queue.size() << "�����÷���������" << able_num << endl;
                break; // ��һ�� while ѭ�������·��ʹ��������ݰ�
            }
        }
    }

    // ��¼�ļ��������ʱ��
    auto end_time = chrono::high_resolution_clock::now();
    chrono::duration<double> duration = end_time - start_time;

    // �����ʱ�ش�����
    cout << "��ʱ�ش�����Ϊ��" << timeout << " ��" << endl;

    // ��������ش�����
    cout << "�����ش�����Ϊ��" << quickcount << " ��" << endl;

    // ���������ʣ��ļ���С / ����ʱ�䣩
    double throughput = (double)file_size / duration.count(); // �����ʣ���λ�ֽ�/��
    cout << "�ļ�����ʱ��: " << duration.count() << " ��" << endl;
    cout << "������: " << throughput / 1024 << " KB/s" << endl; // �����ʵ�λ��KB/s

    file.close();

    return duration.count();
}

// ���Ͷ�������

#pragma comment(lib,"ws2_32.lib")
int main() {
    // ��ʼ��WinSock
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        cout << "WinSock ��ʼ��ʧ��" << endl;
        return -1;
    }

    // ����UDP�׽���
    SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET) {
        cout << "�����׽���ʧ��" << endl;
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
        cout << "���׽���ʧ��" << endl;
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
    string filename = "1.jpg";

    // ���ڴ�С����
    int windows[11] = { 1,10,20,30,40,50,60,70,80,90,100 };

    // ����ʱ���¼
    double times[11]{ 0,0,0,0,0,0,0,0,0,0,0 };
    
    // ѭ�������ļ�
    for (int i = 0; i < 11; i++) {
        // ������ȡ�û�������ļ���
        //cout << "������Ҫ���͵��ļ���������exit�˳���: ";
        //getline(cin, filename);
        // ���ô��ڴ�С
        window_size = windows[i];
        if (filename == "exit")
            break;  // �û�����exitʱ�˳�
        else {
            // ��÷����ļ���
            times[i] = send_file(sock, receiver_addr, filename);  // ����send_file�����ļ�
        }
    }

    // ����������ڴ�С������ʱ���������
    std::cout << "�������ڴ�С�������ļ�ʱ����������:\n";
    std::cout << std::setw(15) << "���ڴ�С" << std::setw(20) << "����ʱ�� (��)" << std::setw(20) << "������ (�ֽ�/��)" << std::endl;
    for (int i = 0; i < 11; i++) {
        // ���������ʣ������� = 1814.0 / ����ʱ��
        double throughput = 1814.0 / times[i];

        // ������ڴ�С������ʱ���������
        std::cout << std::setw(15) << windows[i]
            << std::setw(20) << times[i]
            << std::setw(20) << throughput << std::endl;
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