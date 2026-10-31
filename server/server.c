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
#include <sys/stat.h>
#include <dirent.h>
#include <stdlib.h>
#include <time.h>
#include "../common/protocol.h"

#define TYPE_LIST_VERSIONS 12
#define TYPE_DOWNLOAD_VERSION 13

sqlite3* db;//所有线程共用一个数据库
pthread_mutex_t repository_mutex = PTHREAD_MUTEX_INITIALIZER;
//插入历史记录
void insert_history(char* username,char* action)
{
	time_t t=time(0);
	struct tm* tp=localtime(&t);
	char time[100];
	snprintf(time, sizeof(time), "%d-%d-%d %02d:%02d:%02d",tp->tm_year+1900,tp->tm_mon+1,tp->tm_mday,
			tp->tm_hour,tp->tm_min,tp->tm_sec);
	char sql[300];
	if(snprintf(sql, sizeof(sql), "insert into history_info values('%s','%s','%s');", username,time,action) >= (int)sizeof(sql))
	{
		return;
	}
	char *err;
	int ret=sqlite3_exec(db,sql,NULL,NULL,&err);
	if(ret!=SQLITE_OK)
	{
		printf("[%s]%s\n",sql,err);
	}
}

//初始化代码仓库表
void init_repository_table()
{
	char* err = NULL;
	char* sql = "create table if not exists repositories ("
		"id integer primary key autoincrement,"
		"name text not null,"
		"owner_id text not null,"
		"description text default '',"
		"current_version integer default 0,"
		"create_time text not null);";
	int ret = sqlite3_exec(db, sql, NULL, NULL, &err);
	if(ret != SQLITE_OK)
	{
		printf("创建repositories表失败:%s\n", err);
		sqlite3_free(err);
	}
}

void init_commits_table()
{
	char* err = NULL;
	char* sql = "create table if not exists commits ("
		"id integer primary key autoincrement,"
		"repository_id integer not null,"
		"user_id text not null,"
		"version integer not null,"
		"parent_version integer not null,"
		"message text not null,"
		"create_time text not null,"
		"path text not null);";
	if(sqlite3_exec(db, sql, NULL, NULL, &err) != SQLITE_OK)
	{
		printf("创建commits表失败:%s\n", err);
		sqlite3_free(err);
	}
}

int repository_owner(int repository_id, const char* username)
{
	char sql[300];
	char** q;
	char* err = NULL;
	int nrow, ncol;
	if(snprintf(sql, sizeof(sql), "select id from repositories where id=%d and owner_id='%s';",
		repository_id, username) >= (int)sizeof(sql))
	{
		return 0;
	}
	int ret = sqlite3_get_table(db, sql, &q, &nrow, &ncol, &err);
	if(ret != SQLITE_OK)
	{
		sqlite3_free(err);
		return 0;
	}
	sqlite3_free_table(q);
	return nrow > 0;
}

void my_commit(int newfd, struct net_info s, char* commit_path, size_t path_size, int* commit_active)
{
	if(s.info_len != -1)
	{
		if(!*commit_active)
		{
			return;
		}
		FILE* fp = fopen(commit_path, "ab");
		if(fp != NULL)
		{
			fwrite(s.info, 1, s.info_len, fp);
			fclose(fp);
		}
		return;
	}

	pthread_mutex_lock(&repository_mutex);
	if(!repository_owner(s.repository_id, s.username))
	{
		*commit_active = 0;
		pthread_mutex_unlock(&repository_mutex);
		strcpy(s.info, "你没有权限访问该仓库");
		s.type = TYPE_COMMIT;
		send_packet(newfd, &s);
		return;
	}

	char sql[1200];
	char** q;
	char* err = NULL;
	int nrow, ncol;
	if(snprintf(sql, sizeof(sql), "select current_version from repositories where id=%d;", s.repository_id)
		>= (int)sizeof(sql))
	{
		*commit_active = 0;
		pthread_mutex_unlock(&repository_mutex);
		strcpy(s.info, "仓库查询失败");
		send_packet(newfd, &s);
		return;
	}
	if(sqlite3_get_table(db, sql, &q, &nrow, &ncol, &err) != SQLITE_OK || nrow == 0)
	{
		*commit_active = 0;
		pthread_mutex_unlock(&repository_mutex);
		strcpy(s.info, "仓库不存在");
		send_packet(newfd, &s);
		return;
	}
	int parent_version = atoi(q[1]);
	int new_version = parent_version + 1;
	sqlite3_free_table(q);

	if(snprintf(commit_path, path_size, "repositories/%d/versions/%d/%s",
		s.repository_id, new_version, s.filename) >= (int)path_size)
	{
		*commit_active = 0;
		pthread_mutex_unlock(&repository_mutex);
		strcpy(s.info, "版本路径过长");
		send_packet(newfd, &s);
		return;
	}
	char version_dir[300];
	if(snprintf(version_dir, sizeof(version_dir), "repositories/%d/versions/%d",
		s.repository_id, new_version) >= (int)sizeof(version_dir))
	{
		*commit_active = 0;
		pthread_mutex_unlock(&repository_mutex);
		strcpy(s.info, "版本路径过长");
		send_packet(newfd, &s);
		return;
	}
	mkdir("repositories", 0755);
	char repository_dir[100];
	if(snprintf(repository_dir, sizeof(repository_dir), "repositories/%d", s.repository_id)
		>= (int)sizeof(repository_dir))
	{
		*commit_active = 0;
		pthread_mutex_unlock(&repository_mutex);
		strcpy(s.info, "仓库路径过长");
		send_packet(newfd, &s);
		return;
	}
	mkdir(repository_dir, 0755);
	char versions_dir[150];
	if(snprintf(versions_dir, sizeof(versions_dir), "%s/versions", repository_dir)
		>= (int)sizeof(versions_dir))
	{
		*commit_active = 0;
		pthread_mutex_unlock(&repository_mutex);
		strcpy(s.info, "版本路径过长");
		send_packet(newfd, &s);
		return;
	}
	mkdir(versions_dir, 0755);
	mkdir(version_dir, 0755);

	FILE* fp = fopen(commit_path, "wb");
	if(fp == NULL)
	{
		*commit_active = 0;
		pthread_mutex_unlock(&repository_mutex);
		strcpy(s.info, "版本文件创建失败");
		send_packet(newfd, &s);
		return;
	}
	fclose(fp);

	char create_time[100];
	time_t now = time(NULL);
	struct tm* tm_now = localtime(&now);
	snprintf(create_time, sizeof(create_time), "%d-%d-%d %02d:%02d:%02d", tm_now->tm_year + 1900,
		tm_now->tm_mon + 1, tm_now->tm_mday, tm_now->tm_hour, tm_now->tm_min, tm_now->tm_sec);
	if(snprintf(sql, sizeof(sql), "insert into commits(repository_id,user_id,version,parent_version,message,create_time,path) "
		"values(%d,'%s',%d,%d,'%s','%s','%s');", s.repository_id, s.username,
		new_version, parent_version, s.commit_message, create_time, commit_path) >= (int)sizeof(sql))
	{
		*commit_active = 0;
		pthread_mutex_unlock(&repository_mutex);
		strcpy(s.info, "提交记录过长");
		send_packet(newfd, &s);
		return;
	}
	if(sqlite3_exec(db, sql, NULL, NULL, &err) != SQLITE_OK)
	{
		printf("提交记录保存失败:%s\n", err);
		sqlite3_free(err);
	}
	snprintf(sql, sizeof(sql), "update repositories set current_version=%d where id=%d;",
		new_version, s.repository_id);
	sqlite3_exec(db, sql, NULL, NULL, &err);
	pthread_mutex_unlock(&repository_mutex);

	s.base_version = new_version;
	*commit_active = 1;
	s.type = TYPE_COMMIT;
	snprintf(s.info, sizeof(s.info), "提交成功，版本 %d", new_version);
	send_packet(newfd, &s);
}

void my_list_versions(int newfd, struct net_info s)
{
	if(!repository_owner(s.repository_id, s.username))
	{
		strcpy(s.info, "你没有权限访问该仓库");
		s.type = TYPE_LIST_VERSIONS;
		s.repository_id = -1;
		send_packet(newfd, &s);
		return;
	}
	char sql[300];
	char** q;
	char* err = NULL;
	int nrow, ncol;
	if(snprintf(sql, sizeof(sql), "select version,message,create_time from commits where repository_id=%d order by version desc;",
		s.repository_id) >= (int)sizeof(sql))
	{
		strcpy(s.info, "版本查询失败");
		s.type = TYPE_LIST_VERSIONS;
		s.repository_id = -1;
		send_packet(newfd, &s);
		return;
	}
	if(sqlite3_get_table(db, sql, &q, &nrow, &ncol, &err) == SQLITE_OK)
	{
		for(int i = 1; i <= nrow; i++)
		{
			s.type = TYPE_LIST_VERSIONS;
			s.base_version = atoi(q[i * ncol]);
			strcpy(s.commit_message, q[i * ncol + 1]);
			strcpy(s.info, q[i * ncol + 2]);
			send_packet(newfd, &s);
		}
		sqlite3_free_table(q);
	}
	s.type = TYPE_LIST_VERSIONS;
	s.repository_id = -1;
	send_packet(newfd, &s);
}

void my_download_version(int newfd, struct net_info s)
{
	if(!repository_owner(s.repository_id, s.username))
	{
		strcpy(s.info, "你没有权限访问该仓库");
		s.type = TYPE_DOWNLOAD_VERSION;
		send_packet(newfd, &s);
		return;
	}
	char sql[300];
	char** q;
	char* err = NULL;
	int nrow, ncol;
	if(snprintf(sql, sizeof(sql), "select path from commits where repository_id=%d and version=%d;",
		s.repository_id, s.base_version) >= (int)sizeof(sql))
	{
		strcpy(s.info, "版本查询失败");
		s.type = TYPE_DOWNLOAD_VERSION;
		send_packet(newfd, &s);
		return;
	}
	if(sqlite3_get_table(db, sql, &q, &nrow, &ncol, &err) != SQLITE_OK || nrow == 0)
	{
		strcpy(s.info, "版本不存在");
		s.type = TYPE_DOWNLOAD_VERSION;
		send_packet(newfd, &s);
		return;
	}
	char path[300];
	strcpy(path, q[1]);
	sqlite3_free_table(q);
	FILE* fp = fopen(path, "rb");
	if(fp == NULL)
	{
		strcpy(s.info, "版本文件不存在");
		s.type = TYPE_DOWNLOAD_VERSION;
		send_packet(newfd, &s);
		return;
	}
	strcpy(s.filename, strrchr(path, '/') + 1);
	s.type = TYPE_DOWNLOAD_VERSION;
	strcpy(s.info, "开始下载");
	send_packet(newfd, &s);
	s.type = TYPE_GET;
	while((s.info_len = fread(s.info, 1, sizeof(s.info), fp)) > 0)
	{
		send_packet(newfd, &s);
	}
	fclose(fp);
}

//创建代码仓库
void my_create_repository(int newfd, struct net_info s)
{
	char sql[1200];
	char create_time[100];
	time_t t = time(0);
	struct tm* tp = localtime(&t);
	snprintf(create_time, sizeof(create_time), "%d-%d-%d %02d:%02d:%02d", tp->tm_year + 1900,
		tp->tm_mon + 1, tp->tm_mday, tp->tm_hour, tp->tm_min, tp->tm_sec);
	if(snprintf(sql, sizeof(sql), "insert into repositories(name,owner_id,description,current_version,create_time) "
		"values('%s','%s','%s',0,'%s');", s.filename, s.username, s.info, create_time)
		>= (int)sizeof(sql))
	{
		strcpy(s.info, "仓库信息过长");
		send(newfd, &s, sizeof(s), 0);
		return;
	}
	char* err = NULL;
	int ret = sqlite3_exec(db, sql, NULL, NULL, &err);
	if(ret != SQLITE_OK)
	{
		strcpy(s.info, "创建仓库失败");
		printf("[%s]%s\n", sql, err);
		sqlite3_free(err);
		send(newfd, &s, sizeof(s), 0);
		return;
	}

	s.info_len = (int)sqlite3_last_insert_rowid(db);
	char repository_path[100];
	char versions_path[120];
	snprintf(repository_path, sizeof(repository_path), "repositories/%d", s.info_len);
	snprintf(versions_path, sizeof(versions_path), "%s/versions", repository_path);
	if((mkdir("repositories", 0755) != 0 && access("repositories", F_OK) != 0) ||
		(mkdir(repository_path, 0755) != 0 && access(repository_path, F_OK) != 0) ||
		(mkdir(versions_path, 0755) != 0 && access(versions_path, F_OK) != 0))
	{
		strcpy(s.info, "仓库创建成功，但目录创建失败");
	}
	else
	{
		snprintf(s.info, sizeof(s.info), "仓库创建成功，repository_id=%d", s.info_len);
	}
	send(newfd, &s, sizeof(s), 0);
}

//查看当前用户拥有的代码仓库
void my_list_repository(int newfd, struct net_info s)
{
	char sql[300];
	if(snprintf(sql, sizeof(sql), "select id,name,description,current_version from repositories where owner_id='%s';",
		s.username) >= (int)sizeof(sql))
	{
		strcpy(s.info, "查询条件过长");
		send(newfd, &s, sizeof(s), 0);
		return;
	}
	char** q;
	char* err;
	int nrow, ncol;
	int ret = sqlite3_get_table(db, sql, &q, &nrow, &ncol, &err);
	if(ret != SQLITE_OK)
	{
		strcpy(s.info, "查询仓库失败");
		send(newfd, &s, sizeof(s), 0);
		return;
	}
	if(nrow == 0)
	{
		strcpy(s.info, "当前没有代码仓库");
		send(newfd, &s, sizeof(s), 0);
	}
	else
	{
		for(int i = 1; i <= nrow; i++)
		{
			s.info_len = atoi(q[i * ncol]);
			strcpy(s.filename, q[i * ncol + 1]);
			snprintf(s.info, sizeof(s.info), "描述：%s 当前版本：%s", q[i * ncol + 2], q[i * ncol + 3]);
			send(newfd, &s, sizeof(s), 0);
			usleep(100);
		}
	}
	sqlite3_free_table(q);
}

//注册
void my_register(int newfd,struct net_info s)
{
	//判断用户名是否存在
	char sql[200];
	snprintf(sql, sizeof(sql), "select * from user_info where username='%s';", s.username);
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
			snprintf(sql, sizeof(sql), "insert into user_info values('%s','%s');", s.username, s.password);
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
	snprintf(sql, sizeof(sql), "select * from user_info where username='%s' and password='%s';", s.username, s.password);
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
	char address[50 + NETDISK_NAME_SIZE + 2];
	if(snprintf(address, sizeof(address), "%s/%s", s.username, s.filename) >= (int)sizeof(address))
	{
		printf("文件路径过长\n");
		return;
	}
	FILE* fp=fopen(address,"r");
	if(fp==NULL)
	{
		printf("打开失败");
	}
	else
	{
		fseek(fp,s.info_len,SEEK_SET);//移动文件指针位置
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
	char address[50 + NETDISK_NAME_SIZE + 2];
	if(snprintf(address, sizeof(address), "%s/%s", s.username, s.filename) >= (int)sizeof(address))
	{
		printf("文件路径过长\n");
		return;
	}
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
	char sql[200];
	snprintf(sql, sizeof(sql), "select * from history_info where username='%s';", s.username);
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
				snprintf(s.info, sizeof(s.info), "%s %s %s", q[i*3], q[i*3+1], q[i*3+2]);
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
int remove_user_directory(const char* path)
{
	DIR* dp=opendir(path);
	if(dp==NULL)
	{
		return -1;
	}
	struct dirent* ep=readdir(dp);
	while(ep!=NULL)
	{
		if(strcmp(ep->d_name,".")!=0 && strcmp(ep->d_name,"..")!=0)
		{
			char child[512];
			struct stat st;
			if(snprintf(child, sizeof(child), "%s/%s", path, ep->d_name) >= (int)sizeof(child))
			{
				closedir(dp);
				return -1;
			}
			if(stat(child,&st)!=0)
			{
				closedir(dp);
				return -1;
			}
			if(S_ISDIR(st.st_mode))
			{
				if(remove_user_directory(child)!=0)
				{
					closedir(dp);
					return -1;
				}
			}
			else if(remove(child)!=0)
			{
				closedir(dp);
				return -1;
			}
		}
		ep=readdir(dp);
	}
	closedir(dp);
	return rmdir(path);
}
//注销账号并删除账号数据
void my_delete_account(int newfd,struct net_info s)
{
	char sql[200];
	char* err;
	int ret;
	snprintf(sql, sizeof(sql), "delete from history_info where username='%s';", s.username);
	ret=sqlite3_exec(db,sql,NULL,NULL,&err);
	if(ret==SQLITE_OK)
	{
		snprintf(sql, sizeof(sql), "delete from user_info where username='%s';", s.username);
		ret=sqlite3_exec(db,sql,NULL,NULL,&err);
	}
	if(ret==SQLITE_OK && remove_user_directory(s.username)==0)
	{
		strcpy(s.info,"账号注销成功");
	}
	else
	{
		strcpy(s.info,"账号注销失败");
	}
	send(newfd,&s,sizeof(s),0);
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
		static __thread char commit_path[300];
		static __thread int commit_active = 0;
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
		else if(s.type==7)//注销账号
		{
			my_delete_account(newfd,s);
		}
		else if(s.type==8)//退出
		{
			break;
		}
		else if(s.type==9)//创建代码仓库
		{
			my_create_repository(newfd,s);
		}
		else if(s.type==10)//查看我的代码仓库
		{
			my_list_repository(newfd,s);
		}
		else if(s.type==TYPE_COMMIT)
		{
			my_commit(newfd, s, commit_path, sizeof(commit_path), &commit_active);
		}
		else if(s.type==TYPE_LIST_VERSIONS)
		{
			my_list_versions(newfd, s);
		}
		else if(s.type==TYPE_DOWNLOAD_VERSION)
		{
			my_download_version(newfd, s);
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
	init_repository_table();
	init_commits_table();
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

