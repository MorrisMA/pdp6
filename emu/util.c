#include "pdp6.h"
#include <unistd.h>
#include <netdb.h>

void
strtolower(char *s)
{
	for(; *s != '\0'; s++)
		*s = tolower(*s);
}

int
hasinput(int fd)
{
	fd_set fds;
	struct timeval timeout;

	timeout.tv_sec = 0;
	timeout.tv_usec = 0;
	FD_ZERO(&fds);
	FD_SET(fd, &fds);
	return select(fd+1, &fds, NULL, NULL, &timeout) > 0;
}

int
writen(int fd, void *data, int n)
{
	int m;

	while(n > 0){
		m = write(fd, data, n);
		if(m == -1)
			return -1;
		data += m;
		n -= m;
	}
	return 0;
}

int
readn(int fd, void *data, int n)
{
	int m;

	while(n > 0){
		m = read(fd, data, n);
		if(m == -1)
			return -1;
		data += m;
		n -= m;
	}
	return 0;
}

int
dial(const char *host, int port)
{
	char portstr[32];
	int sockfd;
	struct addrinfo *result, *rp, hints;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;

	snprintf(portstr, 32, "%d", port);
	if(getaddrinfo(host, portstr, &hints, &result)){
		perror("error: getaddrinfo");
		return -1;
	}

	for(rp = result; rp; rp = rp->ai_next){
		sockfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
		if(sockfd < 0)
			continue;
		if(connect(sockfd, rp->ai_addr, rp->ai_addrlen) >= 0)
			goto win;
		close(sockfd);
	}
	freeaddrinfo(result);
	perror("error");
	return -1;

win:
	freeaddrinfo(result);
	return sockfd;
}
