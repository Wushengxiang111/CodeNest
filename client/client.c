#include <sys/types.h>         
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include "../common/protocol.h"

#define TYPE_LIST_VERSIONS 12
#define TYPE_DOWNLOAD_VERSION 13

int islogin=0;//登录状态
char loginUsername[50];//登录成功的用户名
volatile int version_selection_in_progress=0;
int version_download_mode=0;
int version_repository_id=0;

/* 后台接收服务端响应，并按响应 type 显示结果或写入下载数据。 */
void* func(void* p)
{
    int sockfd = *(int *)p;
    while(1)
    {
        struct net_info s;
        int ret = recv(sockfd,&s,sizeof(s),0);//接收服务器返回的数据
        if(ret <= 0)
        {
            printf("链接断开\n");
            break;
        }
		if(s.type==1)
		{
			printf("%s\n",s.info);
		}
		if(s.type==2)
		{
			printf("%s\n",s.info);
			if(strcmp("登录成功",s.info)==0)
			{
				islogin=1;//已登录
				strcpy(loginUsername,s.username);
			}
			else
			{
				islogin=0;
			}
		}
		if(s.type==3)
		{
			printf("%s\n",s.filename);//将收到的文件名打印出来
		}
		if(s.type==4)
		{
			FILE* fp=fopen(s.filename,"a");
			if(fp==NULL)
			{
				printf("打开失败\n");
			}
			else
			{
				fwrite(s.info,1,s.info_len,fp);
				fclose(fp);
			}
		}
		if(s.type==TYPE_COMMIT)
		{
			printf("%s\n",s.info);
		}
		if(s.type==TYPE_LIST_VERSIONS)
		{
			if(s.repository_id==-1)
			{
				/* 下载流程先列出版本；收到结束标记后再请求用户选定的版本。 */
				if(version_download_mode)
				{
					int version;
					printf("请输入版本号：\n");
					scanf("%d",&version);
					struct net_info request = {0};
					request.type = TYPE_DOWNLOAD_VERSION;
					request.repository_id = version_repository_id;
					request.base_version = version;
					strcpy(request.username,loginUsername);
					send_packet(sockfd,&request);
					version_selection_in_progress=0;
					version_download_mode=0;
				}
			}
			else
			{
				printf("版本 %d\n提交说明：%s\n时间：%s\n",
					s.base_version,s.commit_message,s.info);
			}
		}
		if(s.type==TYPE_DOWNLOAD_VERSION)
		{
			if(strcmp(s.info,"开始下载")==0)
			{
				FILE* fp=fopen(s.filename,"w");
				if(fp!=NULL) fclose(fp);
				printf("开始下载版本 %d：%s\n",s.base_version,s.filename);
			}
			else
			{
				printf("%s\n",s.info);
			}
		}
		if(s.type==6)//history
		{
			printf("%s\n",s.info);
		}
		if(s.type==7)//注销账号
		{
			printf("%s\n",s.info);
			if(strcmp("账号注销成功",s.info)==0)
			{
				islogin=0;
				loginUsername[0]='\0';
			}
		}
		if(s.type==9)//创建代码仓库
		{
			printf("%s\n",s.info);
		}
		if(s.type==10)//查看我的代码仓库
		{
			printf("仓库ID：%d 仓库名称：%s %s\n",s.info_len,s.filename,s.info);
		}
    }
}

// 菜单选项只负责分组展示，返回值仍是原有的协议 type。
static int select_operation(void)
{
	int choice;

	if(!islogin)
	{
		printf("\n=== NETDISK ===\n");
		printf("1. 注册账号\n2. 登录\n0. 退出程序\n");
		printf("请选择：\n");
		scanf("%d", &choice);
		switch(choice)
		{
			case 1: return TYPE_REGISTER;
			case 2: return TYPE_LOGIN;
			case 0: return TYPE_EXIT;
			default: printf("无效选项\n"); return 0;
		}
	}

	printf("\n=== 主菜单 ===\n");
	printf("1. 网盘文件\n2. 代码仓库\n3. 操作历史\n4. 注销账号（删除账号文件）\n0. 退出程序\n");
	printf("请选择：\n");
	scanf("%d", &choice);
	switch(choice)
	{
		case 1:
			printf("\n=== 网盘文件 ===\n1. 文件列表\n2. 上传文件\n3. 下载文件\n0. 返回\n");
			scanf("%d", &choice);
			switch(choice)
			{
				case 0: return 0;
				case 1: return TYPE_LIST;
				case 2: return TYPE_PUT;
				case 3: return TYPE_GET;
				default: printf("无效选项\n"); return 0;
			}
		case 2:
			printf("\n=== 代码仓库 ===\n1. 我的仓库\n2. 创建仓库\n3. 提交代码\n4. 查看版本\n5. 下载版本\n0. 返回\n");
			scanf("%d", &choice);
			switch(choice)
			{
				case 0: return 0;
				case 1: return TYPE_LIST_REPO;
				case 2: return TYPE_CREATE_REPO;
				case 3: return TYPE_COMMIT;
				case 4: return TYPE_LIST_VERSIONS;
				case 5: return TYPE_DOWNLOAD_VERSION;
				default: printf("无效选项\n"); return 0;
			}
		case 3: return TYPE_HISTORY;
		case 4: return TYPE_DELETE_ACCOUNT;
		case 0: return TYPE_EXIT;
		default: printf("无效选项\n"); return 0;
	}
}

int main(int argc, char* argv[])
{
	(void)argc;
    //./send 127.0.0.1 8888
	int sockfd = socket(AF_INET, SOCK_STREAM, 0);
	printf("%d\n", sockfd);
    
    //准被对方的ip地址和端口号
    struct sockaddr_in youraddr;
	youraddr.sin_family = AF_INET;
	youraddr.sin_port = htons(atoi(argv[2]));//对方的端口号
	youraddr.sin_addr.s_addr = inet_addr(argv[1]);//对方的ip地址
    
    //链接
    int ret = connect(sockfd, (struct sockaddr*)&youraddr, sizeof(youraddr));
    printf("connect %d\n", ret);
    
    pthread_t id;
    pthread_create(&id, NULL, func, &sockfd);
    
    //发送
    while(1)
    {
		struct net_info s = {0};
		while(version_selection_in_progress) usleep(10000);
		s.type = select_operation();
		if(s.type == 0)
		{
			/* 子菜单的“返回”只回到主循环，不发送网络请求。 */
			continue;
		}
		if(s.type==1)//注册
		{
			printf("请您输入账号和密码:\n");
			scanf("%s%s",s.username,s.password);//数据存到结构体
		}
		else if(s.type==2)//登录
		{
			printf("请您输入账号和密码:\n");
			scanf("%s%s",s.username,s.password);
		}
		else if(s.type==3)//查看文件夹
		{
			if(islogin==0)
			{
				printf("请登录\n");
				continue;//跳过发送结构体的步骤
			}
			else//为了预防登录失败的情况，结构体会保存登录失败的账户名
			{
				strcpy(s.username,loginUsername);//将登陆的用户名放到结构体中
			}
		}
		else if(s.type==4)//get将服务器上用户文件夹下的文件下载到客户端
		{
			if(islogin==0)
			{
				printf("请登录\n");
				continue;//跳过发送结构体的步骤
			}
			else
			{
				printf("请输入您要下载的文件名:\n");
				scanf("%s",s.filename);
				FILE* fp1=fopen(s.filename,"r");//判断文件是否存在
				if(fp1==NULL)//文件不存在
				{
					;//
				}
				else//文件存在
				{
					int download;
					printf("文件存在，是否要继续下载,0不下载，1下载\n");
					scanf("%d",&download);
					if(download==0)//不继续下载
					{
						strcpy(s.username,loginUsername);
						FILE* fp=fopen(s.filename,"w");//将文件清空
						fclose(fp);
					}
					else if(download==1)//继续下载
					{
						fclose(fp1);//关闭只读
						strcpy(s.username,loginUsername);
						FILE* fp=fopen(s.filename,"r");//读取文件
						s.info_len=0;
						fseek(fp,0,SEEK_END);
						s.info_len=ftell(fp);//求文件大小
						fclose(fp);
					}
				}
			}
		}
		else if(s.type==5)//put上传文件
		{
			if(islogin==0)
			{
				printf("请登录\n");
				continue;
			}
			else
			{
				printf("请输入您要上传的文件名:\n");
				scanf("%s",s.filename);
				s.info_len=-1;//提前量，让服务器看看当前用户云盘有没有这个文件，有的话删掉
				strcpy(s.username,loginUsername);
				send(sockfd,&s,sizeof(s),0);
				usleep(300);//让服务器先处理
				FILE* fp=fopen(s.filename,"r");
				s.info_len=fread(s.info,1,sizeof(s.info),fp);
				while(s.info_len>0)
				{
					send(sockfd,&s,sizeof(s),0);
					s.info_len=fread(s.info,1,sizeof(s.info),fp);
				}
				fclose(fp);
				printf("上传成功\n");
				continue;
			}
		}
		else if(s.type==6)//history
		{
			if(islogin==0)
			{
				printf("请登录\n");
				continue;
			}
			else
			{
				strcpy(s.username,loginUsername);//将登陆的账号放到结构体中 
			}

		}
		else if(s.type==7)//注销账号
		{
			if(islogin==0)
			{
				printf("当前未登录\n");
				continue;
			}
			strcpy(s.username,loginUsername);
		}
		else if(s.type==8)//exit
		{
			printf("程序结束\n");
			close(sockfd);
			exit(0);//结束进程
		}
		else if(s.type==9)//创建代码仓库
		{
			if(islogin==0)
			{
				printf("请登录\n");
				continue;
			}
			printf("请输入仓库名称：\n");
			scanf("%49s",s.filename);
			printf("请输入仓库描述：\n");
			scanf(" %499[^\n]",s.info);
			strcpy(s.username,loginUsername);
		}
		else if(s.type==10)//查看我的代码仓库
		{
			if(islogin==0)
			{
				printf("请登录\n");
				continue;
			}
			strcpy(s.username,loginUsername);
			s.info_len=0;
		}
		else if(s.type==TYPE_COMMIT)
		{
			if(islogin==0)
			{
				printf("请登录\n");
				continue;
			}
			printf("请输入仓库ID：\n");
			scanf("%d",&s.repository_id);
			printf("请输入代码文件名：\n");
			scanf("%255s",s.filename);
			printf("请输入提交说明：\n");
			scanf(" %255[^\n]",s.commit_message);
			strcpy(s.username,loginUsername);
			s.info_len=-1;
			send_packet(sockfd,&s);
			FILE* fp=fopen(s.filename,"rb");
			if(fp==NULL)
			{
				printf("本地文件打开失败\n");
				continue;
			}
			while((s.info_len=(int)fread(s.info,1,sizeof(s.info),fp))>0)
			{
				send_packet(sockfd,&s);
			}
			fclose(fp);
			continue;
		}
		else if(s.type==TYPE_LIST_VERSIONS || s.type==TYPE_DOWNLOAD_VERSION)
		{
			if(islogin==0)
			{
				printf("请登录\n");
				continue;
			}
			printf("请输入仓库ID：\n");
			scanf("%d",&s.repository_id);
			strcpy(s.username,loginUsername);
			if(s.type==TYPE_DOWNLOAD_VERSION)
			{
				version_download_mode=1;
				version_repository_id=s.repository_id;
				version_selection_in_progress=1;
				s.type=TYPE_LIST_VERSIONS;
			}
		}
        send(sockfd,&s,sizeof(s), 0);
		sleep(1);//给服务器的时间
    }
    
	close(sockfd);//关闭socket使用close
}
