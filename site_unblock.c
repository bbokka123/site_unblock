/* proxy with one thread!
   function : add dummy
   reffered: simple_tcpclient.c , simple_tcpserver.c */

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>

#define BUFSIZE 1024*1024

char *dummy="GET / HTTP/1.1\r\nHost: www.dummy.com\r\n\r\n";

void error(char *msg){
	perror(msg);
	exit(1);
}

//memmem is similar with strstr find in length hlen
void *memmem(const void *haystack, size_t hlen, const void *needle, size_t nlen)
{
    int needle_first;
    const void *p = haystack;
    size_t plen = hlen;

    if (!nlen)
        return NULL;

    needle_first = *(unsigned char *)needle;

    while (plen >= nlen && (p = memchr(p, needle_first, plen - nlen + 1)))
    {
        if (!memcmp(p, needle, nlen))
            return (void *)p;

        p++;
        plen = hlen - (p - haystack);
    }

    return NULL;
}



static ssize_t read_all(int fd, void *buf, size_t bufsize) {
  void *current = buf;
  size_t remain = bufsize;

  do {
    ssize_t count = read(fd, current, remain);
    if (count == 0 || count == -1) { break; }

    remain -= count;
    current = (void*)((uintptr_t)current + count);
  } while (remain > 0);
  return bufsize - remain;
}

static ssize_t read_header(int fd, void *buf, size_t bufsize) {
  void *current = buf;
  size_t remain = bufsize;
  int end = 0;

  do {
    ssize_t count = read(fd, current, remain);
    if (count == 0 || count == -1) { break; }

    void *pos = memmem(current, count, "\r\n\r\n", 4);
    if (pos != NULL) { end = 1; }

    remain -= count;
    current = (void*)((uintptr_t)current + count);
  } while (remain > 0 && !end);
  return bufsize - remain;
}


static ssize_t write_all(int fd, const void *buf, size_t count) {
  const void *current = buf;
  size_t remain = count;

  do {
    ssize_t count = write(fd, current, remain);
    if (count == -1) { return -1; }

    remain -= count;
    current = (void*)((uintptr_t)current + count);
  } while (remain > 0);
  return count - remain;
}

void* proxy_dummy(int *fd){
	int sockfd, portno, n;
	struct sockaddr_in serveraddr;
	struct hostent *server;
	char *hostname;
	void* buf=(void *)malloc(BUFSIZE);
	void* buf2=(void *)malloc(BUFSIZE);
	int client_fd = *((int*)fd);
	char *site=(char*)malloc(2000);
	
	bzero(buf, BUFSIZE);
	bzero(buf2, BUFSIZE);
	bzero(site, 2000);
	
	n=read_header(client_fd, buf, BUFSIZE);
	if(n<0){
	perror("error reading from client");
	free(buf);
	free(buf2);
	free(site);
	close(client_fd);
	return ;
	}
	
	char *start=memmem(buf, n , "Host: ",6);
	if(start== NULL) {
	perror("its not http");	
	free(buf);
	free(buf2);
	free(site);
	close(client_fd);
 	return ;
	}
	char *end = memchr(start, '\r', BUFSIZE - ((uintptr_t)start - (uintptr_t)buf));
	start=(uintptr_t)start+6;
	int host_len = (uintptr_t)end - (uintptr_t)start;
	memcpy(site, start, host_len);
	site[host_len] = '\0';
	printf("site:%s\n",site);
	
	hostname = site;
	portno = 80;
	
	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if(sockfd < 0){
		error("ERROR opening socket");
		free(buf);
		free(buf2);
		free(site);
		close(client_fd);
		return ;
	} 
	server = gethostbyname(hostname);
	if(server == NULL){
		fprintf(stderr, "ERROR, no such host as %s\n",hostname);
		free(buf); free(buf2); free(site); close(client_fd); return;
		}
	bzero((char *)&serveraddr, sizeof(serveraddr));
	serveraddr.sin_family = AF_INET;
        bcopy((char*)server->h_addr,(char*)&serveraddr.sin_addr.s_addr,server->h_length);
  	serveraddr.sin_port=htons(portno);	
	if(connect(sockfd, &serveraddr, sizeof(serveraddr))<0){
		error("ERROR connecting");
		free(buf);free(buf2);free(site);close(client_fd);return;
	}
	memcpy(buf2, dummy, strlen(dummy));
	memcpy(buf2+strlen(dummy), buf, n);

	int m=write_all(sockfd, buf2, n+strlen(dummy));
	if(m<0) { perror("error server first writing"); free(buf); free(buf2); 	free(site); close(client_fd); return;
	}
	bzero(buf, BUFSIZE); bzero(buf, BUFSIZE);
	n = read_all(sockfd, buf, BUFSIZE);
	if(n<0){ perror("error in first reading ");}
	
	start = memmem(buf, n , "Content-Length: ", 16);
	if(start ==  NULL){ fprintf(stderr, "this is not http\n");
	free(buf); free(buf2); free(site); close(client_fd);
	}
	end = memchr(start, '\r', BUFSIZE-((uintptr_t)start-(uintptr_t)buf));
	start = (uintptr_t)start +16;
	int length=(uintptr_t)end- (uintptr_t)start;
	char content_len[10]; bzero(content_len,10);
	memcpy(content_len, start,length);
	content_len[length] = '\0';
	length = atoi(content_len);
	//tmp points to end of the dummy reply 
        char* tmp = (uintptr_t)(memmem(buf, n, "\r\n\r\n", 4))+4+length;
	length= n-((uintptr_t)tmp - (uintptr_t)buf);
	memcpy(buf2, tmp, length);
	
	m=write_all(client_fd, buf2, length);
	if(m<0){ perror("first writing client");}

	bzero(buf, BUFSIZE);
	bzero(buf2, BUFSIZE);
	while(1) {
		n= read_all(sockfd, buf, BUFSIZE);
		if(n==0) break;
		m= write_all(client_fd, buf, n);
		if(m<0){ perror("error writing to socket"); free(buf); free(buf2); free(site); close (client_fd); return ;
	}
	free(buf);
	free(buf2);
	free(site);
	close(client_fd);
}
	
	
}
int main(int argc, char **argv){
	int parentfd; /* parent socket */
  	int childfd; /* child socket */
  	int portno; /* port to listen on */
  	int clientlen; /* byte size of client's address */
  	struct sockaddr_in serveraddr; /* server's addr */
  	struct sockaddr_in clientaddr; /* client addr */
  	struct hostent *hostp; /* client host info */
  	char buf[BUFSIZE]; /* message buffer */
  	char *hostaddrp; /* dotted decimal host addr string */
  	int optval; /* flag value for setsockopt */
  	int n; /* message byte size */
  	
	char site[100];
  	char ret_buf[BUFSIZE];
	
	if (argc != 2) {
    		fprintf(stderr, "usage: %s <port>\n", argv[0]);
    		exit(1);
  	}
  	portno = atoi(argv[1]);
  	
	parentfd = socket(AF_INET, SOCK_STREAM, 0);
  	if (parentfd < 0) 
    	error("ERROR opening socket");
	
	optval = 1;
  	setsockopt(parentfd, SOL_SOCKET, SO_REUSEADDR,(const void *)&optval , sizeof(int));
	
	/*
	*build the server's Internet address
   	*/
  	bzero((char *) &serveraddr, sizeof(serveraddr));

  	/* this is an Internet address */
  	serveraddr.sin_family = AF_INET;

  	/* let the system figure out our IP address */
  	serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);

  	/* this is the port we will listen on */
  	serveraddr.sin_port = htons((unsigned short)portno);

  	/* 
   	* bind: associate the parent socket with a port 
   	*/
  	if (bind(parentfd, (struct sockaddr *) &serveraddr, 
     	sizeof(serveraddr)) < 0) 
    	error("ERROR on binding");

  	/* 
   	* listen: make this socket ready to accept connection requests 
   	*/
  	if (listen(parentfd, 5) < 0) /* allow 5 requests to queue up */ 
    	error("ERROR on listen");		

  	/* 
   	* main loop: wait for a connection request, echo input line, 
   	* then close connection.
   	*/
  	clientlen = sizeof(clientaddr);
  	while (1) {
    	/* 
     	* accept: wait for a connection request 
     	*/
    	childfd = accept(parentfd, (struct sockaddr *) &clientaddr, &clientlen);
    	if (childfd < 0) 
        error("ERROR on accept");
    
    	/* 
    	 * gethostbyaddr: determine who sent the message 
    	 */
    	hostp = gethostbyaddr((const char *)&clientaddr.sin_addr.s_addr, sizeof(clientaddr.sin_addr.s_addr), AF_INET);
    	if (hostp == NULL)
        error("ERROR on gethostbyaddr");
    	hostaddrp = inet_ntoa(clientaddr.sin_addr);
    	if (hostaddrp == NULL)
        error("ERROR on inet_ntoa\n");
    	printf("server established connection with %s (%s)\n", 
        hostp->h_name, hostaddrp);
    
	pthread_attr_t attr;
	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr,PTHREAD_CREATE_DETACHED);
	
	pthread_t thread;
	int thread_id=pthread_create(&thread,&attr,proxy_dummy,&childfd);
	if(thread_id){
		fprintf(stderr,"pthread_create error\n");
		continue;
	}
  	}
	close(parentfd);
}
