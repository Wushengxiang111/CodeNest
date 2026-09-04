# NETDISK

## 1. 项目简介

NETDISK 是一个基于 **Linux + C 语言**开发的简易网络云盘系统。

项目采用 **客户端 + 服务器**架构，通过 **TCP Socket** 实现客户端与服务器之间的数据通信，服务器使用 **pthread 多线程**处理多个客户端连接，并使用 **SQLite3** 保存用户信息和历史操作记录。

项目主要实现用户注册、登录、文件列表查看、文件上传、文件下载以及历史操作记录查询等功能。

---

## 2. 项目功能

### 用户功能

* 用户注册
* 用户登录
* 用户退出

### 文件功能

* 查看当前用户云盘中的文件
* 下载服务器中的文件
* 上传本地文件到服务器

### 历史记录

* 查看当前用户的历史操作记录
* 记录文件列表查看操作
* 记录文件下载操作
* 记录文件上传操作

---

## 3. 项目架构

项目采用客户端/服务器（C/S）架构。

```text
                    NETDISK
                       │
          ┌────────────┴────────────┐
          │                         │
       Client                    Server
          │                         │
          │       TCP Socket        │
          └────────────────────────→│
                                    │
                              pthread 多线程
                                    │
                       ┌────────────┴────────────┐
                       │                         │
                    SQLite3                  文件系统
                       │                         │
                  用户/历史记录             用户文件
```

---

## 4. 项目目录

```text
NETDISK/
├── client/
│   └── client.c
│
├── server/
│   └── server.c
│
└── .gitignore
```

---

## 5. 使用的主要技术

* C 语言
* Linux
* TCP Socket
* pthread 多线程
* SQLite3
* 文件 I/O
* Linux 文件系统操作
* SQL
* Git / GitHub

---

## 6. 通信协议

客户端和服务器使用统一的 `net_info` 结构体进行数据通信。

```c
struct net_info
{
    int type;
    char username[50];
    char password[50];
    char filename[50];
    char info[500];
    int info_len;
};
```

### type 类型

| type | 功能     |
| ---: | ------ |
|    1 | 注册     |
|    2 | 登录     |
|    3 | 查看文件列表 |
|    4 | 下载文件   |
|    5 | 上传文件   |
|    6 | 查看历史记录 |
|    7 | 退出     |

---

## 7. 服务器设计

服务器主要负责：

1. 创建 TCP Socket
2. 绑定 `8888` 端口
3. 开启监听
4. 接收客户端连接
5. 为客户端创建线程
6. 根据 `type` 判断客户端请求
7. 执行对应操作
8. 返回处理结果

服务器基本流程：

```text
socket()
   ↓
bind()
   ↓
listen()
   ↓
accept()
   ↓
pthread_create()
   ↓
recv()
   ↓
判断 type
   ↓
执行对应功能
```

---

## 8. 多线程设计

服务器使用 `pthread` 实现并发处理。

每当有新的客户端连接时，服务器调用：

```c
pthread_create()
```

创建新的线程处理客户端。

```text
客户端1 ──→ 线程1
客户端2 ──→ 线程2
客户端3 ──→ 线程3
客户端4 ──→ 线程4
```

这样可以同时处理多个客户端连接。

---

## 9. 用户注册

客户端输入：

```text
账号
密码
```

服务器首先查询 `user_info` 表，判断用户名是否已经存在。

如果不存在：

1. 将用户名和密码保存到数据库
2. 创建该用户对应的文件夹
3. 返回“注册成功”

如果用户名已经存在：

```text
注册失败
```

---

## 10. 用户登录

服务器根据用户名和密码查询数据库：

```sql
select * from user_info
where username='xxx'
and password='xxx';
```

查询成功：

```text
登录成功
```

客户端保存当前登录用户名，并设置登录状态。

登录后才能执行：

* list
* get
* put
* history

---

## 11. 文件列表

登录后执行 `list`：

```text
客户端
   ↓
发送 username
   ↓
服务器打开 username 文件夹
   ↓
opendir()
   ↓
readdir()
   ↓
读取文件名
   ↓
send()
   ↓
客户端显示文件名
```

服务器通过：

```c
opendir()
readdir()
```

读取当前用户文件夹中的文件。

隐藏文件不会显示。

---

## 12. 文件下载

执行 `get` 后：

```text
客户端输入文件名
        ↓
发送用户名 + 文件名
        ↓
服务器打开用户目录下的文件
        ↓
fread()
        ↓
分块读取文件
        ↓
send()
        ↓
客户端 recv()
        ↓
fwrite()
        ↓
保存到本地
```

服务器每次读取最多 `500` 字节：

```c
fread(s.info, 1, sizeof(s.info), fp);
```

然后通过 TCP 发送给客户端。

客户端接收到数据后追加写入本地文件。

---

## 13. 文件上传

客户端输入需要上传的文件名。

上传过程：

```text
客户端
   ↓
打开本地文件
   ↓
fread()
   ↓
分块读取
   ↓
send()
   ↓
服务器 recv()
   ↓
fwrite()
   ↓
保存到用户文件夹
```

第一次发送：

```c
s.info_len = -1;
```

服务器收到后创建/清空对应文件。

之后客户端不断读取文件内容并发送：

```c
s.info_len = fread(...);
```

服务器使用：

```c
fwrite()
```

追加写入文件。

---

## 14. 历史记录

服务器使用 SQLite3 保存用户操作记录。

历史记录包括：

```text
用户名
操作时间
操作类型
```

例如：

```text
zhangsan 2026-09-04 20:10:25 list
zhangsan 2026-09-04 20:12:31 get
zhangsan 2026-09-04 20:15:42 put
```

服务器通过：

```c
insert_history()
```

记录用户操作。

目前记录的操作包括：

* list
* get
* put

---

## 15. 数据库

项目使用 SQLite3。

数据库文件：

```text
netdisk.db
```

主要使用：

```c
sqlite3_open()
sqlite3_exec()
sqlite3_get_table()
```

### 用户信息

`user_info`

```text
username
password
```

### 历史记录

`history_info`

```text
username
time
action
```

---

## 16. 编译

### Server

进入项目目录：

```bash
cd ~/NETDISK
```

编译服务器：

```bash
gcc server/server.c -o server/server -lpthread -lsqlite3
```

### Client

```bash
gcc client/client.c -o client/client -lpthread
```

---

## 17. 运行

### 启动服务器

```bash
./server/server
```

服务器监听：

```text
8888
```

### 启动客户端

打开另一个终端：

```bash
./client/client 127.0.0.1 8888
```

---

## 18. 客户端操作菜单

程序启动后显示：

```text
1.register
2.login
3.list
4.get
5.put
6.history
7.exit
```

对应功能：

```text
1 → 用户注册
2 → 用户登录
3 → 查看文件列表
4 → 下载文件
5 → 上传文件
6 → 查看历史记录
7 → 退出程序
```

---

## 19. 项目涉及的核心知识

### Linux

* Linux 基本命令
* 文件系统
* 进程
* `fork()`
* `execlp()`
* `wait()`

### 网络编程

* Socket
* TCP
* `socket()`
* `bind()`
* `listen()`
* `accept()`
* `connect()`
* `send()`
* `recv()`

### 多线程

* `pthread_create()`
* 线程并发
* 客户端/服务器多线程通信

### 文件操作

* `fopen()`
* `fread()`
* `fwrite()`
* `fclose()`
* `opendir()`
* `readdir()`
* `closedir()`

### 数据库

* SQLite3
* SQL
* `sqlite3_open()`
* `sqlite3_exec()`
* `sqlite3_get_table()`

---

## 20. 项目运行流程

```text
                    启动 Server
                         │
                    socket()
                         │
                      bind()
                         │
                     listen()
                         │
                     accept()
                         │
                  创建客户端线程
                         │
                     recv()
                         │
              ┌──────────┼──────────┐
              │          │          │
            注册        登录       文件操作
              │          │          │
           SQLite3    SQLite3    文件系统
              │          │          │
              └──────────┴──────────┘
                         │
                       send()
                         │
                    Client 接收
                         │
                       显示结果
```

---

## 21. 项目总结

通过本项目学习和实践了 Linux 环境下的 C 网络编程。

项目将以下知识进行了综合应用：

```text
C语言
  ↓
Linux
  ↓
TCP Socket
  ↓
pthread 多线程
  ↓
文件 I/O
  ↓
SQLite3
  ↓
客户端/服务器架构
```

通过 NETDISK 项目实现了一个基础的网络云盘系统，并加深了对 **TCP 网络通信、多线程并发、Linux 文件操作以及 SQLite 数据库操作**的理解。

