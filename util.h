//
// Created by Administrator on 2020/4/1.
//

#ifndef HTTP_WINDOWS_UTIL_H
#define HTTP_WINDOWS_UTIL_H


#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <map>
#include <ctime>
#include "socket_header.h"
std::vector<std::string> split(std::string str, std::string pattern);
bool contain(std::string str, std::string target);
bool file_exists (const std::string& name);
bool dir_exists(std::string path);
long file_size(const char *filepath);
void trim_space(std::string &s);
std::string &replace_all(std::string &str, const std::string &old_value, const std::string &new_value);
std::string read_file(std::string file);
std::map<std::string, std::string> getConf(std::string file);
time_t getTimeStamp();

char *to4ByteChar(unsigned int n); //需要delete返回值
unsigned int byteCharToInt(const char *data);

template <class Type> Type stringToNum(const std::string& in){
    std::istringstream iss(in);
    Type out;
    iss >> out;
    return out;
}


#ifdef _WIN64

void CreateSocket(SOCKET *secket_fd, fd_set *select_fd, u_short port);

#else

void CreateSocket(int *socket_fd, int *epoll_fd, struct epoll_event *epoll_events, u_short port);


#endif
#endif //HTTP_WINDOWS_UTIL_H
