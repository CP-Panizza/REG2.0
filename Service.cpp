//
// Created by Administrator on 2020/4/9.
//

#include "Service.h"
#include "socket_header.h"
#include <iostream>
#include <thread>
#include <inaddr.h>
#include "libs/rapidjson/document.h"
#include "libs/rapidjson/writer.h"
#include "libs/rapidjson/stringbuffer.h"
#include "util.h"

std::atomic<bool> runnning(true);


Service::Service(u_short port) {
#ifdef _WIN64

    CreateSocket(&this->socket_fd, &this->select_fd, port);

#else


#endif
}

void Service::run() {
    printf("======waiting for client's request======\n");
#ifdef _WIN64
    int ret;
    fd_set temp_fd;
    struct timeval t = {5, 0};
    while (runnning) {
        FD_ZERO(&temp_fd);
        temp_fd = select_fd;
        ret = select(socket_fd + 1, &temp_fd, NULL, NULL, &t);//最后一个参数为NULL，一直等待，直到有数据过来,客户端断开也会触发读/写状态，然后判断recv返回值是否为0，为0这说明客户端断开连接
        if (ret == SOCKET_ERROR) {
            printf("[ERROR]>> select err!");
            exit(-1);
        }

        for (int i = 0; i < temp_fd.fd_count; ++i) {
            //获取到套接字
            SOCKET s = temp_fd.fd_array[i];
            if (FD_ISSET(s, &temp_fd)) {
                //接收到客户端的链接
                if (s == socket_fd) {
                    do_accept();
                } else {
                    try {
                        handle(s, clients[s]);
                    } catch (const std::string err) {
                        std::cout << err << std::endl;
                        disconnect(s);
                    } catch (...) {
                        std::cout << "[ERROR]>> handle err!" << std::endl;
                        disconnect(s);
                    }
                }
            }
        }


    }


#else
    int ret;
    while (runnning) {
        ret = epoll_wait(epoll_fd, epoll_events, MAX_COUNT, -1);
        if (ret <= 0) {
            continue;
        } else {
            for (int i = 0; i < ret; i++) {
                if (epoll_events[i].data.fd == socket_fd) {
                    do_accept();
                } else {
                    handle(epoll_events[i].data.fd, clients[epoll_events[i].data.fd]);
                }
            }
        }
    }
#endif
}

void Service::do_accept() {
#ifdef _WIN64
    struct sockaddr_in addrClient;
    socklen_t len = sizeof(struct sockaddr_in);
    SOCKET c = accept(socket_fd, (struct sockaddr *) &addrClient, &len);
    if (c == INVALID_SOCKET) {
        std::cout << "[ERROR]>> accept err!" << std::endl;
        return;
    }
    clients[c] = inet_ntoa(addrClient.sin_addr);
    FD_SET(c, &select_fd);

#else
    struct sockaddr_in cli;
    socklen_t len = sizeof(cli);
    int new_fd = accept(socket_fd, (struct sockaddr *) &cli, &len);
    if (new_fd == -1) {
        perror("accept err");
        return;
    }

    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = new_fd;
    int ret = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, new_fd, &ev);
    if (ret == -1) {
        perror("epoll_ctl err");
        return;
    }
    clients[new_fd] = inet_ntoa(cli.sin_addr);
#endif
}


/**
 * recv data:[4][...]
 * 前四个字节保存数据长度，之后的为json数据
 */
void Service::handle(SOCKET connfd, std::string remoteIp) {
    char buff[MAXLINE] = {0};
    std::string respmsg;
    int resplen = 0;
    int n = recv(connfd, buff, MAXLINE, 0);
    buff[n] = '\0';
    if(n <= 0 && errno != EINTR){
        throw std::string("[ERROR]>> socket closed");
    }
    int content_len = 0;
    if (n > 4) {
        content_len = byteCharToInt(buff);
    }

    char *p = buff + 4;

    if (static_cast<int>(strlen(p)) != content_len) {
        throw std::string("[ERROR]>> recv data length not match");
    }

    printf("[DEBUG]>> recv msg from client: %s\n", buff);
    rapidjson::Document doc;
    if (doc.Parse(p).HasParseError()) {
        respmsg = "Data err!";
    } else if (doc.HasMember("Type")
               && doc["Type"].GetString() == "service"
               && doc.HasMember("Op")
               && std::string(doc["Op"].GetString()) == "REG"
               && doc.HasMember("ServiceList")
               && doc.HasMember("ServicePort")
               && doc.HasMember("Proportion")
               && (this->config->node_type == NodeType::Single || this->config->node_type == NodeType::Master)) {
        std::string port_str(doc["ServicePort"].GetString());
        remoteIp.append(port_str);
        int proportion = doc["Proportion"].GetInt();
        const rapidjson::Value &serverList = doc["ServiceList"];
        for (int i = 0; i < serverList.Size(); ++i) {
            std::string serName(serverList[i].GetString());
            lock.lockWrite();
            if (server_list_map.count(serName)) {
                std::list<ServerInfo *> *temp = server_list_map[serName];
                temp->push_front(new ServerInfo(remoteIp, proportion));
            } else {
                auto *templist = new std::list<ServerInfo *>;
                templist->push_front(new ServerInfo(remoteIp, proportion));
                server_list_map[serName] = templist;
            }
            lock.unlockWrite();
        }
        collect_server(connfd, remoteIp); //把服务提供方描述符放入一个列表
        respmsg = "OK";
        resplen = respmsg.length();
    } else if (
            doc.HasMember("Op")
            && std::string(doc["Op"].GetString()) == "PULL"
            && doc.HasMember("ServiceList")
            && doc.HasMember("Type")
            && doc["Type"].GetString() == "client"
            ) {
        rapidjson::StringBuffer s;
        rapidjson::Writer<rapidjson::StringBuffer> w(s);

        std::map<std::string, std::list<ServerInfo *> *> temp_map;
        auto serviceList = doc["ServiceList"].GetArray();

        for (auto it = serviceList.Begin(); it != serviceList.End(); it++) {
            std::string ser_name(it->GetString());
            lock.lockRead();
            if (server_list_map.count(ser_name)) {
                temp_map[ser_name] = server_list_map[ser_name];
            } else {
                std::list<ServerInfo *> tmep_list;
                temp_map[ser_name] = &tmep_list;
            }
            lock.unlockRead();
        }
        w.StartObject();
        w.Key("data");
        w.StartArray();
        for (auto key_val : temp_map) {
            w.StartObject();
            w.Key(key_val.first.c_str());
            w.StartArray();
            for (auto &ip : *key_val.second) {
                w.StartObject();
                w.Key("Ip");
                w.String(ip->ip.c_str());
                w.Key("Proportion");
                w.Int(ip->proportion);
                w.EndObject();
            }
            w.EndArray();
            w.EndObject();
        }
        w.EndArray();
        w.EndObject();
        respmsg = s.GetString();
        resplen = respmsg.length();
    } else if (
            doc.HasMember("Op")
            && std::string(doc["Op"].GetString()) == "PULL"
            && doc.HasMember("Type")
            && doc["Type"].GetString() == "slave"
            && doc.HasMember("NodeName")
            ) {
        //把当前slave加入列表
        collect_slave(connfd, remoteIp, std::string(doc["NodeName"].GetString()));
        rapidjson::StringBuffer s;
        rapidjson::Writer<rapidjson::StringBuffer> w(s);
        w.StartObject();
        w.Key("Op");
        w.String("ADD");
        w.Key("Data");
        w.StartObject();
        lock.lockRead();
        for (auto x : server_list_map) {
            w.Key(x.first.c_str());
            w.StartArray();
            for (auto y : *x.second) {
                w.StartObject();
                w.Key("Ip");
                w.String(y->ip.c_str());
                w.Key("Proportion");
                w.Int(y->proportion);
                w.EndObject();
            }
            w.EndArray();
        }
        lock.unlockRead();
        w.EndObject();
        w.Key("Slaves");
        w.StartArray();
        for(auto slave : slavers){
            w.StartObject();
            w.Key("Ip");
            w.String(slave.ip.c_str());
            w.Key("Name");
            w.String(slave.name.c_str());
            w.Key("ConnectTime");
            w.Int64(slave.connect_time);
            w.EndObject();
        }
        w.EndArray();
        w.EndObject();
        resplen = 4 + s.GetLength();
        char data[resplen];
        char *head = to4ByteChar(s.GetLength());
        delete[] head;
        memcpy(data, head, 4);
        memcpy(data + 4, s.GetString(), s.GetLength());
        respmsg = std::string(data);
    }

    if (send(static_cast<SOCKET>(connfd), respmsg.c_str(), resplen, 0) < 0) {
        std::string fmt("[ERROR]>> send msg error: %s(errno: %d)\n");
        char targetString[1024];
        int len = snprintf(targetString,
                           sizeof(targetString),
                           fmt.c_str(),
                           strerror(errno),
                           errno);
        targetString[len] = '\0';
        throw std::string(targetString);
    }
}


#ifdef _WIN64

void Service::disconnect(SOCKET cfd) {
    clients.erase(cfd);
    slavers.remove_if([&](SlaverInfo n){
        return n.fd == cfd;
    });
    FD_CLR(cfd, &select_fd);
    closesocket(cfd);
}

#else
void Service::disconnect(int cfd) {
    clients.erase(cfd);
    slavers.remove_if([&](SlaverInfo n){
        return n.fd == cfd;
    });
    int ret = epoll_ctl(epoll_fd, EPOLL_CTL_DEL, cfd, NULL);
    if (ret == -1) {
        perror("epoll_ctl del cfd error");
        return;
    }
    close(cfd);
}
#endif


#ifdef _WIN64

void Service::collect_server(SOCKET fd, std::string remoteIp) {
    this->clients.erase(fd);
    struct ServiceClientInfo info(fd, std::move(remoteIp));
    lock.lockWrite();
    if (!count(this->service_client, info)) {
        this->service_client.push_back(info);
        FD_CLR(fd, &select_fd);
    }
    lock.unlockWrite();
}

#else
void Service::collect_server(int fd, std::string remoteIp) {
    this->clients.erase(fd);
    struct ServiceClientInfo info(fd, remoteIp);
    lock.lockWrite();
    if(!count(this->service_client, info)){
        this->service_client.push_back(info);
        int ret = epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL);
        if (ret == -1) {
            perror("epoll_ctl del cfd error");
            return;
        }
    }
    lock.unlockWrite();
}
#endif


void Service::ConfigAndRun(Config *_config) {
    this->config = _config;
    if (this->config->node_type == NodeType::Master) {
        this->heartCheckService();
    } else if (this->config->node_type == NodeType::Slave) {
        if (this->config->master_ip.empty()) {
            std::cout << "[ERROR]>> node_type is Slave, must config master_ip" << std::endl;
            exit(-1);
        }
        std::thread t(&Service::slaveRun, this);
        t.detach();
    }
    this->run();
}


void Service::slaveRun() {
    connect_to_master();
    int len;
    char *reqData = this->initSlaveRequestData(&len);
    char temp[len];
    memcpy(temp, reqData, len);
    delete[] reqData;
    char buff[MAXLINE] = {0};
    fd_set fd;
    int ret;
    FD_ZERO(&fd);
    FD_SET(0,&fd);
    if (send(master_fd, reqData, len, 0) < 0) {
        std::cout << "[ERROR]>> slave send data err!" << std::endl;
        WSACleanup();
        return;
    }

    while (runnning) {
        FD_SET(0,&fd);
        FD_SET(master_fd,&fd);
        ret = select(master_fd+1, &fd, nullptr, nullptr, nullptr);
        if (ret == -1) {
            printf("[ERROR]>> select err!");
            exit(-1);
        }
        if(FD_ISSET(master_fd, &fd)){
            int n = recv(master_fd, buff, MAXLINE, 0);
            if(n <= 0 && errno != EINTR){
                std::cout <<"[ERROR]>> socket closed" << std::endl;
                closesocket(master_fd);

                break;
                //选举并连接master
            }
            buff[n] = '\0';
            int content_len = 0;
            if (n > 4) {
                content_len = byteCharToInt(buff);
            }
            char *p = buff + 4;
            if (static_cast<int>(strlen(p)) != content_len) {
                throw std::string("[ERROR]>> recv data length not match");
            }
            rapidjson::Document doc;
            if (doc.Parse(p).HasParseError()) {

            }
        }
    }
}


void Service::heartCheckService() {
    std::thread t(&Service::heartCheckEntry, this);
    t.detach();
}


void Service::heartCheckEntry() {
    while (runnning) {
        proceHeartCheck();
    }
}


void Service::removeServerInfoByIp(std::string ip) {
    for (auto k_v : server_list_map) {
        k_v.second->remove_if([=](ServerInfo *n) {
            if (n->ip == ip) {
                delete n;
                return true;
            }
            return false;
        });
    }
}

//need delete return value
char *Service::initSlaveRequestData(int *len) {
    rapidjson::StringBuffer s;
    rapidjson::Writer<rapidjson::StringBuffer> w(s);
    w.StartObject();
    w.Key("Op");
    w.String("PULL");
    w.Key("Type");
    w.String("slave");
    w.Key("NodeName");
    w.String(this->config->node_name.c_str());
    w.EndObject();
    auto data_size = 4 + s.GetLength();
    char *temp = new char[data_size];
    auto _head = to4ByteChar(static_cast<unsigned int>(s.GetLength()));
    memcpy(temp, _head, 4);
    memcpy(temp + 4, s.GetString(), data_size - 4);
    delete[] _head;
    *len = static_cast<int>(data_size);
    return temp;
}

void Service::proceHeartCheck() {
    const char *check_data = "HEART CHECK";
    char recvline[100];
    clock_t start, end;
    double duration;
    std::list<ServiceClientInfo> err_client;
    start = clock();
    lock.lockWrite();
    if (!service_client.empty()) {
        for (auto item : service_client) {
            if ((send(item.fd, check_data, static_cast<int>(strlen(check_data)), 0)) < 0) {
                removeServerInfoByIp(item.ip);
                err_client.push_back(item);
            }
            if (recv(item.fd, recvline, sizeof(recvline), 0) < 0) {
                removeServerInfoByIp(item.ip);
                err_client.push_back(item);
            }
            memset(recvline, 0, strlen(recvline));
        }

        for (auto i : err_client) {
            service_client.remove_if([&](ServiceClientInfo n) {
                closesocket(i.fd);
                return n.fd == i.fd;
            });
        }
    }
    lock.unlockWrite();
    end = clock();
    duration = (double) (end - start) / CLOCKS_PER_SEC;
    auto sub_t = duration - this->config->heart_check_time;
    int64_t t;
    if (sub_t <= 0) {
        t = static_cast<int64_t>(this->config->heart_check_time + sub_t);
    } else {
        t = this->config->heart_check_time;
    }
    std::this_thread::sleep_for(std::chrono::seconds(t));
}

void Service::connect_to_master() {
    WORD dwVersion = MAKEWORD(2, 2);
    WSAData wsaData{};
    WSAStartup(dwVersion, &wsaData);
    sockaddr_in servaddr{};
    memset(&servaddr, 0, sizeof(servaddr));
    master_fd = 0;
    servaddr.sin_family = AF_INET; //网络类型
    servaddr.sin_addr.S_un.S_addr = inet_addr(this->config->master_ip.c_str());
    servaddr.sin_port = htons(SERVICE_IP);
    if ((master_fd = socket(AF_INET, SOCK_STREAM, 0)) == INVALID_SOCKET) {
        printf("[ERROR]>> create socket error: %s(errno: %d)\n", strerror(errno), errno);
        WSACleanup();
        exit(-1);
    }
    if (connect(master_fd, (struct sockaddr *) &servaddr, sizeof(servaddr)) < 0) {
        printf("[ERROR]>> connect socket error: %s(errno: %d)\n", strerror(errno), errno);
        WSACleanup();
        exit(-1);
    }
}

void Service::collect_slave(SOCKET fd, std::string remoteIp, std::string name) {
    this->clients.erase(fd);
    this->slavers.push_back(SlaverInfo{static_cast<int>(fd), std::move(name), remoteIp, getTimeStamp()});
    FD_CLR(fd, &select_fd);
}


bool count(std::list<ServiceClientInfo> src, ServiceClientInfo target) {
    for (auto x : src) {
        if (x.ip == target.ip && x.fd == target.fd) {
            return true;
        }
    }
    return false;
}





