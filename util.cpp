//
// Created by Administrator on 2020/4/1.
//

#include "util.h"
#include <dirent.h>
#include <sys/stat.h>
#include <fstream>
#include <iostream>
#include <map>
#include <time.h>

std::vector<std::string> split(std::string str, std::string pattern) {
    std::string::size_type pos;
    std::vector<std::string> result;
    str += pattern;//扩展字符串以方便操作
    auto size = static_cast<int>(str.size());

    for (int i = 0; i < size; i++) {
        pos = str.find(pattern, i);
        if (pos < size) {
            std::string s = str.substr(i, pos - i);
            result.push_back(s);
            i = static_cast<int>(pos + pattern.size() - 1);
        }
    }
    return result;
}

bool contain(std::string str, std::string target) {
    if (str == target)
        return true;
    if (str.empty())
        return false;
    if (target.empty())
        return true;
    std::string::size_type pos = str.find(target);
    return pos != std::string::npos;
}


bool file_exists(const std::string &name) {
    std::ifstream f(name.c_str());
    return f.good();
}


bool dir_exists(std::string path) {
    DIR *dir;
    if ((dir = opendir(path.c_str())) == NULL) {
        return false;
    }
    closedir(dir);
    return true;
}

long file_size(const char *filepath) {
    struct stat info{};
    stat(filepath, &info);
    int size = info.st_size;
    return size;
}

void trim_space(std::string &s) {
    int index = 0;
    if (!s.empty()) {
        while ((index = static_cast<int>(s.find(' ', index))) != std::string::npos) {
            s.erase(index, 1);
        }
    }
}

std::string &replace_all(std::string &str, const std::string &old_value, const std::string &new_value) {
    while (true) {
        std::string::size_type pos(0);
        if ((pos = str.find(old_value)) != std::string::npos) {
            str.replace(pos, old_value.length(), new_value);
        } else { break; }
    }
    return str;
}

std::string read_file(std::string file) {
    std::ifstream fin(file);
    std::stringstream buffer;
    buffer << fin.rdbuf();
    std::string str(buffer.str());
    return str;
}

std::map<std::string, std::string> getConf(std::string file) {
    std::map<std::string, std::string> conf;
    std::string data = read_file(file);
    auto v = split(data, "\n");
    for (auto x : v) {
        if(!contain(x, "#")){
            auto str = replace_all(x, "\n", "");
            trim_space(str);
            auto k_v = split(str, "=");
            if (k_v.size() == 2) {
                conf[k_v[0]] = k_v[1];
            }
        }
    }
    return conf;
}

time_t getTimeStamp() {
    time_t t;
    time(&t);
    return t;
}

char *to4ByteChar(unsigned int n) {
    auto *buff = new char[4];
    buff[0] = static_cast<char>((n >> 24) & 0xFF);
    buff[1] = static_cast<char>((n >> 16) & 0xFF);
    buff[2] = static_cast<char>((n >> 8) & 0xFF);
    buff[3] = static_cast<char>(n & 0xFF);
    return buff;
}

unsigned int byteCharToInt(const char *data){
    return (0xFFFFFF | (data[0] << 24)) & (0xFF00FFFF | (data[1] << 16)) & (0xFFFF00FF | (data[2] << 8)) & (0xFFFFFF00 | data[3]);
}





#ifdef _WIN64

void CreateSocket(SOCKET *socket_fd, fd_set *select_fd, u_short port) {
    WORD dwVersion = MAKEWORD(2, 2);
    WSAData wsaData{};
    WSAStartup(dwVersion, &wsaData);
    sockaddr_in servaddr{};
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET; //网络类型
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(port); //端口

    if ((*socket_fd = socket(AF_INET, SOCK_STREAM, 0)) == INVALID_SOCKET) {
        printf("[ERROR]>> create socket error: %s(errno: %d)\n", strerror(errno), errno);
        WSACleanup();
        exit(-1);
    }

    bool bReAddr = true;
    if (SOCKET_ERROR == (setsockopt(*socket_fd, SOL_SOCKET, SO_REUSEADDR, (char *) &bReAddr, sizeof(bReAddr)))) {
        std::cout << "[ERROR]>> set resueaddr socket err!" << std::endl;
        WSACleanup();
        exit(-1);
    }

    if (bind(*socket_fd, (struct sockaddr *) &servaddr, sizeof(servaddr)) == INVALID_SOCKET) {
        printf("[ERROR]>> bind socket error: %s(errno: %d)\n", strerror(errno), errno);
        WSACleanup();
        exit(-1);
    }

    //监听，设置最大连接数10
    if (listen(*socket_fd, 10) == INVALID_SOCKET) {
        printf("[ERROR]>> listen socket error: %s(errno: %d)\n", strerror(errno), errno);
        WSACleanup();
        exit(-1);
    }
    FD_ZERO(select_fd);
    FD_SET(*socket_fd, select_fd);
}


#else

void CreateSocket(int *socket_fd, int *epoll_fd, struct epoll_event *epoll_events, u_short port){
    struct sockaddr_in servaddr;
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET; //网络类型
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(port); //端口

    if ((*socket_fd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        printf("create socket error: %s(errno: %d)\n", strerror(errno), errno);
        exit(-1);
    }

    if (bind(*socket_fd, (struct sockaddr *) &servaddr, sizeof(servaddr)) == -1) {
        printf("bind socket error: %s(errno: %d)\n", strerror(errno), errno);
        exit(-1);
    }

    //监听，设置最大连接数10
    if (listen(*socket_fd, 10) == -1) {
        printf("listen socket error: %s(errno: %d)\n", strerror(errno), errno);
        exit(-1);
    }

    epoll_events = new struct epoll_event[MAX_COUNT];

    *epoll_fd = epoll_create(MAX_COUNT);
    struct epoll_event ev;
    ev.data.fd = *socket_fd;
    ev.events = EPOLLIN;
    epoll_ctl(*epoll_fd, EPOLL_CTL_ADD, *socket_fd, &ev);
}

#endif