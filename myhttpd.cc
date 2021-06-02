#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <string>
#include <pthread.h>
#include <sys/wait.h>
#include <errno.h>
#include <dirent.h>
#include <dlfcn.h>
#include <link.h>
#include <iostream>
#include <iomanip>
#include <vector>
#include <numeric>
#include <chrono>


struct arg_struct {
  int masterSocket;
  sockaddr_in *sockInfo;
  int *alen;
}args;

struct file_struct {
  char *size;
  char *name;
  char *path;
  char *date;
  bool dir;
};

typedef void (*httprunfunc) (int ssock, const char *querystring);
pthread_mutex_t mutex;

const char *usage = "\nmyhttpd [-f|-t|-p] [<port>]\n";
const char *realm = "The_Great_Realm_Of_CS252";
const char *auth = "eWVsc2hhbGw6aW15b3RhMDI=";
const int maxDocRequest = 1024;
int mode = -1;
int QueueLength = 5;
const int maxFiles = 50;
std::string lastPath = "";
bool alphaBool = false;
bool sizeBool = false;
bool dateBool = false;
bool descBool = false;
int sortMode = 0;
int noRequests = 0;
double minService = 2147483647;
double maxService = 0;
int port = 0;

std::chrono::high_resolution_clock::time_point start;
std::chrono::high_resolution_clock::time_point start1;
std::chrono::high_resolution_clock::time_point end1;

void processHTTPRequest(int fd);
char * endsWith(char *path);
void fileNotFound(int fd, char *type, char *response_string);
void badRequest(int fd, char *response_string);
void fileNotFound(int fd, char *type, char *response_string);
void forbidden(int fd, char *response_string);
void unauthorized(int fd, char *response_string);
void createThreadForEachRequest(int masterSocket, sockaddr_in *sockInfo, int *alen);
void poolOfThreads(int masterSocket, sockaddr_in *sockInfo, int *alen);
void loopThread(arg_struct *args);
void sendDir(int fd, DIR *dir, char *path);
char *itoa(int value, char *result, int base);
struct file_struct *getInfo(struct dirent *ent, char *path);
int fsize(FILE *fp);
void setMode(char *mode);
void cgiBin(int fd, char *response_string, std::string fullPath);
char *dateFormat(char *date);
int compareDate(file_struct *one, file_struct *two);
int compareSize(file_struct *one, file_struct *two);
void statsPage(int fd);
void logsPage(int fd);
void writeLog(char *log);
char *timeFormatted(int seconds);

void sortAlpha(struct file_struct **files, int n);
void sortAlphaReverse(struct file_struct **files, int n);
void sortDate(struct file_struct **files, int n);
void sortDateReverse(struct file_struct **files, int n);
void sortSize(struct file_struct **files, int n);
void sortSizeReverse(struct file_struct **files, int n);

extern "C" void brokenHandler(int sig) {
  return;
}

extern "C" void zombieKiller(int sig) {
  while(waitpid(-1, NULL, WNOHANG) > 0) {
    continue;
  }
}

int main(int argc, char **argv) {
  if (argc < 2 || argc > 3) {
    fprintf(stderr, "%s", usage);
    exit(-1);
  }

  start = std::chrono::system_clock::now();
  pthread_mutex_init(&mutex, NULL);

  if (argc == 3) {
    if(strcmp(argv[1], "-f") == 0) {
      mode = 0;
    } else if(strcmp(argv[1], "-t") == 0) {
      mode = 1;
    } else if(strcmp(argv[1], "-p") == 0) {
      mode = 2;
    } else {
      fprintf(stderr, "%s", usage);
      exit(-1);
    }
    port = atoi(argv[2]);
  } else {
    port = atoi(argv[1]);
  }

  printf("%d\n", port);

  struct sockaddr_in serverIPAddress;
  memset(&serverIPAddress, 0, sizeof(serverIPAddress));
  serverIPAddress.sin_family = AF_INET;
  serverIPAddress.sin_addr.s_addr = INADDR_ANY;
  serverIPAddress.sin_port = htons((u_short) port);

  int serverSocket = socket(PF_INET, SOCK_STREAM, 0);
  if(serverSocket < 0) {
    perror("socket");
    exit(-1);
  }

  int optval = 1;
  int err = setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, (char *) &optval,
  sizeof(int));

  int error = bind(serverSocket, (struct sockaddr *)&serverIPAddress,
  sizeof(serverIPAddress));

  if(error) {
    perror("bind");
    exit(-1);
  }

  error = listen(serverSocket, QueueLength);
  if(error) {
    perror("Listen");
    exit(-1);
  }

  struct sigaction broken;
  broken.sa_handler = brokenHandler;
  sigemptyset(&broken.sa_mask);
  broken.sa_flags = SA_RESTART;

  if(sigaction(SIGPIPE, &broken, NULL)) {
    perror("sigpipe");
    exit(2);
  }

  struct sigaction zombie;
  zombie.sa_handler = zombieKiller;
  sigemptyset(&zombie.sa_mask);
  zombie.sa_flags = SA_RESTART;

  if(sigaction(SIGCHLD, &zombie, NULL)) {
    perror("sigaaction child");
    exit(2);
  }

  while(1) {
    struct sockaddr_in clientIPAddress;
    int alen = sizeof(clientIPAddress);
    int clientSocket;

    if(clientSocket < 0) {
      continue;
    }

    start1 = std::chrono::system_clock::now();

    if(mode == 0) {
      clientSocket =  accept(serverSocket, (struct sockaddr *) &clientIPAddress,
      (socklen_t *)&alen);
      int ret = fork();
      if (ret == 0) {
        processHTTPRequest(clientSocket);
        exit(0);
      }
      close(clientSocket);
      end1 = std::chrono::system_clock::now();
      std::chrono::duration<double> diff = end1-start1;

      double dur = diff.count();
      if(dur < minService) {
        pthread_mutex_lock(&mutex);
        minService = dur;
        pthread_mutex_unlock(&mutex);
      }

      if(dur > maxService) {
        pthread_mutex_lock(&mutex);
        maxService = dur;
        pthread_mutex_unlock(&mutex);
      }
    } else if (mode == 1) {
      createThreadForEachRequest(serverSocket, &clientIPAddress, &alen);
    } else if (mode == 2) {
      poolOfThreads(serverSocket, &clientIPAddress, &alen);
    } else if (mode == -1) {
      clientSocket = accept(serverSocket, (struct sockaddr *) &clientIPAddress,
      (socklen_t *)&alen);
      processHTTPRequest(clientSocket);
      close(clientSocket);
      end1 = std::chrono::system_clock::now();
      std::chrono::duration<double> diff = end1-start1;

      double dur = diff.count();
      if(dur < minService) {
        pthread_mutex_lock(&mutex);
        minService = dur;
        pthread_mutex_unlock(&mutex);
      }

      if(dur > maxService) {
        pthread_mutex_lock(&mutex);
        maxService = dur;
        pthread_mutex_unlock(&mutex);
      }
    }
  }
}

void processHTTPRequest(int fd) {
  pthread_mutex_lock(&mutex);
  noRequests++;
  pthread_mutex_unlock(&mutex);

  char *response_string = "HTTP/1.1 400 Bad Request\r\nServer: CS 252 lab5";
  char doc[maxDocRequest];
  char otherInfo[maxDocRequest];
  int docLength = 0;
  int n;

  unsigned char c = 0;

  char temp[maxDocRequest];
  int tempInt = 0;

  while(tempInt < 4 && (n = read(fd, &c, 1)) > 0) {
    temp[tempInt] = c;
    tempInt++;
  }

  temp[tempInt-1] = 0;

  if(strcmp(temp, "GET") != 0) {
    badRequest(fd, response_string);
    if(mode == 1 || mode == 2) {
      close(fd);
    }
    end1 = std::chrono::system_clock::now();
    std::chrono::duration<double> diff = end1-start1;

    double dur = diff.count();
    if(dur < minService) {
      pthread_mutex_lock(&mutex);
      minService = dur;
      pthread_mutex_unlock(&mutex);
    }

    if(dur > maxService) {
      pthread_mutex_lock(&mutex);
      maxService = dur;
      pthread_mutex_unlock(&mutex);
    }
    return;
  }

  while(docLength < maxDocRequest && (n = read(fd, &c, sizeof(c))) > 0) {
    if(c == '\040') {
      break;
    }
    doc[docLength] = c;
    docLength++;
  }

  doc[docLength] = 0;

  tempInt = 0;

  while(tempInt < 8 && (n = read(fd, &c, sizeof(c))) > 0) {
    temp[tempInt] = c;
    tempInt++;
  }

  temp[tempInt] = 0;

  if(strcmp(temp, "HTTP/1.1") != 0) {
    badRequest(fd, response_string);
    if(mode == 1 || mode == 2) {
      close(fd);
    }
    end1 = std::chrono::system_clock::now();
    std::chrono::duration<double> diff = end1-start1;

    double dur = diff.count();
    if(dur < minService) {
      pthread_mutex_lock(&mutex);
      minService = dur;
      pthread_mutex_unlock(&mutex);
    }

    if(dur > maxService) {
      pthread_mutex_lock(&mutex);
      maxService = dur;
      pthread_mutex_unlock(&mutex);
    }

    return;
  }

  unsigned char newChar;
  unsigned char lastChar = 0;
  tempInt = 0;
  int tempInt2 = 0;
  bool tempBool = false;
  otherInfo[0] = 0;
  while((n = read(fd, &newChar, sizeof(newChar))) > 0) {
    if(lastChar == '\015' && newChar == '\012') {
      if(tempInt == 1) {
        break;
      }
      tempInt++;
      tempBool = true;
    }

    if(newChar != '\012' && newChar != '\015' && !tempBool) {
      badRequest(fd, response_string);

      if(mode == 1 || mode == 2) {
        close(fd);
      }
      end1 = std::chrono::system_clock::now();
      std::chrono::duration<double> diff = end1-start1;

      double dur = diff.count();
      if(dur < minService) {
        pthread_mutex_lock(&mutex);
        minService = dur;
        pthread_mutex_unlock(&mutex);
      }

      if(dur > maxService) {
        pthread_mutex_lock(&mutex);
        maxService = dur;
        pthread_mutex_unlock(&mutex);
      }

      return;
    } else if (newChar != '\012' && newChar != '\015' && tempBool) {
      otherInfo[tempInt2] = newChar;
      tempInt2++;
      tempInt = 0;
    }
    lastChar = newChar;
  }

  otherInfo[tempInt2] = 0;

  response_string = "HTTP/1.1 401 Unauthorized\r\nWWW-Authenticate: Basic realm=\"";
  if(otherInfo[0] == 0) {
    unauthorized(fd, response_string);
    if(mode == 1 || mode == 2) {
      close(fd);
    }
    end1 = std::chrono::system_clock::now();
    std::chrono::duration<double> diff = end1-start1;

    double dur = diff.count();
    if(dur < minService) {
      pthread_mutex_lock(&mutex);
      minService = dur;
      pthread_mutex_unlock(&mutex);
    }

    if(dur > maxService) {
      pthread_mutex_lock(&mutex);
      maxService = dur;
      pthread_mutex_unlock(&mutex);
    }


    return;
  } else {
    char *temp2 = strstr(otherInfo, "Authorization: Basic ");
    char *temp3;

    if(temp2 != NULL) {
      temp3 = strchr(temp2, '=');
      temp3++;
      strncpy(temp, temp2, temp3-temp2);
    } else {
      unauthorized(fd, response_string);
      if(mode == 1 || mode == 2) {
        close(fd);
      }
      end1 = std::chrono::system_clock::now();
      std::chrono::duration<double> diff = end1-start1;

      double dur = diff.count();
      if(dur < minService) {
        pthread_mutex_lock(&mutex);
        minService = dur;
        pthread_mutex_unlock(&mutex);
      }

      if(dur > maxService) {
        pthread_mutex_lock(&mutex);
        maxService = dur;
        pthread_mutex_unlock(&mutex);
      }

      return;
    }

    temp[temp3-temp2] = 0;

    temp2 = strrchr(temp, ' ');
    temp2++;

    if(strcmp(temp2, auth) != 0) {
      unauthorized(fd, response_string);
      if(mode == 1 || mode == 2) {
        close(fd);
      }
      end1 = std::chrono::system_clock::now();
      std::chrono::duration<double> diff = end1-start1;

      double dur = diff.count();
      if(dur < minService) {
        pthread_mutex_lock(&mutex);
        minService = dur;
        pthread_mutex_unlock(&mutex);
      }

      if(dur > maxService) {
        pthread_mutex_lock(&mutex);
        maxService = dur;
        pthread_mutex_unlock(&mutex);
      }

      return;
    }
  }

  if( n <= 0) {
    perror("read");
    exit(-1);
  }

  std::string fullPath = "";

  char buf[maxDocRequest];

  std::string rootDir = "";
  rootDir += getcwd(buf, maxDocRequest);
  rootDir += "/http-root-dir";

  fullPath += getcwd(buf, maxDocRequest);

  if(doc[0] == '/' && docLength == 1) {
    fullPath += "/http-root-dir/htdocs/index.html";
  } else {
    char component[maxDocRequest];

    if(doc[0] != '/') {
      if(strchr(&doc[0], '/') == NULL) {
        strncpy(component, doc, strlen(doc));
        component[strlen(doc)] = 0;
      } else {
        strncpy(component, doc, strchr(&doc[0], '/')-doc);
        component[strchr(&doc[0], '/')-doc] = '\0';
      }
    } else {
      if(strchr(&doc[1], '/') == NULL) {
        strncpy(component, doc, strlen(doc));
        component[strlen(doc)] = 0;
      } else {
        strncpy(component, doc, strchr(&doc[1], '/')-doc);
        component[strchr(&doc[1], '/')-doc] = '\0';
      }
    }

    if(strstr(doc, "/u/") != NULL && strlen(doc) < rootDir.length()) {
      response_string = "HTTP/1.1 403 Forbidden\r\nServer: CS 252 lab5";
      forbidden(fd, response_string);
      if(mode == 1 || mode == 2) {
        close(fd);
      }
      end1 = std::chrono::system_clock::now();
      std::chrono::duration<double> diff = end1-start1;

      double dur = diff.count();

      if(dur < minService) {
        pthread_mutex_lock(&mutex);
        minService = dur;
        pthread_mutex_unlock(&mutex);
      }

      if(dur > maxService) {
        pthread_mutex_lock(&mutex);
        maxService = dur;
        pthread_mutex_unlock(&mutex);
      }

      return;
    }

    if(strcmp(component, "/icons") == 0) {
      fullPath += "/http-root-dir";
      fullPath += doc;
      writeLog((char *) strdup(fullPath.c_str()));
    } else if(strcmp(component, "/htdocs") == 0) {
      fullPath += "/http-root-dir";
      fullPath += doc;
      writeLog((char *) strdup(fullPath.c_str()));
    } else if(strcmp(component, "/u") == 0) {
      fullPath += strstr(doc, "/http-root-dir");
      writeLog((char *) strdup(fullPath.c_str()));
    } else if(strstr(component, "/http-root-dir") != NULL) {
      fullPath += doc;
      writeLog((char *) strdup(fullPath.c_str()));
    } else if(strcmp(component, "/cgi-bin") == 0) {
      fullPath += "/http-root-dir";
      fullPath += doc;
      writeLog((char *) strdup(fullPath.c_str()));

      response_string =  "HTTP/1.1 200 Document follows\r\nServer: CS 252 lab5\r\n";
      int ret = fork();
      if(ret == 0) {
        cgiBin(fd, response_string, fullPath);
      }
      close(fd);
      end1 = std::chrono::system_clock::now();
      std::chrono::duration<double> diff = end1-start1;

      double dur = diff.count();
      if(dur < minService) {
        pthread_mutex_lock(&mutex);
        minService = dur;
        pthread_mutex_unlock(&mutex);
      }

      if(dur > maxService) {
        pthread_mutex_lock(&mutex);
        maxService = dur;
        pthread_mutex_unlock(&mutex);
      }

      return;
    } else if(strcmp(component, "/stats") == 0) {
      fullPath += "/http-root-dir";
      fullPath += doc;
      writeLog((char *) strdup(fullPath.c_str()));

      response_string = "HTTP/1.1 200 Document follows\r\nServer: CS 252 lab5\r\nContent-type: ";

      write(fd, response_string, strlen(response_string));
      write(fd, "text/html", strlen("text/html"));
      write(fd, "\r\n\r\n", strlen("\r\n\r\n"));

      statsPage(fd);
      close(fd);
      end1 = std::chrono::system_clock::now();
      std::chrono::duration<double> diff = end1-start1;

      double dur = diff.count();
      if(dur < minService) {
        pthread_mutex_lock(&mutex);
        minService = dur;
        pthread_mutex_unlock(&mutex);
      }

      if(dur > maxService) {
        pthread_mutex_lock(&mutex);
        maxService = dur;
        pthread_mutex_unlock(&mutex);
      }

      return;
    } else if(strcmp(component, "/logs") ==0) {
      fullPath += "/http-root-dir";
      fullPath += doc;
      writeLog((char *) strdup(fullPath.c_str()));

      response_string = "HTTP/1.1 200 Document follows\r\nServer: CS 252 lab5\r\nContent-type: ";

      write(fd, response_string, strlen(response_string));
      write(fd, "text/html", strlen("text/html"));
      write(fd, "\r\n\r\n", strlen("\r\n\r\n"));

      logsPage(fd);
      close(fd);
      end1 = std::chrono::system_clock::now();
      std::chrono::duration<double> diff = end1-start1;

      double dur = diff.count();
      if(dur < minService) {
        pthread_mutex_lock(&mutex);
        minService = dur;
        pthread_mutex_unlock(&mutex);
      }

      if(dur > maxService) {
        pthread_mutex_lock(&mutex);
        maxService = dur;
        pthread_mutex_unlock(&mutex);
      }

      return;
    } else {
      fullPath += "/http-root-dir/htdocs";
      fullPath += doc;
      writeLog((char *) strdup(fullPath.c_str()));
    }


    char *full = strdup(fullPath.c_str());

    char *temp;

    while((temp = strstr(full, "/..")) != NULL) {
      char temp2[maxDocRequest];
      strncpy(temp2, full, temp-full);
      temp2[temp-full] = 0;

      char temp3[maxDocRequest];
      strncpy(temp3, temp2, strrchr(temp2, '/')-temp2);
      temp3[strrchr(temp2, '/')-temp2] = 0;

      char *temp4 = strchr(&temp[1], '/');

      fullPath = "";
      fullPath += temp3;

      if(temp4 != NULL) {
        fullPath += temp4;
      }

      full = strdup(fullPath.c_str());
    }

    free(full);
    full = NULL;
  }

  if(fullPath.length() < rootDir.length()) {
    response_string = "HTTP/1.1 403 Forbidden\r\nServer: CS 252 lab5";
    forbidden(fd, response_string);

    if(mode == 1 || mode == 2) {
      close(fd);
    }
    end1 = std::chrono::system_clock::now();
    std::chrono::duration<double> diff = end1-start1;

    double dur = diff.count();
    if(dur < minService) {
      pthread_mutex_lock(&mutex);
      minService = dur;
      pthread_mutex_unlock(&mutex);
    }

    if(dur > maxService) {
      pthread_mutex_lock(&mutex);
      maxService = dur;
      pthread_mutex_unlock(&mutex);
    }

    return;
  }

  char *mode = strrchr(doc, '?');

  if(mode != NULL) {
    fullPath = "";
    fullPath += lastPath;

    setMode(mode);

    DIR *dir = opendir(fullPath.c_str());

    if(dir != NULL) {
      response_string = "HTTP/1.1 200 Document follows\r\nServer: CS 252 lab5\r\nContent-type: ";

      write(fd, response_string, strlen(response_string));
      write(fd, "text/html", strlen("text/html"));
      write(fd, "\r\n\r\n", strlen("\r\n\r\n"));

      sendDir(fd, dir, (char *)fullPath.c_str());

      close(fd);
      closedir(dir);
      end1 = std::chrono::system_clock::now();
      std::chrono::duration<double> diff = end1-start1;

      double dur = diff.count();
      if(dur < minService) {
        pthread_mutex_lock(&mutex);
        minService = dur;
        pthread_mutex_unlock(&mutex);
      }

      if(dur > maxService) {
        pthread_mutex_lock(&mutex);
        maxService = dur;
        pthread_mutex_unlock(&mutex);
      }

      return;
    }
  }

  DIR *dir = opendir(fullPath.c_str());

  if(dir != NULL) {
    sortMode = 0;
    response_string = "HTTP/1.1 200 Document follows\r\nServer: CS 252 lab5\r\nContent-type: ";

    write(fd, response_string, strlen(response_string));
    write(fd, "text/html\r\n", strlen("text/html\r\n"));
    write(fd, "\r\n\r\n", strlen("\r\n\r\n"));

    sendDir(fd, dir, (char *)fullPath.c_str());

    lastPath = "";
    lastPath += fullPath;

    close(fd);
    closedir(dir);
    end1 = std::chrono::system_clock::now();
    std::chrono::duration<double> diff = end1-start1;

    double dur = diff.count();
    if(dur < minService) {
      pthread_mutex_lock(&mutex);
      minService = dur;
      pthread_mutex_unlock(&mutex);
    }

    if(dur > maxService) {
      pthread_mutex_lock(&mutex);
      maxService = dur;
      pthread_mutex_unlock(&mutex);
    }

    return;
  }

  char *type = endsWith((char *) fullPath.c_str());

  FILE *f = fopen(fullPath.c_str(), "r");

  if(f == NULL) {
    response_string = "HTTP/1.1 404 File Not Found\r\nServer: CS 252 lab5\r\nContent-type: ";
    fileNotFound(fd, type, response_string);
  } else {
    response_string = "HTTP/1.1 200 Document follows\r\nServer: CS 252 lab5\r\nContent-type: ";

    write(fd, response_string, strlen(response_string));
    write(fd, type, strlen(type));
    write(fd, "\r\n\r\n", strlen("\r\n\r\n"));

    while(fread(&c, 1, 1, f) != -1) {
      if(feof(f)) {
        break;
      }
      if(write(fd, &c, 1) != 1) {
        continue;
      }
    }
    fclose(f);
  }
  close(fd);
  end1 = std::chrono::system_clock::now();
  std::chrono::duration<double> diff = end1-start1;

  double dur = diff.count();
  if(dur < minService) {
    pthread_mutex_lock(&mutex);
    minService = dur;
    pthread_mutex_unlock(&mutex);
  }
  if(dur > maxService) {
    pthread_mutex_lock(&mutex);
    maxService = dur;
    pthread_mutex_unlock(&mutex);
  }
}

void fileNotFound(int fd, char *type, char *response_string) {
  write(fd, response_string, strlen(response_string));
  write(fd, type, strlen(type));
  write(fd, "\r\n\r\n", strlen("\r\n\r\n"));
}

void badRequest(int fd, char *response_string) {
  write(fd, response_string, strlen(response_string));
  write(fd, "\r\n\r\n", strlen("\r\n\r\n"));
}

void forbidden(int fd, char *response_string) {
  write(fd, response_string, strlen(response_string));
  write(fd, "\r\n\r\n", strlen("\r\n\r\n"));
}

void unauthorized(int fd, char *response_string) {
  write(fd, response_string, strlen(response_string));
  write(fd, realm, strlen(realm));
  write(fd, "\"", 1);
  write(fd, "\r\n\r\n", strlen("\r\n\r\n"));
}

void setMode(char *mode) {
  if(strstr(mode, "C=N") != NULL) {
    alphaBool = !alphaBool;
    dateBool = false;
    sizeBool = false;
    descBool = false;
    sortMode = 0;
  } else if(strstr(mode, "C=M") != NULL) {
    dateBool = !dateBool;
    alphaBool = false;
    sizeBool = false;
    descBool = false;

    sortMode = 1;
  } else if(strstr(mode, "C=S") != NULL) {
    sizeBool = !sizeBool;
    alphaBool = false;
    dateBool = false;
    descBool = false;

    sortMode = 2;
  } else if(strstr(mode, "C=D") != NULL) {
    descBool = !descBool;
    alphaBool = false;
    dateBool = false;
    sizeBool = false;

    sortMode = 3;
  }
}

void cgiBin(int fd, char *response_string, std::string fullPath) {
  write(fd, response_string, strlen(response_string));

  char *full = strdup(fullPath.c_str());
  char full2[maxDocRequest];

  char *temp = strrchr(full, '/');
  temp++;

  char *request_method = "REQUEST_METHOD";
  char *request = "GET";
  char *query_string = "QUERY_STRING";
  char *query = strrchr(full, '?');

  if(query == NULL) {
    response_string = "HTTP/1.1 400 Bad Request\r\nServer: CS 252 lab5";
    badRequest(fd, response_string);
    if(mode == 1 || mode == 2) {
      close(fd);
    }
    end1 = std::chrono::system_clock::now();
    std::chrono::duration<double> diff = end1-start1;

    double dur = diff.count();
    if(dur < minService) {
      pthread_mutex_lock(&mutex);
      minService = dur;
      pthread_mutex_unlock(&mutex);
    }

    if(dur > maxService) {
      pthread_mutex_lock(&mutex);
      maxService = dur;
      pthread_mutex_unlock(&mutex);
    }

    free(full);
    full = NULL;

    return;
  }

  char file[maxDocRequest];

  strncpy(file, temp, query-temp);
  strncpy(full2, full, query-full);

  file[query-temp] = 0;
  full2[query-full] = 0;

  const char *argv[2];

  argv[0] = file;
  argv[1] = NULL;
  query++;

  setenv(request_method, request, 1);
  setenv(query_string, query, 1);

  if(strstr(file, ".so") != NULL) {
    std::string tempPath = "";
    tempPath += "./";
    tempPath += file;
    void *lib = dlopen(tempPath.c_str(), RTLD_LAZY);

    if(lib == NULL) {
      fprintf(stderr, "file not found\n");
      perror("dlopen");
      exit(1);
    }

    httprunfunc hello_httprun;

    hello_httprun = (httprunfunc) dlsym(lib, "httprun");
    if(hello_httprun == NULL) {
      perror("dlsym: httprun not found:");
      exit(1);
    }

    hello_httprun(fd, query);

    free(full);
    full = NULL;

    return;
  }

  int defaultout = dup(1);

  dup2(fd, 1);

  execv(full2, (char *const *)argv);

  dup2(defaultout, 1);

  free(full);
  full = NULL;
}

void sendDir(int fd, DIR *dir, char *path) {
  write(fd, "<html>", 6);
  write(fd, "<head>", 6);
  write(fd, "<title>", 7);
  write(fd, "Index of ", 9);
  write(fd, path, strlen(path));
  write(fd, "</title>", 8);
  write(fd, "</head>", 7);
  write(fd, "<body data-new-gr-c-s-check-loaded=\"14.1006.0\" data-gr-ext-installed>", 69);
  write(fd, "<h1>", 4);
  write(fd, "Index of /homes", 15);
  write(fd, strstr(path, "/cs252"), strlen(strstr(path, "/cs252")));
  write(fd, "</h1>", 5);
  write(fd, "<table>", 7);
  write(fd, "<tbody>", 7);

  write(fd, "<tr>", 4);

  write(fd, "<th valign=\"top\">", 17);
  write(fd, "<img src=\"/u/riker/u93/yelshall/cs252/lab5-src/http-root-dir/dirIcons/blank.gif\" alt=\"[ICO]\">", 92);
  write(fd, "</th>", 5);

  write(fd, "<th>", 4);
  write(fd, "<a href=\"?C=N;O=", 16);
  if(!alphaBool) {
    write(fd, "D", 1);
  } else {
    write(fd, "A", 1);
  }
  write(fd, "\">Name</a>", 10);
  write(fd, "</th>", 5);

  write(fd, "<th>", 4);
  write(fd, "<a href=\"?C=M;O=", 16);
  if(!dateBool) {
    write(fd, "D", 1);
  } else {
    write(fd, "A", 1);
  }
  write(fd, "\">Last modified</a>", 18);
  write(fd, "</th>", 5);

  write(fd, "<th>", 4);
  write(fd, "<a href=\"?C=S;O=", 16);
  if(!sizeBool) {
    write(fd, "D", 1);
  } else {
    write(fd, "A", 1);
  }
  write(fd, "\">Size</a>", 10);
  write(fd, "</th>", 5);

  write(fd, "<th>", 4);
  write(fd, "<a href=\"?C=D;O=", 16);
  if(!descBool) {
    write(fd, "D", 1);
  } else {
    write(fd, "A", 1);
  }
  write(fd, "\">Description</a>", 17);
  write(fd, "</th>", 5);

  write(fd, "</tr>", 5);

  write(fd, "<tr><th colspan=\"5\"><hr></th></tr>", 34);

  write(fd, "<tr><td valign=\"top\"><img src=\"/u/riker/u93/yelshall/cs252/lab5-src/http-root-dir/dirIcons/back.gif\" alt=\"[PARENTDIR]\"></td><td><a href=", 136);
  char *temp = strrchr(path, '/');
  char temp2[maxDocRequest];
  char temp3[maxDocRequest];

  if(strlen(temp) == 1 || temp[0] == '?') {
    strncpy(temp2, path, temp-path);
    temp = strrchr(temp2, '/');

    strncpy(temp3, temp2, temp-temp2);

    temp3[temp-temp2] = 0;
  } else {
    strncpy(temp3, path, temp-path);
    temp3[temp-path] = 0;
  }

  write(fd, temp3, strlen(temp3));
  write(fd, ">Parent Directory</a></td><td>&nbsp;</td><td align=\"right\">-</td><td>&nbsp;</td></tr>", 87);

  struct dirent *ent;
  struct stat *filestat;
  std::string fullPath = "";
  fullPath += path;
  struct file_struct *file;
  struct file_struct *files[maxFiles];
  int x = 0;
  while((ent = readdir(dir)) != NULL) {
    if(ent->d_name[0] != '.') {
      fullPath += "/";
      fullPath += ent->d_name;
      file = getInfo(ent, (char *)fullPath.c_str());
      fullPath = "";
      fullPath += path;

      files[x] = file;
      x++;
    }
  }

  if(sortMode == 0) {
    if(!alphaBool) {
      sortAlpha((struct file_struct **) files, x);
    } else {
      sortAlphaReverse((struct file_struct **) files, x);
    }
  } else if(sortMode == 1) {
    if(!dateBool) {
      sortDate((struct file_struct **) files, x);
    } else {
      sortDateReverse((struct file_struct **) files, x);
    }
  } else if(sortMode == 2) {
    if(!sizeBool) {
      sortSize((struct file_struct **) files, x);
    } else {
      sortSizeReverse((struct file_struct **) files, x);
    }
  } else if(sortMode == 3) {
    if(!descBool) {
      sortAlpha((struct file_struct **) files, x);
    } else {
      sortAlphaReverse((struct file_struct **) files, x);
    }
  }

  for(int i = 0; i < x; i++) {
    write(fd, "<tr><td valign=\"top\"><img src=\"/u/riker/u93/yelshall/cs252/lab5-src/http-root-dir/dirIcons/", 91);
    if(files[i]->dir) {
      write(fd, "folder.gif", 10);
    } else if(strcmp(endsWith(files[i]->path), "error") == 0) {
      write(fd, "unknown.gif", 11);
    } else {
      write(fd, "image2.gif", 10);
    }
    write(fd, "\" alt=\"[   ]\"></td><td><a href=\"", 32);
    std::string relative = "";
    write(fd, files[i]->path, strlen(files[i]->path));

    write(fd, "\">", 2);
    write(fd, files[i]->name, strlen(files[i]->name));
    write(fd, "</a></td><td align=\"right\">", 27);
    write(fd, files[i]->date, strlen(files[i]->date));
    write(fd, " </td><td align=\"right\">", 24);

    write(fd, files[i]->size, strlen(files[i]->size));
    write(fd, " </td><td>&nbsp;</td></tr>", 26);
  }

  for(int i = 0; i < x; i++) {
    free(files[i]->path);
    files[i]->path = NULL;
    free(files[i]->name);
    files[i]->name = NULL;
    free(files[i]->date);
    files[i]->date = NULL;
    free(files[i]->size);
    files[i]->size = NULL;

    free(files[i]);
    files[i] = NULL;
  }

  write(fd, "<tr><th colspan=\"5\"><hr></th></tr>", 34);

  write(fd, "</tbody>", 8);
  write(fd, "</table>", 8);
  write(fd, "</body>", 7);
  write(fd, "</html>", 7);
}

void statsPage(int fd) {
  write(fd, "<html>", 6);
  write(fd, "<head><title>Statistics page of yelshall server implementation</title></head>", 77);
  write(fd, "<body>", 6);
  write(fd, "<h1>Statistics page of yelshall server implementation</h1>", 58);

  write(fd, "<h2>Server Implementation by: Youssuf El Shall</h2>", 51);


  write(fd, "<UL><LI><h3>Time server has been running: ", 42);

  auto end = std::chrono::system_clock::now();
  std::chrono::duration<double> diff = end-start;

  double dur = diff.count();

  char temp[10];

  char *timeElapsed = (timeFormatted(dur));

  write(fd, timeElapsed, strlen(timeElapsed));

  free(timeElapsed);
  timeElapsed = NULL;

  write(fd, "</h4></UL>", 10);


  write(fd, "<UL><LI><h3>Number of Requests Served: ", 39);

  write(fd, itoa(noRequests, temp, 10), strlen(itoa(noRequests, temp, 10)));

  write(fd, "</h4></UL>", 10);


  write(fd, "<UL><LI><h3>Minimum Service Time: ", 34);

  char minServiceString[maxDocRequest] = {0};
  char maxServiceString[maxDocRequest] = {0};

  if(minService == 2147483647) {
    sprintf(minServiceString, "%f Seconds", 0.0);
  } else {
    sprintf(minServiceString, "%f Seconds", minService);
  }
  sprintf(maxServiceString, "%f Seconds", maxService);

  write(fd, minServiceString, strlen(minServiceString));

  write(fd, "</h4></UL>", 10);

  write(fd, "<UL><LI><h3>Maximum Service Time: ", 34);

  write(fd, maxServiceString, strlen(maxServiceString));

  write(fd, "</h4></UL>", 10);

  write(fd, "</body>", 6);
  write(fd, "</html>", 7);
}

void logsPage(int fd) {
  write(fd, "<html>", 6);

  write(fd, "<head>", 6);

  write(fd, "<title>Logs page of yelshall server implementation</title>", 58);

  write(fd, "</head>", 7);

  write(fd, "<body>", 6);

  write(fd, "<h1>Logs page of yelshall server implementation</h1>", 52);

  write(fd, "<table>", 7);

  write(fd, "<tr><th colspan=\"1\"><hr></th></tr>", 34);

  FILE *f = fopen("./http-root-dir/log/logFile.txt", "r");

  fseek(f, 0, SEEK_SET);

  int p = 0;
  char d[maxDocRequest];
  char response[maxDocRequest] = {0};

  fscanf(f, "\n");

  while(fscanf(f, "Source: data.cs.purdue.edu:%d, Directory Requested: %s\n", &p, d) == 2) {
    sprintf(response, "Source: data.cs.purdue.edu:%d, Directory Requested: %s", p, d);
    write(fd, "<tr>", 4);
    write(fd, "<td>", 4);
    write(fd, "<h3>", 4);
    write(fd, response, strlen(response));
    write(fd, "</h3>", 5);
    write(fd, "</td>", 5);
    write(fd, "</tr>", 5);
  }

  fclose(f);

  write(fd, "</table>", 8);

  write(fd, "</body>", 7);

  write(fd, "</html>", 7);
}

char *timeFormatted(int seconds) {
  char time[maxDocRequest] = {0};

  int h, m, s;

  h = (seconds/3600);

  m = (seconds - (3600*h))/60;

  s = (seconds - (3600*h) - (m*60));

  sprintf(time, "%d Hours, %d Minutes, %d Seconds", h, m, s);

  return strdup(time);
}

void writeLog(char *log) {
  FILE *f = fopen("./http-root-dir/log/logFile.txt", "a+");

  fprintf(f, "Source: data.cs.purdue.edu:%d, Directory Requested: %s\n", port, log);

  free(log);
  log = NULL;

  fclose(f);
}

struct file_struct *getInfo(struct dirent *ent, char *path) {
  struct file_struct *info = (struct file_struct *) malloc(sizeof(struct file_struct));
  info->name = strdup(ent->d_name);

  info->path = strdup(path);

  struct stat filestat;
  stat(path, &filestat);
  std::string temp = "";
  temp += ctime(&filestat.st_mtime);
  info->date = dateFormat((char *) temp.c_str());

  if(ent->d_type != DT_DIR) {
    char size[10];
    FILE *f = fopen(path, "r");
    info->size = strdup(itoa(fsize(f), size, 10));
    fclose(f);
    info->dir = false;
  } else {
    char *temp = "-";
    info->size = strdup(temp);
    info->dir = true;
  }

  return info;
}

char *dateFormat(char *date) {
  std::string finalDate = "";
  char *year = strrchr(date, ' ');
  year++;
  year[4] = 0;
  finalDate += year;

  char temp[maxDocRequest];
  strncpy(temp, date, (year-1)-date);

  char *time = strrchr(temp, ' ');
  time++;

  char temp2[maxDocRequest];

  strncpy(temp2, temp, (time-1)-temp);

  char *day = strrchr(temp2, ' ');
  day++;

  finalDate += "-4-";

  finalDate += day;

  finalDate += " ";

  finalDate += time;

  return (char *) strdup(finalDate.c_str());
}

void sortAlpha(struct file_struct **files, int n) {
  struct file_struct **arr = files;
  struct file_struct *key;
  int i, j;
  for(i = 1; i < n; i++) {
    key = arr[i];
    j = i - 1;
    while (j >= 0 && strcmp(arr[j]->name, key->name) > 0) {
      arr[j + 1] = arr[j];
      j = j - 1;
    }
    arr[j + 1] = key;
  }
}

void sortAlphaReverse(struct file_struct **files, int n) {
  struct file_struct **arr = files;
  struct file_struct *key;
  int i, j;
  for(i = 1; i < n; i++) {
    key = arr[i];
    j = i - 1;
    while (j >= 0 && strcmp(arr[j]->name, key->name) < 0) {
      arr[j + 1] = arr[j];
      j = j - 1;
    }
    arr[j + 1] = key;
  }
}

int compareDate(file_struct *one, file_struct *two) {
  if(strcmp(one->date, two->date) == 0) {
    return strcmp(one->name, two->name);
  }
  return strcmp(one->date, two->date);
}

int compareSize(file_struct *one, file_struct *two) {
  int x = atoi(one->size);
  int y = atoi(two->size);
  if(x == y) {
    return strcmp(one->name, two->name);
  } else if(x > y) {
    return 1;
  }
  return -1;
}

void sortDate(struct file_struct **files, int n) {
  struct file_struct **arr = files;
  struct file_struct *key;
  int i, j;
  for(i = 1; i < n; i++) {
    key = arr[i];
    j = i - 1;
    while (j >= 0 && compareDate(arr[j], key) > 0) {
      arr[j + 1] = arr[j];
      j = j - 1;
    }
    arr[j + 1] = key;
  }
}

void sortDateReverse(struct file_struct **files, int n) {
  struct file_struct **arr = files;
  struct file_struct *key;
  int i, j;
  for(i = 1; i < n; i++) {
    key = arr[i];
    j = i - 1;
    while (j >= 0 && compareDate(arr[j], key) < 0) {
      arr[j + 1] = arr[j];
      j = j - 1;
    }
    arr[j + 1] = key;
  }
}

void sortSize(struct file_struct **files, int n) {
  struct file_struct **arr = files;
  struct file_struct *key;
  int i, j;
  int x, y;
  for(i = 1; i < n; i++) {
    key = arr[i];
    j = i - 1;

    while (j >= 0 && compareSize(arr[j], key) > 0) {
      arr[j + 1] = arr[j];
      j = j - 1;
      if(j >= 0) {
        x = atoi(arr[j]->size);
      }
    }
    arr[j + 1] = key;
  }
}

void sortSizeReverse(struct file_struct **files, int n) {
  struct file_struct **arr = files;
  struct file_struct *key;
  int i, j;
  int x, y;
  for(i = 1; i < n; i++) {
    key = arr[i];
    j = i - 1;

    while (j >= 0 && compareSize(arr[j], key) < 0) {
      arr[j + 1] = arr[j];
      j = j - 1;
      if(j >= 0) {
        x = atoi(arr[j]->size);
      }
    }
    arr[j + 1] = key;
  }
}


int fsize(FILE *fp) {
    int prev=ftell(fp);
    fseek(fp, 0L, SEEK_END);
    int sz=ftell(fp);
    fseek(fp,prev,SEEK_SET); //go back to where we were
    return sz;
}


void createThreadForEachRequest(int masterSocket, sockaddr_in *sockInfo, int *alen) {
  while (1) {
    int clientSocket = accept(masterSocket, (struct sockaddr *) sockInfo,
    (socklen_t *)alen);

    if (clientSocket >= 0) {
    // When the thread ends resources are recycled
      pthread_t thread;
      pthread_attr_t attr;
      pthread_attr_init(&attr);
      pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
      pthread_attr_setscope(&attr, PTHREAD_SCOPE_SYSTEM);
      pthread_create(&thread, &attr, (void *(*)(void *))processHTTPRequest, (void *) clientSocket);
    }
  }
}

void poolOfThreads(int masterSocket, sockaddr_in *sockInfo, int *alen) {
  args.masterSocket = masterSocket;
  args.sockInfo = sockInfo;
  args.alen = alen;

  for(int i = 0; i < 4; i++) {
    pthread_t thread;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_attr_setscope(&attr, PTHREAD_SCOPE_SYSTEM);
    pthread_create(&thread, &attr, (void *(*)(void *))loopThread, (void *) &args);
  }

  loopThread(&args);
}

void loopThread(arg_struct *args) {
  while(1) {
    int clientSocket = accept(args->masterSocket, (struct sockaddr *) args->sockInfo,
    (socklen_t *)args->alen);

    if(clientSocket >= 0) {
      processHTTPRequest(clientSocket);
    }
  }
}

char * endsWith(char *path) {
  char *temp = strrchr(path, '.');

  if(temp == NULL) {
    return "error";
  }

  if(strcmp(temp, ".html") == 0) {
    return "text/html";
  } else if (strcmp(temp, ".gif") == 0) {
    return "image/gif";
  } else if (strcmp(temp, ".xbm") == 0) {
    return "image/x-xbitmap";
  } else if (strcmp(temp, ".png") == 0) {
    return "image/png";
  } else if (strcmp(temp, ".jpg") == 0) {
    return "image/jpeg";
  } else if (strcmp(temp, ".svg") == 0) {
    return "image/svg+xml";
  } else {
    return "error";
  }
}

char* itoa(int value, char* result, int base) {
  // check that the base if valid
  if (base < 2 || base > 36) { *result = '\0'; return result; }

  char* ptr = result, *ptr1 = result, tmp_char;
  int tmp_value;

  do {
    tmp_value = value;
    value /= base;
    *ptr++ = "zyxwvutsrqponmlkjihgfedcba9876543210123456789abcdefghijklmnopqrstuvwxyz" [35 + (tmp_value - value * base)];
  } while ( value );

  // Apply negative sign
  if (tmp_value < 0) *ptr++ = '-';
  *ptr-- = '\0';
  while(ptr1 < ptr) {
    tmp_char = *ptr; 
    *ptr--= *ptr1;
    *ptr1++ = tmp_char;
  }
  return result;
}
