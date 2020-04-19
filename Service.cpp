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


Service::Service() {

}


/**
 * recv data:[4][...]
 * 前四个字节保存数据长度，之后的为json数据
 */

void Service::handle(Event *ev) {
    int n = recv(ev->fd, ev->buff, sizeof(ev->buff), 0);
    if (n > 0) {
        ev->len = n;
        ev->buff[n] = '\0';
        printf("recv: %s\n", ev->buff + 4);
        int content_len = 0;
        if (n > 4) {
            content_len = byteCharToInt(ev->buff);
        }
        char *p = ev->buff + 4;
        if (static_cast<int>(strlen(p)) != content_len) {
            std::cout << "[ERROR]>> recv data length not match" << std::endl;
            closesocket(ev->fd);
            ev->ClearBuffer();
            ev->Del();
            return;
        }

        rapidjson::Document *doc = new rapidjson::Document;
        if (doc->Parse(p).HasParseError()) {
            std::cout << "[ERROR]>> parse data err" << std::endl;
            closesocket(ev->fd);
            ev->ClearBuffer();
            ev->Del();
            delete (doc);
            return;
        } else if (doc->HasMember("Type")
                   && std::string(((*doc)["Type"]).GetString()) == "service"
                   && doc->HasMember("Op")
                   && std::string((*doc)["Op"].GetString()) == "REG"
                   && doc->HasMember("ServiceList")
                   && doc->HasMember("ServicePort")
                   && doc->HasMember("Proportion")
                   && (this->config->node_type == NodeType::Single || this->config->node_type == NodeType::Master)) {
            this->el->customEventManger->Emit("ServiceREG", {ev, doc});
        } else if (
                doc->HasMember("Op")
                && std::string(((*doc)["Op"]).GetString()) == "PULL"
                && doc->HasMember("ServiceList")
                && doc->HasMember("Type")
                && std::string(((*doc)["Type"]).GetString()) == "client"
                ) {
            this->el->customEventManger->Emit("ClientPULL", {ev, doc});
        } else if(
                doc->HasMember("Type")
                && std::string(((*doc)["Type"]).GetString()) == "slave"
                && doc->HasMember("NodeName")
                && this->config->node_type == NodeType::Master
                ){
            this->el->customEventManger->Emit("SlaveConnect", {ev, doc});
        }

    } else if ((n < 0) && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
        ev->Set(ev->fd, SelectEvent::Read, std::bind(&Service::handle, this, std::placeholders::_1));
    } else if (n == 0 || n == -1) {
        std::cout << "[Notify]>> clinet: " << ev->fd << " closed" << std::endl;
        closesocket(ev->fd);
        ev->ClearBuffer();
        ev->Del();
    }
}


#ifdef _WIN64

void Service::disconnect(SOCKET cfd) {
//    slavers.remove_if([&](SlaverInfo n){
//        return n.fd == cfd;
//    });
//    FD_CLR(cfd, &select_fd);
//    closesocket(cfd);
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
    struct ServiceClientInfo info(fd, std::move(remoteIp));
    if (!count(this->service_client, info)) {
        this->service_client.push_back(info);
    }
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
        this->el->timeEventManeger->LoadTimeEventMap(
                std::bind(&Service::proceHeartCheck, this, std::placeholders::_1),
                nullptr,
                TimeEvemtType::CERCLE,
                {},
                this->config->heart_check_time * 1000
        );

        this->el->customEventManger->On("SlaveConnect",
                                        std::bind(&Service::OnSlaveConnect, this, std::placeholders::_1, std::placeholders::_2));

    } else if (this->config->node_type == NodeType::Slave) {
//        if (this->config->master_ip.empty()) {
//            std::cout << "[ERROR]>> node_type is Slave, must config master_ip" << std::endl;
//            exit(-1);
//        }
//        std::thread t(&Service::slaveRun, this);
//        t.detach();
    }

    this->el->LoadEventMap(this->socket_fd, std::bind(&Service::handle, this, std::placeholders::_1));
    this->el->customEventManger->On("ServiceREG", std::bind(&Service::OnServiceREG, this, std::placeholders::_1,
                                                            std::placeholders::_2));
    this->el->customEventManger->On("ClientPULL", std::bind(&Service::OnClientPULL, this, std::placeholders::_1,
                                                            std::placeholders::_2));
    this->el->customEventManger->On("SendData",
                                    std::bind(&Service::SendData, this, std::placeholders::_1, std::placeholders::_2));

    this->el->Run();
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
    FD_SET(0, &fd);
    if (send(master_fd, reqData, len, 0) < 0) {
        std::cout << "[ERROR]>> slave send data err!" << std::endl;
        WSACleanup();
        return;
    }

    while (runnning) {
        FD_SET(0, &fd);
        FD_SET(master_fd, &fd);
        ret = select(master_fd + 1, &fd, nullptr, nullptr, nullptr);
        if (ret == -1) {
            printf("[ERROR]>> select err!");
            exit(-1);
        }
        if (FD_ISSET(master_fd, &fd)) {
            int n = recv(master_fd, buff, MAXLINE, 0);
            if (n <= 0 && errno != EINTR) {
                std::cout << "[ERROR]>> socket closed" << std::endl;
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


void Service::proceHeartCheck(TimeEvent *event) {
    const char *check_data = "HEART CHECK";
    char recvline[100];
    std::list<ServiceClientInfo> err_client;

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
    this->slavers.push_back(SlaverInfo{static_cast<int>(fd), std::move(name), remoteIp, getTimeStamp()});
}


void Service::InitEL() {
    el = new EventLoop;
    el->InitEvents();
    el->InitEventManger();
    el->InitTimeEventManeger();

#ifndef _WIN64
    el->CreateEpoll();
#else
    el->InitFDS();
#endif
}


void Service::InitService() {
    this->socket_fd = CreateSocket(SERVICE_IP);
}


void Service::OnServiceREG(EventManger *eventManger, std::vector<pvoid> args) {
    auto *e = (Event *) args[0];
    auto *doc = (rapidjson::Document *) args[1];
    std::string port_str((*doc)["ServicePort"].GetString());
    std::string remoteIp = GetRemoTeIp(e->fd);
    remoteIp.append(port_str);
    int proportion = (*doc)["Proportion"].GetInt();
    const rapidjson::Value &serverList = (*doc)["ServiceList"];
    for (int i = 0; i < serverList.Size(); ++i) {
        std::string serName(serverList[i].GetString());
        if (server_list_map.count(serName)) {
            std::list<ServerInfo *> *temp = server_list_map[serName];
            temp->push_front(new ServerInfo(remoteIp, proportion));
        } else {
            auto *templist = new std::list<ServerInfo *>;
            templist->push_front(new ServerInfo(remoteIp, proportion));
            server_list_map[serName] = templist;
        }
    }
    collect_server(e->fd, remoteIp); //把服务提供方描述符放入一个列表
    e->ClearBuffer();
    strcpy(e->buff, "OK");
    e->len = 2;
    eventManger->Emit("SendData", {e});
    delete (doc);
}

//发送数据处理函数
void Service::SendData(EventManger *eventManger, std::vector<pvoid> args) {
    auto ev = (Event *) args[0];

    int ret = -1;
    int Total = 0;
    int lenSend = 0;
    struct timeval tv{};
    tv.tv_sec = 3;
    tv.tv_usec = 500;
    fd_set wset;
    while (true) {
        FD_ZERO(&wset);
        FD_SET(ev->fd, &wset);
        if (select(0, nullptr, &wset, nullptr, &tv) > 0)//3.5秒之内可以send，即socket可以写入
        {
            lenSend = send(ev->fd, ev->buff + Total, ev->len - Total, 0);
            if (lenSend == -1) {
                ev->ClearBuffer();
                closesocket(ev->fd);
                ev->Del();
                return;
            }
            Total += lenSend;
            if (Total == ev->len) {
                ev->ClearBuffer();
                return;
            }
        } else  //3.5秒之内socket还是不可以写入，认为发送失败
        {
            ev->ClearBuffer();
            closesocket(ev->fd);
            ev->Del();
            return;
        }
    }

}

void Service::OnClientPULL(EventManger *eventManger, std::vector<pvoid> args) {
    auto *e = (Event *) args[0];
    auto *doc = (rapidjson::Document *) args[1];
    rapidjson::StringBuffer s;
    rapidjson::Writer<rapidjson::StringBuffer> w(s);
    std::map<std::string, std::list<ServerInfo *> *> temp_map;
    auto serviceList = ((*doc)["ServiceList"]).GetArray();
    for (auto it = serviceList.Begin(); it != serviceList.End(); it++) {
        std::string ser_name(it->GetString());
        if (server_list_map.count(ser_name)) {
            temp_map[ser_name] = server_list_map[ser_name];
        } else {
            std::list<ServerInfo *> tmep_list;
            temp_map[ser_name] = &tmep_list;
        }
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
    strcpy(e->buff, s.GetString());
    e->len = s.GetLength();
    eventManger->Emit("SendData", {e});
    delete(doc);
}


void Service::OnSlaveConnect(EventManger *eventManger, std::vector<pvoid> args) {
    auto *e = (Event *) args[0];
    auto *doc = (rapidjson::Document *) args[1];
    collect_slave(e->fd, GetRemoTeIp(e->fd), std::string((*doc)["NodeName"].GetString()));

}


bool count(std::list<ServiceClientInfo> src, ServiceClientInfo target) {
    for (auto x : src) {
        if (x.ip == target.ip && x.fd == target.fd) {
            return true;
        }
    }
    return false;
}





