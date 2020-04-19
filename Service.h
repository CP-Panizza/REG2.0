//
// Created by Administrator on 2020/4/9.
//

#ifndef REGISTCENTER2_0_SERVICE_H
#define REGISTCENTER2_0_SERVICE_H

#include "socket_header.h"
#include "RWLock.hpp"
#include <string>
#include <map>
#include <list>
#include <atomic>
#include "libs/EL/EventLoop.hpp"

#define MAXLINE 4096
#define SERVICE_IP 8527
#define DEFAULT_HEART_CHECK_TIME 60
enum NodeType {
    Master,
    Slave,
    Single
};

struct Config {
    Config(const std::string &node_name, NodeType node_type, const std::string &master_ip, int64_t heart_check_time)
            : node_name(node_name), node_type(node_type), master_ip(master_ip), heart_check_time(heart_check_time) {}

    std::string node_name;
    NodeType node_type;
    std::string master_ip;
    int64_t heart_check_time; //单位s
};

//保存服务提供者socket_fd和远端服务ip
struct ServiceClientInfo {
#ifdef _WIN64
    SOCKET fd;

    ServiceClientInfo(SOCKET _fd, std::string _ip) : fd(_fd), ip(_ip) {}

#else
    ServiceClientInfo(int _fd, std::string _ip):fd(_fd), ip(_ip){}
    int fd;
#endif
    std::string ip; //exp:127.0.0.1:8520
};

//保存服务注册信息
struct ServerInfo {
    ServerInfo(std::string _ip, int _proportion) : ip(_ip), proportion(_proportion) {}

    std::string ip;  //exp: 127.0.0.1:8888
    int proportion;
};

struct SlaverInfo {
    int fd;
    std::string name; //slave名字
    std::string ip; //slave ip
    time_t connect_time; //slave连接上master的时间
};



class Service {
public:
    Service();

    void InitService();

    void InitEL();

    void ConfigAndRun(Config *);

    void removeServerInfoByIp(std::string ip);

    void slaveRun();

    char *initSlaveRequestData(int *len);

    void proceHeartCheck(TimeEvent *event);

    void connect_to_master();

    void handle(Event *ev);

    void OnServiceREG(EventManger *eventManger, std::vector<pvoid> args);

    void OnClientPULL(EventManger *eventManger, std::vector<pvoid> args);

    void OnSlaveConnect(EventManger *eventManger, std::vector<pvoid> args);

    void SendData(EventManger *eventManger, std::vector<pvoid> args);
#ifdef _WIN64

    void disconnect(SOCKET cfd);

    void collect_server(SOCKET fd, std::string remoteIp);

    void collect_slave(SOCKET fd, std::string remoteIp, std::string name);

#else
    void disconnect(int cfd);
    void collect_server(int fd, std::string remoteIp);
#endif


private:
    typedef std::map<std::string, std::list<struct ServerInfo *> *> Server_map;
    RWLock lock;
    Server_map server_list_map;
    std::list<struct ServiceClientInfo> service_client; //服务提供者信息
    std::list<SlaverInfo> slavers;
    Config *config;

    EventLoop *el;

#ifdef _WIN64

    SOCKET socket_fd;
    SOCKET master_fd;
#else
    int socket_fd;
#endif
};

extern std::atomic<bool> runnning;


bool count(std::list<ServiceClientInfo> src, ServiceClientInfo target);

#endif //REGISTCENTER2_0_SERVICE_H
