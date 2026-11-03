#ifndef NETDISK_PROTOCOL_H
#define NETDISK_PROTOCOL_H

#include <stddef.h>
#include <sys/socket.h>

/* 客户端与服务端共用的请求编号；新增或调整编号时两端保持一致。 */
#define TYPE_REGISTER 1
#define TYPE_LOGIN 2
#define TYPE_LIST 3
#define TYPE_GET 4
#define TYPE_PUT 5
#define TYPE_HISTORY 6
#define TYPE_DELETE_ACCOUNT 7
#define TYPE_EXIT 8
#define TYPE_CREATE_REPO 9
#define TYPE_LIST_REPO 10
#define TYPE_COMMIT 11

#define NETDISK_NAME_SIZE 256
#define NETDISK_INFO_SIZE 500
#define NETDISK_MESSAGE_SIZE 256

/* 固定长度的通信数据包。info/info_len 也用于分块传输文件内容。 */
struct net_info
{
    int type;
    char username[50];
    char password[50];
    char filename[NETDISK_NAME_SIZE];
    char info[NETDISK_INFO_SIZE];
    int info_len;
    int repository_id;
    int base_version;
    int file_count;
    char commit_message[NETDISK_MESSAGE_SIZE];
};

/* TCP 是字节流，循环收发直到指定长度的数据全部处理完。 */
static inline int send_all(int sockfd, const void *buffer, size_t length)
{
    const char *data = (const char *)buffer;
    size_t sent = 0;

    while (sent < length)
    {
        ssize_t ret = send(sockfd, data + sent, length - sent, 0);
        if (ret <= 0)
        {
            return -1;
        }
        sent += (size_t)ret;
    }
    return 0;
}

static inline int recv_all(int sockfd, void *buffer, size_t length)
{
    char *data = (char *)buffer;
    size_t received = 0;

    while (received < length)
    {
        ssize_t ret = recv(sockfd, data + received, length - received, 0);
        if (ret <= 0)
        {
            return -1;
        }
        received += (size_t)ret;
    }
    return 0;
}

/* 数据包接口：当前协议直接传输整个 struct net_info。 */
static inline int send_packet(int sockfd, const struct net_info *packet)
{
    return send_all(sockfd, packet, sizeof(*packet));
}

static inline int recv_packet(int sockfd, struct net_info *packet)
{
    return recv_all(sockfd, packet, sizeof(*packet));
}

#endif