/**************************************************************************************************
 * HTTP POST upload file
 * (c) Part of the Wilson project (www.iambobot.com) 
 * 
 * Oct 2020
 **************************************************************************************************
*/
#include <unistd.h>     /* Symbolic Constants */
#include <sys/types.h>  /* Primitive System Data Types */ 
#include <errno.h>      /* Errors */
#include <string.h>
#include <fstream>
#include <sstream>
#include <fcntl.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/time.h>

#include "HTTPpost.h"

using namespace std;

char CloudHost_name[256];
char CloudHost_serverpath[256]; 
unsigned int CloudHost_port;
int h_errno;

void upload_system_error(const char *msg);

void hhtpPOST_init(const char *hostname, const char *path, unsigned int port)
{
	snprintf(CloudHost_name, sizeof(CloudHost_name), "%s", hostname);
	snprintf(CloudHost_serverpath, sizeof(CloudHost_serverpath), "%s", path);
	CloudHost_port= port;
}


// Send command to the cloud server and receive and process response
int hhtpPOST_upload(char *phtml, size_t szhtml, double *elapsed, char **xmlcode_ptr)
{
	char str[256];
	int sockfd;
	struct sockaddr_in servaddr;
	char **pptr;
	struct hostent *hptr;
	char str1[200];

	// Check if network is available
	if((hptr = gethostbyname(CloudHost_name)) == NULL) 
	{
		fprintf(stderr, "\n[ERROR] gethostbyname error for host: %s: %s", CloudHost_name, hstrerror(h_errno));
		fflush(stderr);
		return -1;
	}
#ifdef HTTPPOST_DEBUG_ENABLED
	fprintf(stdout, "\nhostname: %s", hptr->h_name);
	fflush(stdout);
#endif	
	if (hptr->h_addrtype == AF_INET && (pptr = hptr->h_addr_list) != NULL) {
	
//		fprintf(stdout, "\naddress: %s\n", inet_ntop(hptr->h_addrtype, *pptr, str, sizeof(str)));
//		fflush(stdout);
		inet_ntop(hptr->h_addrtype, *pptr, str, sizeof(str));
	
	} 
	else {
		fprintf(stderr, "\n[ERROR]Error call inet_ntop");
		fflush(stderr);
		return -1;
	}
	
	
	// 1. create socket to send IP cloud message
	// SOCKET
	sockfd = socket(AF_INET, SOCK_STREAM, 0); // AF_INET: IPv4 Internet protocols  / SOCK_STREAM: TCP sockect
	bzero(&servaddr, sizeof(servaddr));
	servaddr.sin_family = AF_INET;
	servaddr.sin_port = htons(CloudHost_port);
	inet_pton(AF_INET, str, &servaddr.sin_addr);	
	// Set time-out before connect
	struct timeval timeout;      
	timeout.tv_sec = 3; // 3 second timeout
	timeout.tv_usec = 0;
	if (setsockopt (sockfd, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(timeout)) < 0)
		upload_system_error("setsockopt failed\n");
	if (setsockopt (sockfd, SOL_SOCKET, SO_SNDTIMEO, (char *)&timeout, sizeof(timeout)) < 0)
		upload_system_error("setsockopt failed\n");
	int r= connect(sockfd, (struct sockaddr *) & servaddr, sizeof(servaddr));
	// (END) SOCKET
	
#ifdef HTTPPOST_DEBUG_ENABLED
	sprintf(str1, "\nSocket (%d)\n", r);
	write(STDOUT_FILENO, str1, strlen(str1));
#endif	
		
	if(r != 0) 
	{
		upload_system_error("sockect error - no connection");
		return -1;
	}
	else 
	{
		// 2. SEND
		ssize_t n;
		n= write(sockfd, phtml, szhtml);
		
#ifdef HTTPPOST_DEBUG_ENABLED			
		sprintf(str1, "***** Sent (%d)\n", n);
		write(STDOUT_FILENO, str1, strlen(str1));
#endif	

		timeval t0, now;
		gettimeofday(&t0, NULL);

		// 4. RECEIVE response
		n = read(sockfd, phtml, szhtml);
		if(n>0)
		{
			phtml[n]='\0';
			
		}
		else if(n==0) {
			phtml[0]='\0';
			snprintf(str1, sizeof(str1), "--- ZERO BYTES read");
			write(STDOUT_FILENO, str1, strlen(str1));
			close(sockfd);
			return -1;
		}
		else // n<0 
		{
			phtml[0]='\0';
			snprintf(str1, sizeof(str1), "-- errno= %d -- %s", errno, strerror(errno));
			write(STDOUT_FILENO, str1, strlen(str1));
			close(sockfd);
			return -1;
		}
		
#ifdef HTTPPOST_DEBUG_ENABLED		
		sprintf(str1, "***** Received (%d)\n", n);
		write(STDOUT_FILENO, str1, strlen(str1));
		write(STDOUT_FILENO, phtml, n);
		write(STDOUT_FILENO, "\n\n", 2);
#endif		
		
		// elapsed time
		gettimeofday(&now, NULL);
		if(elapsed) *elapsed= ((now.tv_sec * 1000000 + now.tv_usec) - (t0.tv_sec * 1000000 + t0.tv_usec));  // microsecs
		
		// response
		// pointer to the xml part of the response
		size_t pos = string(phtml).find("<?xml");
		if(xmlcode_ptr) *xmlcode_ptr= (pos!= string::npos)? &phtml[pos] : 0;
	}
	close(sockfd);
	return 0;
}


// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
size_t hhtpPOST_header(const char *filename, char *buffer, size_t max, size_t *pos, size_t sz_Payload)
{
	// CONTENT
	const char boundary[]= "EtherJuice__26261265391015";
	
	// Content
	// content header
	char content_header[512];
	snprintf(content_header, sizeof(content_header),	
	"--%s\r\n"
    "Content-Disposition: form-data; name=\"Action\"\r\n"
	"\r\n"
	"%s\r\n"
	"--%s\r\n"
	"Content-Disposition: form-data; name=\"file1\"; filename=\"%s\"\r\n"
	"Content-Type: image/jpg\r\n"
	"\r\n"
	, boundary, "IMAGEUP", boundary, filename
	);
	size_t sz_content_header= strlen(content_header);
	
	// content tail
	char content_tail[512];
	snprintf(content_tail, sizeof(content_tail),	
	"\r\n\r\n"	
    "--%s--"
	, boundary
	);
	size_t sz_content_tail= strlen(content_tail);
	
	size_t Content_length= sz_content_header + sz_Payload + sz_content_tail;
	
	// POST message
	size_t sz_b;
	// 1. header
	snprintf(buffer, max,
	"POST %s HTTP/1.0\r\n"
	"Accept: text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8\r\n"
	"Accept-Language: en-US,en;q=0.5\r\n"
	"DNT: 1\r\n"
	"Host: %s\r\n"
	"User-Agent: Mozilla/5.0 (Windows NT 6.1; WOW64; rv:45.0) Gecko/20100101 Firefox/45.0\r\n"
	"Content-Type: multipart/form-data; boundary=%s\r\n"
	, CloudHost_serverpath, CloudHost_name,  boundary);
	sz_b= strlen(buffer);
	
	// add Content-length:
	snprintf(&buffer[sz_b], max-sz_b,
	"Content-length: %d\r\n\r\n", Content_length);
	size_t sz_header= sz_b= strlen(buffer);
	
	// 2. add content_header
	snprintf(&buffer[sz_b], max-sz_b,"%s", content_header);
	sz_b= strlen(buffer);	
	
	// 3. add payload
//	memset(&buffer[sz_b], 'z', sz_Payload);
	*pos= sz_b;
	sz_b += sz_Payload;

	// 4. add sz_content_tail
	memcpy(&buffer[sz_b], content_tail, sz_content_tail+1);
	
	return (sz_header + Content_length);
}
size_t generate_HTML_content_0(char *buffer, size_t max, char *content)
{
	// SEND CLOUD RESPONSE
	snprintf(buffer, max,
	"POST %s HTTP/1.0\r\n"
	"Host: %s\r\n"
//	"Content-Type: application/x-www-form-urlencoded; charset=UTF-8\r\n"
	"Content-Type: application/x-www-form-urlencoded; charset=ISO-8859-1\r\n"
	"Content-length: %d\r\n\r\n"
	"%s"
	, CloudHost_serverpath, CloudHost_name, strlen(content), content);
	return strlen(buffer);
}
void upload_system_error(const char *msg) //  C library function void perror(const char *str) 
{
	fprintf(stderr, "[ERROR] --------- %s", msg);
	fflush(stderr);
    perror(msg);
//    exit(1);
}

/* END OF FILE */