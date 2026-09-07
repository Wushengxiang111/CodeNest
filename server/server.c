#include <sys/types.h>         
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdio.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <sqlite3.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <dirent.h>
#include <stdlib.h>
sqlite3* db;//所有线程共用一个数据库
struct net_info
{
    int type;//1.register 2.login 3.list 4.get 5.put 6.history 7.exit
    char username[50];//用户名
    char password[50];//密码
    char filename[50];//文件名
    char info[500];//文件内容
    int info_len;//内容长度
};
//插入历史记录
void insert_history(char* username,char* action)
{
	time_t t=time(0);
	struct tm* tp=localtime(&t);
	char time[100];
	sprintf(time,"%d-%d-%d %02d:%02d:%02d",tp->tm_year+1900,tp->tm_mon+1,tp->tm_mday,
			tp->tm_hour,tp->tm_min,tp->tm_sec);
	char sql[300];
	sprintf(sql,"insert into history_info values('%s','%s','%s');",username,time,action);
	char *err;
	int ret=sqlite3_exec(db,sql,NULL,NULL,&err);
	if(ret!=SQLITE_OK)
	{
		printf("[%s]%s\n",sql,err);
	}
}

//注册
void my_register(int newfd,struct net_info s)
{
	//判断用户名是否存在
	char sql[200];
	sprintf(sql,"select * from user_info where username='%s';",s.username);
	char** q;
	char* err;
	int nrow,ncol;
	int ret=sqlite3_get_table(db,sql,&q,&nrow,&ncol,&err);//执行查询语句
	if(ret==SQLITE_OK)
	{
		if(nrow>0)
		{
			strcpy(s.info,"注册失败");
		}
		else if(nrow==0)
		{
			sprintf(sql,"insert into user_info values('%s','%s');",s.username,s.password);
			ret=sqlite3_exec(db,sql,NULL,NULL,&err);//执行除了查询之外的sql语句
			if(ret!=SQLITE_OK)
			{
				printf("[%s]%s\n",sql,err);
			}
			pid_t pid=fork();
			if(pid==0)//子进程
			{
				execlp("mkdir","mkdir",s.username,NULL);//会爆炸
			}
			else if(pid>0)//父进程
			{
				wait(NULL);
			}
			strcpy(s.info,"注册成功");
		}
		send(newfd,&s,sizeof(s),0);
	}
	else
	{
		printf("[%s]%s\n",sql,err);
	}
}
//登录
void my_login(int newfd,struct net_info s)
{
	//判断用户名和密码是否正确
	char sql[200];
	sprintf(sql,"select * from user_info where username='%s' and password='%s';",s.username,s.password);
	char** q;
	char* err;
	int nrow,ncol;
	int ret=sqlite3_get_table(db,sql,&q,&nrow,&ncol,&err);
	if(ret==SQLITE_OK)
	{
		if(nrow>0)
		{
			strcpy(s.info,"登录成功");
		}
		else if(nrow==0)
		{
			strcpy(s.info,"登录失败");
		}
		send(newfd,&s,sizeof(s),0);
	}
	else
	{
		printf("[%s]%s\n",sql,err);
	}
}
//查看文件
void my_list(int newfd,struct net_info s)
{
	//1.opendir + readdir
	DIR* dp=opendir(s.username);//打开当前账户的文件夹
	if(dp==NULL)
	{
		printf("打开目录失败");
		return;
	}
	struct dirent* ep=readdir(dp);//读数据
	while(ep!=NULL)
	{
		if(ep->d_name[0]!='.')//不是隐藏数据
		{
			strcpy(s.filename,ep->d_name);//将文件名放到结构体中
			send(newfd,&s,sizeof(s),0);//将结构体发给客户端
			usleep(100);//延迟100毫秒
		}
		ep=readdir(dp);//读取下一个文件
	}
	closedir(dp);
	insert_history(s.username,"list");//记录当下操作
}
//下载文件
void my_get(int newfd,struct net_info s)
{
	//拼接地址
	char address[100];
	sprintf(address,"%s/%s",s.username,s.filename);
	FILE* fp=fopen(address,"r");
	fseek(fp,s.info_len,SEEK_SET);//移动文件指针位置
	if(fp==NULL)
	{
		printf("打开失败");
	}
	else
	{
		int ret=fread(s.info,1,sizeof(s.info),fp);
		while(ret>0)
		{
			s.info_len=ret;
			send(newfd,&s,sizeof(s),0);
			ret=fread(s.info,1,sizeof(s.info),fp);
			//两行测试代码
			printf("下载长度:%d\n",s.info_len);
			sleep(2);//两秒延迟
		}
		fclose(fp);
	}
	insert_history(s.username,"get");//记录当下操作
}  
//上传文件
void my_put(int newfd,struct net_info s)
{
	char address[100];
	sprintf(address,"%s/%s",s.username,s.filename);
	if(s.info_len==-1)
	{
		insert_history(s.username,"put");//put可能执行很多次
		FILE* fp=fopen(address,"w");
		fclose(fp);
	}
	else
	{
		FILE* fp=fopen(address,"a");
		fwrite(s.info,1,s.info_len,fp);
		fclose(fp);
	}
}
//查看历史
void my_history(int newfd,struct net_info s)
{
	char sql[100];
	sprintf(sql,"select * from history_info where username='%s';",s.username);
	char **q;
	int nrow,ncol;
	char* err;
	int ret=sqlite3_get_table(db,sql,&q,&nrow,&ncol,&err);
	if(ret==SQLITE_OK)
	{
		if(nrow==0)
		{
			strcpy(s.info,"没有历史记录");
			send(newfd,&s,sizeof(s),0);
		}
		else if(nrow>0)
		{
			for(int i=0;i<nrow+1/*表头的存在*/;i++)
			{
				sprintf(s.info,"%s %s %s",q[i*3],q[i*3+1],q[i*3+2]);
				send(newfd,&s,sizeof(s),0);
				usleep(200);
			}
		}
	}
	else
	{
		printf("[%s]%s\n",sql,err);
	}
}
void* func(void* p)
{
	int newfd = *(int *)p;//将进程中的newfd的值取出，保存到线程 栈区的变量里
	free(p);
	while(1)
	{
		//5.收数据
		struct net_info s;
    	int ret = recv(newfd,&s,sizeof(s),0);
    	if(ret <= 0)
    	{
    		printf("链接断开\n");
    		break;
    	}
		printf("type is %d\n",s.type);
        if(s.type==1)
		{
			my_register(newfd,s);//执行注册操作
		}
		else if(s.type==2)
		{
			my_login(newfd,s);//登录操作
		}
		else if(s.type==3)
		{
			my_list(newfd,s);//查看文件
		}
		else if(s.type==4)
		{
			my_get(newfd,s);//下载文件
		}
		else if(s.type==5)//上传文件
		{
			my_put(newfd,s);
		}
		else if(s.type==6)//history
		{
			my_history(newfd,s);
		}
	}
	close(newfd);//链接通道关闭
	return NULL;
}

int main()
{
	//打开数据库
	int sqlret=sqlite3_open("netdisk.db",&db);
	if(sqlret!=SQLITE_OK)
	{
		printf("打开数据库失败\n");
		return 0;//结束程序
	}
    //1.创建
   	int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    
    //2.绑定
    struct sockaddr_in myaddr;
	myaddr.sin_family = AF_INET;//固定的 代表使用ipv4
	myaddr.sin_port = htons(8888);//端口号取值范围是0-65535 short类型
	myaddr.sin_addr.s_addr = htonl(INADDR_ANY);//自动识别本机ip地址
	int ret = bind(sockfd, (struct sockaddr *)&myaddr, sizeof(myaddr));
    printf("bind %d\n", ret);
    
    //3.设置监听
    listen(sockfd, 5);
    
    //4.阻塞等待客户端链接
    while(1)
    {
        int newfd = accept(sockfd, NULL, NULL);
    	printf("newfd %d\n", newfd);
		//不写下面这两行会有bug
		int* p=malloc(sizeof(int));
		*p=newfd;

    	pthread_t id;
    	pthread_create(&id, NULL, func, p);//将newfd作为参数传进去
		pthread_detach(id);//线程跑完自动回收资源
    }
    
    //6.关闭
    close(sockfd);
}

