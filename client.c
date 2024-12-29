/************************************************************
 * EX2 – HTTP client
 *
 * Implements a simple HTTP/1.1 client supporting GET requests,
 * optional parameters appended as a query string, and automatic
 * handling of 3XX (HTTP) redirects up to 10 times.
 *
 * Usage:
 *   client [-r n <pr1=value1 pr2=value2 …>] <URL>
 *
 * Example:
 *   ./client -r 2 param1=val1 param2=val2 http://example.com/path
 *
 ************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>     // for gethostbyname, herror
#include <ctype.h>     // for isdigit
#include <errno.h>

/* We fix these buffer sizes for this assignment. */
#define REQUEST_BUFFER_SIZE 2048
#define LOCATION_URL_SIZE   1024
#define MAX_BUFFER_SIZE     8192

/*
 * Data structure to hold command-line results
 */
typedef struct {
    char *url;         // URL must start with http://
    int  numParams;    // number of name=value pairs
    char **params;     // array of "name=value" strings
} CmdArgs;

/*
 * Function Prototypes
 */
static void printUsageAndExit();
static int  isPositiveNumberUnder16Bit(const char *str);
static void parseArguments(int argc, char *argv[], CmdArgs *cmd);
static void parseURL(const char *url, char *host, int *port, char *path);
static int  buildHTTPRequest(const char *host,
                             const char *path,
                             int numParams,
                             char **params,
                             char *requestBuffer);
static int  connectToServer(const char *hostname, int port);
static int  sendAll(int sockfd, const char *buf, size_t len);
static int  receiveResponse(int sockfd, char **response, int *responseSize);
static int  extractStatusCode(const char *response);
static int  extractLocationHeader(const char *response, char *locationURL);
static int  isHTTP(const char *maybeURL);

/*
 * main()
 */
int main(int argc, char *argv[])
{
    CmdArgs cmd;
    parseArguments(argc, argv, &cmd);  // Exits on error

    /* Move MAX_REDIRECTS to this inner scope. */
    int redirectCount = 0;

    char currentURL[1024] = {0};
    strncpy(currentURL, cmd.url, sizeof(currentURL) - 1);

    while (1) {
        const int MAX_REDIRECTS = 10;
        if (redirectCount > MAX_REDIRECTS) {
            fprintf(stderr, "Too many redirects.\n\n");
            exit(1);
        }

        char host[256]  = {0};
        char path[1024] = {0};
        int  port       = 80;

        parseURL(currentURL, host, &port, path);  // Exits on error

        /* Build the HTTP request string. */
        char request[REQUEST_BUFFER_SIZE] = {0};
        if (buildHTTPRequest(host, path, cmd.numParams, cmd.params, request) < 0) {
            fprintf(stderr, "Error building HTTP request.\n\n");
            exit(1);
        }

        /* Print the request (per instructions). */
        printf("HTTP request =\n%s\nLEN = %d\n", request, (int)strlen(request));

        /* Connect to the server. */
        int sockfd = connectToServer(host, port);
        if (sockfd < 0) {
            exit(1); /* connectToServer prints its own error. */
        }

        /* Send the request. */
        if (sendAll(sockfd, request, strlen(request)) < 0) {
            perror("send");
            close(sockfd);
            exit(1);
        }

        /* Receive the response. */
        char *response = NULL;
        int responseSize = 0;
        if (receiveResponse(sockfd, &response, &responseSize) < 0) {
            perror("recv");
            close(sockfd);
            free(response);
            exit(1);
        }
        close(sockfd);

        /* Print the response. */
        if (response) {
            fwrite(response, 1, responseSize, stdout);
            printf("\n Total received response bytes: %d\n", responseSize);
        }

        /* Check if it's a 3XX redirect with Location header. */
        int statusCode = extractStatusCode(response ? response : "");
        if (statusCode >= 300 && statusCode < 400) {
            char locationURL[LOCATION_URL_SIZE] = {0};
            if (extractLocationHeader(response ? response : "", locationURL) == 0) {
                if (isHTTP(locationURL)) {
                    free(response);
                    response = NULL;
                    strncpy(currentURL, locationURL, sizeof(currentURL) - 1);
                    redirectCount++;
                    continue;
                }
            }
        }

        if (response) {
            free(response);
        }
        break;
    }

    /* Cleanup. */
    if (cmd.params) {
        for (int i = 0; i < cmd.numParams; i++) {
            free(cmd.params[i]);
        }
        free(cmd.params);
    }

    return 0;
}

/*
 * printUsageAndExit:
 *   Print usage line, then exit(1).
 */
static void printUsageAndExit()
{
    fprintf(stderr, "Usage: client [-r n <pr1=value1 pr2=value2 …>] <URL>\n\n");
    exit(1);
}

/*
 * Check if str is a positive integer < 65536
 * Use strtol so we can detect errors.
 */
static int isPositiveNumberUnder16Bit(const char *str)
{
    if (!str || !*str) return 0;

    char *endptr = NULL;
    errno = 0;
    long val = strtol(str, &endptr, 10);
    // Check for non-digit characters, range issues, or conversion errors
    if (*endptr != '\0' || errno == ERANGE || val <= 0 || val >= 65536) {
        return 0;
    }
    return 1;
}

/*
 * parseArguments:
 *   We look for an optional "-r n <params...>" block (exactly one),
 *   then a single <URL> somewhere in the arguments.
 *   If any part is malformed, print usage + newline and exit.
 *
 *   On success, fill cmd.url, cmd.numParams, cmd.params.
 *   This function calls exit(1) on error, so it never returns on usage failure.
 */
static void parseArguments(int argc, char *argv[], CmdArgs *cmd)
{
    cmd->url       = NULL;
    cmd->numParams = 0;
    cmd->params    = NULL;

    int i = 1;
    while (i < argc) {
        if (argv[i][0] == '-') {
            /* Must be '-r' or usage error. */
            if (strcmp(argv[i], "-r") != 0) {
                fprintf(stderr, "Unknown flag: %s\n\n", argv[i]);
                printUsageAndExit();
            }
            i++;
            if (i >= argc) {
                fprintf(stderr, "Has to be a number after -r\n\n");
                printUsageAndExit();
            }
            if (!isPositiveNumberUnder16Bit(argv[i])) {
                fprintf(stderr, "Has to be a number after -r\n\n");
                printUsageAndExit();
            }

            char *endptr = NULL;
            const long n = strtol(argv[i], &endptr, 10);
            i++;

            cmd->params = (char **)malloc(sizeof(char*) * n);
            if (!cmd->params) {
                perror("malloc");
                exit(1);
            }
            for (int j = 0; j < (int)n; j++) {
                if (i >= argc) {
                    fprintf(stderr, "Too few parameters after -r\n\n");
                    printUsageAndExit();
                }
                char *eq = strchr(argv[i], '=');
                if (!eq) {
                    fprintf(stderr, "Parameter '%s' is not in the form name=value\n\n", argv[i]);
                    printUsageAndExit();
                }
                cmd->params[j] = strdup(argv[i]);
                i++;
            }
            cmd->numParams = (int)n;
        }
        else {
            /* This should be the URL. */
            if (cmd->url) {
                // Already have a URL => extra arguments => usage error
                fprintf(stderr, "Multiple URLs or extra arguments provided.\n\n");
                printUsageAndExit();
            }
            cmd->url = argv[i];
            i++;
        }
    }

    /* Must have at least a URL. */
    if (!cmd->url) {
        fprintf(stderr, "No URL provided.\n\n");
        printUsageAndExit();
    }
}

/*
 * parseURL:
 *   url format: http://hostname[:port]/path
 *
 *   - Must begin with "http://"
 *   - If port is given, must be < 65536
 *   - If no path, default to "/"
 *   - Calls exit(1) on error.
 */
static void parseURL(const char *url, char *host, int *port, char *path)
{
    const char *prefix = "http://";
    size_t prefixLen = strlen(prefix);

    if (strncmp(url, prefix, prefixLen) != 0) {
        fprintf(stderr, "URL must begin with http://\n\n");
        exit(1);
    }

    const char *p = url + prefixLen;

    // Extract hostname until ':' or '/' or end
    const char *hostStart = p;
    while (*p && *p != ':' && *p != '/') {
        p++;
    }
    int lenHost = (int)(p - hostStart);
    if (lenHost <= 0 || lenHost >= 256) {
        fprintf(stderr, "Invalid host in URL.\n\n");
        exit(1);
    }
    strncpy(host, hostStart, (size_t)lenHost);
    host[lenHost] = '\0';

    /* Default path. */
    path[0] = '/';
    path[1] = '\0';

    // If we see ':', parse port
    if (*p == ':') {
        p++;
        char portBuf[10];
        int idx = 0;
        while (*p && *p != '/' && idx < 9) {
            if (!isdigit((unsigned char)*p)) {
                fprintf(stderr, "Port must be a valid positive integer < 65536\n\n");
                exit(1);
            }
            portBuf[idx++] = *p;
            p++;
        }
        portBuf[idx] = '\0';
        if (!isPositiveNumberUnder16Bit(portBuf)) {
            fprintf(stderr, "Port out of range < 65536\n\n");
            exit(1);
        }
        char *endptr = NULL;
        long val = strtol(portBuf, &endptr, 10);
        *port = (int)val;
    }

    // If we see '/', parse path
    if (*p == '/') {
        strncpy(path, p, 1023);
        path[1023] = '\0';
    }
}

/*
 * buildHTTPRequest:
 *   Build the GET request string:
 *     "GET path[?param1=value1&param2=value2...] HTTP/1.1\r\n"
 *     "Host: hostname\r\n"
 *     "\r\n"
 *   Return 0 if OK, -1 if error.
 */
static int buildHTTPRequest(const char *host,
                            const char *path,
                            int numParams,
                            char **params,
                            char *requestBuffer)
{
    /* finalPath for path + optional query. */
    char finalPath[1200] = {0};
    strncpy(finalPath, path, sizeof(finalPath) - 1);

    /* If we have parameters, append ?p1=v1&p2=v2... */
    if (numParams > 0) {
        if (!strchr(finalPath, '?')) {
            strncat(finalPath, "?", sizeof(finalPath) - strlen(finalPath) - 1);
        } else {
            strncat(finalPath, "&", sizeof(finalPath) - strlen(finalPath) - 1);
        }
        for (int i = 0; i < numParams; i++) {
            if (i > 0) {
                strncat(finalPath, "&", sizeof(finalPath) - strlen(finalPath) - 1);
            }
            strncat(finalPath, params[i], sizeof(finalPath) - strlen(finalPath) - 1);
        }
    }

    int ret = snprintf(requestBuffer,
                       REQUEST_BUFFER_SIZE,
                       "GET %s HTTP/1.1\r\nHost: %s\r\n\r\n",
                       finalPath, host);

    if (ret < 0 || ret >= REQUEST_BUFFER_SIZE) {
        return -1; // truncated or error
    }
    return 0;
}

/*
 * connectToServer:
 *   - Resolve hostname via gethostbyname (IPv4).
 *   - Open socket(AF_INET, SOCK_STREAM).
 *   - connect().
 *   Return the sockfd on success, or -1 on error (with perror/herror).
 */
static int connectToServer(const char *hostname, int port)
{
    struct hostent *server = gethostbyname(hostname);
    if (!server) {
        herror("gethostbyname");
        return -1;
    }

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("socket");
        return -1;
    }

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port   = htons(port);

    memcpy(&serv_addr.sin_addr.s_addr, server->h_addr_list[0], (size_t)server->h_length);

    if (connect(sockfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("connect");
        close(sockfd);
        return -1;
    }

    return sockfd;
}

/*
 * sendAll:
 *   Repeatedly send until all bytes are sent or error.
 *   Return 0 if OK, -1 on error.
 */
static int sendAll(int sockfd, const char *buf, size_t len)
{
    size_t totalSent = 0;
    while (totalSent < len) {
        ssize_t n = send(sockfd, buf + totalSent, len - totalSent, 0);
        if (n < 0) {
            return -1;
        }
        totalSent += (size_t)n;
    }
    return 0;
}

/*
 * receiveResponse:
 *   Read until the server closes the connection.
 *   Dynamically allocate a buffer to store the entire response.
 *   *response must be freed by the caller.
 *   Return 0 if success, -1 if error.
 */
static int receiveResponse(int sockfd, char **response, int *responseSize)
{
    *response = NULL;
    *responseSize = 0;

    size_t capacity = 0;
    size_t size = 0;

    for (;;) {
        char buffer[MAX_BUFFER_SIZE];
        ssize_t bytesRead = recv(sockfd, buffer, sizeof(buffer), 0);
        if (bytesRead < 0) {
            return -1; // error
        }
        if (bytesRead == 0) {
            /* connection closed by server */
            break;
        }

        if (size + (size_t)bytesRead >= capacity) {
            size_t newCap = (capacity == 0) ? (size_t)bytesRead + 1 : capacity * 2;
            if (newCap < size + (size_t)bytesRead) {
                newCap = size + (size_t)bytesRead;
            }
            char *tmp = realloc(*response, newCap);
            if (!tmp) {
                perror("realloc");
                free(*response);
                return -1;
            }
            *response = tmp;
            capacity = newCap;
        }

        memcpy((*response) + size, buffer, (size_t)bytesRead);
        size += (size_t)bytesRead;
    }

    if (*response) {
        (*response)[size] = '\0';
    }
    *responseSize = (int)size;
    return 0;
}

/*
 * extractStatusCode:
 *   From the first line of `response`, parse the numeric status code (e.g., "200", "301", etc.).
 *   Return the code or -1 if not found.
 */
static int extractStatusCode(const char *response)
{
    if (!response || !*response) {
        return -1;
    }
    // Typical first line: "HTTP/1.1 200 OK"
    const char *p = strstr(response, "HTTP/");
    if (!p) return -1;

    // Move to the next space after "HTTP/1.x"
    p = strchr(p, ' ');
    if (!p) return -1;

    // skip spaces
    while (*p == ' ') {
        p++;
    }

    // now p should point to the status code
    char codeBuf[4] = {0};
    strncpy(codeBuf, p, 3);
    codeBuf[3] = '\0';

    // Use strtol for safety
    char *endptr = NULL;
    errno = 0;
    long val = strtol(codeBuf, &endptr, 10);
    if (*endptr != '\0' || errno == ERANGE) {
        return -1;
    }
    return (int)val;
}

/*
 * extractLocationHeader:
 *   Look for "location:" (case-insensitive).
 *   If found, copy up to CR or LF into locationURL.
 *   Return 0 if found, -1 if not.
 */
static int extractLocationHeader(const char *response, char *locationURL)
{
    if (!response) return -1;

    /* We'll do a naive case-insensitive scan for "location:". */
    const char *needle = "location:";
    size_t needleLen = strlen(needle);

    for (const char *p = response; *p; p++) {
        /* Compare ignoring case. */
        if (strncasecmp(p, needle, needleLen) == 0) {
            /* Move past "location:" */
            p += needleLen;
            /* Skip spaces/tabs. */
            while (*p == ' ' || *p == '\t') {
                p++;
            }

            /* Copy until CR or LF. */
            int i = 0;
            while (*p && *p != '\r' && *p != '\n' && i < (LOCATION_URL_SIZE - 1)) {
                locationURL[i++] = *p++;
            }
            locationURL[i] = '\0';
            return 0;
        }
    }
    return -1;
}

/*
 * isHTTP:
 *   Returns 1 if maybeURL starts with "http://", else 0.
 */
static int isHTTP(const char *maybeURL)
{
    if (!maybeURL) return 0;
    return (strncmp(maybeURL, "http://", 7) == 0) ? 1 : 0;
}
