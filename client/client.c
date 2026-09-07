#include <sys/types.h>         
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <stdlib.h>
#include <arpa/inet.h>
int islogin=0;//登录状态
char loginUsername[50];//登录成功的用户名
struct net_info
{
    int type;//1.register 2.login 3.list 4.get 5.put 6.history 7.exit
    char username[50];//用户名
    char password[50];//密码
    char filename[50];//文件名
    char info[500];//文件内容
    int info_len;//内容长度
};
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
		if(s.type==6)//history
		{
			printf("%s\n",s.info);
		}
    }
}

int main(int argc, char* argv[])
{
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
		struct net_info s;
   		printf("1.register 2.login 3.list 4.get 5.put 6.history 7.exit\n");
		printf("请输入您的选择:\n");
		scanf("%d",&s.type);
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
						s.info_len==0;
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
		else if(s.type==7)//exit
		{
			printf("程序结束\n");
			close(sockfd);
			exit(0);//结束进程
		}
        send(sockfd,&s,sizeof(s), 0);
		sleep(1);//给服务器的时间
    }
    
	close(sockfd);//关闭socket使用close
}

