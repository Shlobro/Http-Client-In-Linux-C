// //
// // Created by student on 12/27/24.
// //
//
// #include <stdio.h>
// #include <stdlib.h>
// #include <string.h>
// #include <unistd.h>
// #include <sys/types.h>
// #include <sys/socket.h>
// #include <netinet/in.h>
// #include <netdb.h>       // for gethostbyname
// #include <arpa/inet.h>   // for inet_addr
// #include <ctype.h>       // for isdigit
// #include <errno.h>
//
// /*
//  * We define a constant for the maximum buffer size we'll use to receive
//  * response data from the server. You can tweak this as needed.
//  */
// #define MAX_BUFFER_SIZE 8192
//
// /*
//  * Our command-line struct to store URL and parameters
//  */
// typedef struct {
//     char *url;
//     int  numParams;
//     char **params;   // array of "name=value" strings
// } CmdArgs;
//
// /*
//  * Helper function prototypes
//  */
// void printUsageAndExit();
// int isPositiveNumberUnder16Bit(const char *str);
// int parseArguments(int argc, char *argv[], CmdArgs *cmd);
//
// int parseURL(const char *url,
//              char *host,
//              int  *port,
//              char *path);
// int buildHTTPRequest(const char *host,
//                      const char *path,
//                      int numParams,
//                      char **params,
//                      char *requestBuffer,
//                      size_t requestBufferSize);
// int connectToServer(const char *hostname, int port);
// int sendAll(int sockfd, const char *buf, size_t len);
// int receiveResponse(int sockfd, char **response, int *responseSize);
// int extractStatusCode(const char *response);
// int extractLocationHeader(const char *response, char *locationURL, size_t maxLen);
// int isHTTP(const char *maybeURL);
//
// /*
//  * MAIN
//  */
// int main(int argc, char *argv[])
// {
//     CmdArgs cmd;
//     // ----------------------------------------------------------------
//     // Parse command-line arguments into cmd.url, cmd.numParams, cmd.params
//     // ----------------------------------------------------------------
//     parseArguments(argc, argv, &cmd);
//
//     // Optional debug prints (you can remove these if you want)
//     printf("URL = %s\n", cmd.url);
//     printf("numParams = %d\n", cmd.numParams);
//     for (int i = 0; i < cmd.numParams; i++) {
//         printf("   param[%d] = %s\n", i, cmd.params[i]);
//     }
//
//     // We'll allow multiple "rounds" if we get a 3XX redirect
//     // that points to another HTTP location
//     int redirectCount = 0;
//     const int MAX_REDIRECTS = 10;  // to prevent infinite loops
//
//     // currentURL holds the *working* URL that may be updated on redirects
//     char currentURL[1024];
//     strncpy(currentURL, cmd.url, sizeof(currentURL) - 1);
//     currentURL[sizeof(currentURL) - 1] = '\0';
//
//     while (1) {
//         if (redirectCount > MAX_REDIRECTS) {
//             fprintf(stderr, "Too many redirects.\n");
//             break;
//         }
//
//         // ----------------------------------------------------------------
//         // 1. Parse the current URL (extract host, port, path)
//         // ----------------------------------------------------------------
//         char host[256]   = {0};
//         char path[1024]  = {0};
//         int  port        = 80; // default if none in URL
//
//         if (parseURL(currentURL, host, &port, path) < 0) {
//             // parseURL prints usage or error if needed
//             // In practice, you'd handle error or exit.
//             // We'll just break here:
//             break;
//         }
//
//         // ----------------------------------------------------------------
//         // 2. Construct the HTTP request
//         // ----------------------------------------------------------------
//         char request[2048];
//         memset(request, 0, sizeof(request));
//         if (buildHTTPRequest(host, path,
//                              cmd.numParams, cmd.params,
//                              request, sizeof(request)) < 0)
//         {
//             fprintf(stderr, "Error building HTTP request.\n");
//             break;
//         }
//
//         // Print the request before sending (as per requirement)
//         printf("HTTP request =\n%s\nLEN = %ld\n", request, strlen(request));
//
//         // ----------------------------------------------------------------
//         // 3. Connect to the server
//         // ----------------------------------------------------------------
//         int sockfd = connectToServer(host, port);
//         if (sockfd < 0) {
//             // connectToServer prints its own error
//             break;
//         }
//
//         // ----------------------------------------------------------------
//         // 4. Send the HTTP request
//         // ----------------------------------------------------------------
//         if (sendAll(sockfd, request, strlen(request)) < 0) {
//             perror("send");
//             close(sockfd);
//             break;
//         }
//
//         // ----------------------------------------------------------------
//         // 5. Receive HTTP response
//         // ----------------------------------------------------------------
//         char *response = NULL;
//         int responseSize = 0;
//         if (receiveResponse(sockfd, &response, &responseSize) < 0) {
//             perror("recv");
//             close(sockfd);
//             if (response) free(response);
//             break;
//         }
//
//         // Close the connection after receiving the response (per requirement)
//         close(sockfd);
//
//         // ----------------------------------------------------------------
//         // 6. Display the response on the screen
//         // ----------------------------------------------------------------
//         if (response) {
//             // Print raw response:
//             fwrite(response, 1, responseSize, stdout);
//
//             // Print the "Total received response bytes"
//             printf("\n   Total received response bytes: %d\n", responseSize);
//         }
//
//         // ----------------------------------------------------------------
//         // 7. Handle 3XX response with "Location" header
//         // ----------------------------------------------------------------
//         int statusCode = extractStatusCode(response ? response : "");
//         if (statusCode >= 300 && statusCode < 400) {
//             char locationURL[1024] = {0};
//             if (extractLocationHeader(response ? response : "",
//                                       locationURL, sizeof(locationURL)) == 0)
//             {
//                 // We have a "Location: ..." header
//                 // Check if it's an HTTP URL
//                 if (isHTTP(locationURL)) {
//                     // free old response
//                     free(response);
//                     response = NULL;
//
//                     // new request with this location
//                     strncpy(currentURL, locationURL, sizeof(currentURL) - 1);
//                     currentURL[sizeof(currentURL) - 1] = '\0';
//
//                     redirectCount++;
//                     continue; // re-enter the loop with the new URL
//                 }
//             }
//         }
//
//         // If not a 3xx or not an HTTP redirect, we're done
//         if (response) free(response);
//         break;
//     }
//
//     // Cleanup: free any allocated parameters
//     if (cmd.params) {
//         for (int i = 0; i < cmd.numParams; i++) {
//             free(cmd.params[i]);
//         }
//         free(cmd.params);
//     }
//
//     return 0;
// }
//
// /**
//  * parseArguments:
//  *   We scan the arguments. The possible usage is:
//  *   client [-r n <pr1=value1 pr2=value2 …>] <URL>
//  *
//  *   - The URL can appear before or after the -r block.
//  *   - If we find a "-", it must be "-r" or it's an error.
//  *   - After "-r", we must see a positive integer n (< 65536).
//  *   - Then we must read exactly n parameters of the form name=value.
//  *   - Anything else is considered the URL.
//  *
//  *   Return 0 if OK, otherwise print usage and exit.
//  */
// int parseArguments(int argc, char *argv[], CmdArgs *cmd) {
//     // Initialize struct to default (no URL, no params)
//     cmd->url       = NULL;
//     cmd->numParams = 0;
//     cmd->params    = NULL;
//
//     int i = 1;
//     while (i < argc) {
//         if (argv[i][0] == '-') {
//             // Must be '-r' or error
//             if (strcmp(argv[i], "-r") != 0) {
//                 fprintf(stderr, "Unknown flag: %s\n", argv[i]);
//                 printUsageAndExit();
//             }
//
//             // Now parse the number n
//             i++;
//             if (i >= argc) {
//                 fprintf(stderr, "Has to be a number after -r\n");
//                 printUsageAndExit();
//             }
//             if (!isPositiveNumberUnder16Bit(argv[i])) {
//                 fprintf(stderr, "Has to be a number after -r\n");
//                 printUsageAndExit();
//             }
//             int n = atoi(argv[i]);
//             i++;
//
//             // Now read exactly n parameters
//             cmd->params = (char **)malloc(sizeof(char*) * n);
//             if (!cmd->params) {
//                 perror("malloc");
//                 exit(1);
//             }
//
//             for (int j = 0; j < n; j++) {
//                 if (i >= argc) {
//                     fprintf(stderr, "Too few parameters after -r\n");
//                     printUsageAndExit();
//                 }
//                 // Must be of the form name=value
//                 char *eq = strchr(argv[i], '=');
//                 if (!eq) {
//                     fprintf(stderr, "Parameter '%s' is not in the form name=value\n", argv[i]);
//                     printUsageAndExit();
//                 }
//                 cmd->params[j] = strdup(argv[i]);
//                 i++;
//             }
//
//             cmd->numParams = n;
//         }
//         else {
//             // This should be the URL
//             // If we already have a URL, that's an error (multiple URLs)
//             if (cmd->url) {
//                 fprintf(stderr, "Multiple URLs or extra arguments provided.\n");
//                 printUsageAndExit();
//             }
//             cmd->url = argv[i];
//             i++;
//         }
//     }
//
//     // Validate that we have at least a URL
//     if (!cmd->url) {
//         fprintf(stderr, "No URL provided.\n");
//         printUsageAndExit();
//     }
//
//     return 0;
// }
//
// /*
//  * printUsageAndExit
//  */
// void printUsageAndExit()
// {
//     fprintf(stderr, "Usage: client [-r n < pr1=value1 pr2=value2 …>] <URL>\n");
//     exit(1);
// }
//
// /*
//  * Check if str is a positive decimal number less than 65536
//  */
// int isPositiveNumberUnder16Bit(const char *str)
// {
//     if (!str || !*str) return 0;
//     for (size_t i = 0; i < strlen(str); i++) {
//         if (!isdigit((unsigned char)str[i])) return 0;
//     }
//     long portLong = strtol(str, NULL, 10);
//     if (portLong <= 0 || portLong >= 65536) {
//         return 0;
//     }
//     return 1;
// }
//
// /*
//  * parseURL:
//  *   Input:  URL of form  http://hostname[:port]/path
//  *   Output: host, port, path
//  *   Return: 0 if OK, -1 if error
//  */
// int parseURL(const char *url,
//              char *host,
//              int  *port,
//              char *path)
// {
//     // Check prefix "http://"
//     const char *prefix = "http://";
//     size_t prefixLen = strlen(prefix);
//
//     if (strncmp(url, prefix, prefixLen) != 0) {
//         fprintf(stderr, "URL must begin with http://\n");
//         printUsageAndExit();
//     }
//
//     // Start after "http://"
//     const char *p = url + prefixLen;
//
//     // Extract host until ':' or '/' or end
//     const char *hostStart = p;
//     while (*p && *p != ':' && *p != '/') {
//         p++;
//     }
//
//     // Now p is at ':', '/', or '\0'
//     int lenHost = p - hostStart;
//     if (lenHost <= 0 || lenHost >= 256) {
//         fprintf(stderr, "Invalid host in URL.\n");
//         printUsageAndExit();
//     }
//     strncpy(host, hostStart, lenHost);
//     host[lenHost] = '\0';
//
//     // default path is "/"
//     strcpy(path, "/");
//
//     // If we see a colon, parse a port
//     if (*p == ':') {
//         p++;
//         // parse port
//         char portBuf[10];
//         int i = 0;
//         while (*p && *p != '/' && i < 9) {
//             if (!isdigit((unsigned char)*p)) {
//                 fprintf(stderr, "Port must be a valid positive integer < 65536\n");
//                 printUsageAndExit();
//             }
//             portBuf[i++] = *p;
//             p++;
//         }
//         portBuf[i] = '\0';
//         if (!isPositiveNumberUnder16Bit(portBuf)) {
//             fprintf(stderr, "Port out of range < 65536\n");
//             printUsageAndExit();
//         }
//         *port = atoi(portBuf);
//     }
//
//     // If we see '/', parse path
//     if (*p == '/') {
//         // copy the rest to path
//         strncpy(path, p, 1023);
//         path[1023] = '\0';
//     }
//
//     return 0;
// }
//
// /*
//  * buildHTTPRequest:
//  *   Build the GET request string:
//  *     "GET path[?param1=value1&param2=value2...] HTTP/1.1\r\n"
//  *     "Host: hostname\r\n"
//  *     "\r\n"
//  */
// int buildHTTPRequest(const char *host,
//                      const char *path,
//                      int numParams,
//                      char **params,
//                      char *requestBuffer,
//                      size_t requestBufferSize)
// {
//     // We build the path with the query string if needed
//     char finalPath[1200];
//     strncpy(finalPath, path, sizeof(finalPath) - 1);
//     finalPath[sizeof(finalPath) - 1] = '\0';
//
//     // If we have parameters, append ?p1=v1&p2=v2...
//     if (numParams > 0) {
//         // Check if path already has '?'; instructions imply normal usage won't have it,
//         // but let's handle it gracefully.
//         if (strchr(finalPath, '?') == NULL) {
//             strncat(finalPath, "?", sizeof(finalPath) - strlen(finalPath) - 1);
//         } else {
//             strncat(finalPath, "&", sizeof(finalPath) - strlen(finalPath) - 1);
//         }
//
//         // Build the param string
//         for (int i = 0; i < numParams; i++) {
//             if (i > 0) {
//                 strncat(finalPath, "&", sizeof(finalPath) - strlen(finalPath) - 1);
//             }
//             strncat(finalPath, params[i], sizeof(finalPath) - strlen(finalPath) - 1);
//         }
//     }
//
//     // Start building the request
//     // Example:
//     // GET /somePath?param=val HTTP/1.1\r\n
//     // Host: host\r\n
//     // \r\n
//     int ret = snprintf(requestBuffer,
//                        requestBufferSize,
//                        "GET %s HTTP/1.1\r\nHost: %s\r\n\r\n",
//                        finalPath,
//                        host);
//
//     if (ret < 0 || (size_t)ret >= requestBufferSize) {
//         // truncated
//         return -1;
//     }
//     return 0;
// }
//
// /*
//  * connectToServer:
//  *   Return socket FD or -1 if error
//  */
// int connectToServer(const char *hostname, int port)
// {
//     struct hostent *server = gethostbyname(hostname);
//     if (!server) {
//         herror("gethostbyname");
//         return -1;
//     }
//
//     int sockfd = socket(AF_INET, SOCK_STREAM, 0);
//     if (sockfd < 0) {
//         perror("socket");
//         return -1;
//     }
//
//     struct sockaddr_in serv_addr;
//     memset(&serv_addr, 0, sizeof(serv_addr));
//     serv_addr.sin_family = AF_INET;
//     serv_addr.sin_port   = htons(port);
//
//     // copy the address from server->h_addr_list[0]
//     memcpy(&serv_addr.sin_addr.s_addr, server->h_addr_list[0], server->h_length);
//
//     if (connect(sockfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
//         perror("connect");
//         close(sockfd);
//         return -1;
//     }
//
//     return sockfd;
// }
//
// /*
//  * sendAll:
//  *   Repeatedly send until all bytes are sent or error.
//  */
// int sendAll(int sockfd, const char *buf, size_t len)
// {
//     size_t totalSent = 0;
//     while (totalSent < len) {
//         ssize_t n = send(sockfd, buf + totalSent, len - totalSent, 0);
//         if (n < 0) {
//             return -1; // send error
//         }
//         totalSent += n;
//     }
//     return 0;
// }
//
// /*
//  * receiveResponse:
//  *   We read the server's response until it closes the connection or we hit an error.
//  *   We store the entire response into a single buffer 'response' (dynamically allocated).
//  *   The caller must free it.
//  */
// int receiveResponse(int sockfd, char **response, int *responseSize)
// {
//     *response = NULL;
//     *responseSize = 0;
//     char buffer[MAX_BUFFER_SIZE];
//
//     // We'll gather data into a dynamic buffer
//     size_t capacity = 0;
//     size_t size = 0;
//
//     while (1) {
//         ssize_t bytesRead = recv(sockfd, buffer, sizeof(buffer), 0);
//         if (bytesRead < 0) {
//             return -1; // error
//         }
//         if (bytesRead == 0) {
//             // server closed connection
//             break;
//         }
//
//         // Expand *response if needed
//         if (size + bytesRead > capacity) {
//             size_t newCap = (capacity == 0) ? (bytesRead + 1) : (capacity * 2);
//             if (newCap < size + bytesRead) {
//                 newCap = size + bytesRead;
//             }
//             char *tmp = realloc(*response, newCap);
//             if (!tmp) {
//                 perror("realloc");
//                 free(*response);
//                 return -1;
//             }
//             *response = tmp;
//             capacity = newCap;
//         }
//
//         // Copy data
//         memcpy((*response) + size, buffer, bytesRead);
//         size += bytesRead;
//     }
//
//     if (*response) {
//         (*response)[size] = '\0'; // null-terminate for easier parsing
//     }
//     *responseSize = size;
//     return 0;
// }
//
// /*
//  * extractStatusCode:
//  *   From the first line of the response, extract the numeric status code (e.g., 200, 404, 301, etc.)
//  *   Return code or -1 if not found/invalid.
//  */
// int extractStatusCode(const char *response)
// {
//     // Typical HTTP response first line: "HTTP/1.1 200 OK"
//     // Let's parse the first line until we get the code
//     if (!response || !*response) {
//         return -1;
//     }
//
//     const char *p = strstr(response, "HTTP/");
//     if (!p) return -1;
//
//     // move p to the next space after "HTTP/1.x"
//     p = strchr(p, ' ');
//     if (!p) return -1;
//
//     // skip spaces
//     while (*p == ' ') p++;
//
//     // now p should point to the status code
//     char codeBuf[4] = {0};
//     strncpy(codeBuf, p, 3); // read 3 digits
//     codeBuf[3] = '\0';
//
//     int code = atoi(codeBuf);
//     return code;
// }
//
// /*
//  * extractLocationHeader:
//  *   If the response contains a "Location: ..." header (case-insensitive),
//  *   extract its value. Return 0 if found, -1 if not found.
//  */
// int extractLocationHeader(const char *response, char *locationURL, size_t maxLen)
// {
//     if (!response) return -1;
//
//     // We'll do a naive approach: find "location:" (case-insensitive).
//     // Then parse until the next CRLF.
//     // For a more robust approach, you'd parse line-by-line.
//     const char *locationPtr = NULL;
//
//     // "strcasestr" is GNU-specific; if not available, do a manual search
//     // For simplicity here, let's use strcasestr if available
//     // If not available in your environment, you'd implement a custom case-insensitive search.
//     // e.g.:
//     //   while ((temp = myCaseInsensitiveSearch(temp, "location:")) != NULL) ...
//     // We'll assume strcasestr is okay for demonstration:
//
//     {
//         const char *temp = response;
//         // We'll do a quick approach: repeatedly search for 'L' or 'l', then compare ignoring case.
//         // Or if your environment has strcasestr, do:
//         extern char *strcasestr(const char *haystack, const char *needle);
//         while ((temp = strcasestr(temp, "location:")) != NULL) {
//             locationPtr = temp;
//             break;
//         }
//     }
//
//     if (!locationPtr) {
//         return -1; // not found
//     }
//
//     // Move pointer after "location:"
//     locationPtr = strchr(locationPtr, ':');
//     if (!locationPtr) return -1;
//     locationPtr++; // skip the colon
//
//     // skip spaces
//     while (*locationPtr == ' ' || *locationPtr == '\t') {
//         locationPtr++;
//     }
//
//     // Now read until we hit CR or LF or end
//     int i = 0;
//     while (*locationPtr && *locationPtr != '\r' && *locationPtr != '\n' && i < (int)maxLen - 1) {
//         locationURL[i++] = *locationPtr;
//         locationPtr++;
//     }
//     locationURL[i] = '\0';
//
//     return 0;
// }
//
// /*
//  * isHTTP:
//  *   returns 1 if the string starts with "http://", else 0
//  */
// int isHTTP(const char *maybeURL)
// {
//     if (!maybeURL) return 0;
//     if (strncmp(maybeURL, "http://", 7) == 0) {
//         return 1;
//     }
//     return 0;
// }
