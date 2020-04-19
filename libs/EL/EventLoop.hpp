//
// Created by cmj on 20-4-15.
//

#ifndef EVENTLOOP_EVENTLOOP_H
#define EVENTLOOP_EVENTLOOP_H

#include <functional>

#define MAXLINE 4096
#define MAX_COUNT 1024

#include "../../socket_header.h"


#include <string.h>


#include <iostream>
#include <fcntl.h>
#include <unistd.h>
#include <vector>
#include "Event.hpp"
#include "CusEvent.hpp"
#include "TimeEvent.hpp"
#include "../../util.h"

#define pvoid void *


struct MiddleData {
#ifndef _WIN64
    int fd;
    MiddleData(int fd, const Event::CallBack &callBack) : fd(fd), callBack(callBack) {}
#else
    SOCKET fd;
    MiddleData(SOCKET fd, const Event::CallBack &callBack) : fd(fd), callBack(callBack) {}
#endif

    Event::CallBack callBack;
};


class EventLoop {
public:
    EventManger *customEventManger;
    Event *events;
    TimeEventManeger *timeEventManeger;
    int cut_index;  //事件列表分割线

    bool running;

    EventLoop() {
        cut_index = MAX_COUNT;
    };

    void InitEvents() {
        this->events = new Event[MAX_COUNT + 1];
    }

    void InitEventManger(){
        this->customEventManger = new EventManger;
    }

    void InitTimeEventManeger(){
        this->timeEventManeger = new TimeEventManeger;
    }

    void ShutDown(){
        running = false;
        std::cout << "[INFO]>> Server is shutdown" << std::endl;
        exit(-1);
    }

#ifndef _WIN64

    int epoll_fd;



    void CreateEpoll() throw(std::runtime_error) {
        epoll_fd = epoll_create1(EPOLL_CLOEXEC);
        if (epoll_fd == -1) {
            throw std::runtime_error("[INIT_ERROR]>> create epoll faile");
        }
    }

    //为fd挂载tcp处理函数
    void LoadEventMap(int fd, Event::CallBack call_back) {

        Event *accepter = &this->events[cut_index--];

        accepter->data = new MiddleData(fd, call_back);

        accepter->SetSrcFd(epoll_fd);

        accepter->customEventManger = this->customEventManger;

        accepter->el = this;
        accepter->Set(fd, EPOLLIN, std::bind(&EventLoop::Accept, this, std::placeholders::_1));
    }


    //卸载fd上的tcp处理函数
    void UnLoadEventMap(int fd, std::function<void(Event *ev)> unload_call_back){
        for (int i = 0; i <= MAX_COUNT; ++i) {
            auto ev = &this->events[i];
            if(ev->fd == fd){
                ev->ClearBuffer();
                ev->Del();
                ev->callBack = nullptr;
                unload_call_back(ev);
            }
        }
    }

    void Accept(Event *ev) {

        MiddleData *data_ptr = (MiddleData *) ev->data;

        int i;

        for (i = 0; i < cut_index; ++i) {
            if (this->events[i].statu == EventStatu::Free) break;
        }

        if (i == cut_index) {
            std::cout << "[warning]>> max events limited" << std::endl;
            return;
        }

        int connfd = accept(data_ptr->fd, NULL, NULL);
        if (connfd < 0) {
            std::cout << "[ERROR]>> accept err" << std::endl;
            return;
        }

        setnonblocking(connfd);
        auto e = &this->events[i];
        e->SetSrcFd(epoll_fd);
        e->customEventManger = this->customEventManger;
        e->el = this;
        e->Set(connfd, EPOLLIN, data_ptr->callBack);
    }


    void Run() {
        struct epoll_event epoll_events[MAX_COUNT + 1];
        int i;
        running = true;
        while (running) {
            struct timeval tv;
            long now_sec, now_ms;
            TimeEvent *te = this->timeEventManeger->GetNearestEvent();
            if(te){
                GetTime(&now_sec, &now_ms);
                tv.tv_sec = te->when_sec - now_sec;
                if(te->when_ms < now_ms){
                    tv.tv_usec = (te->when_ms + 1000 - now_ms)*1000;
                    tv.tv_sec--;
                } else {
                    tv.tv_usec = (te->when_ms - now_ms)*1000;
                }
                if(tv.tv_sec < 0) tv.tv_sec = 0;
                if(tv.tv_usec < 0) tv.tv_usec = 0;
            } else {
                tv.tv_sec = 0;
                tv.tv_usec = 0;
            }

            int nfd = epoll_wait(this->epoll_fd, epoll_events, MAX_COUNT + 1, (tv.tv_sec*1000 + tv.tv_usec/1000));

            for (i = 0; i < nfd; ++i) {
                ((Event *) epoll_events[i].data.ptr)->Call();
            }

            //处理自定义事件
            this->customEventManger->ProcEvents();

            //处理时间事件
            this->timeEventManeger->ProcTimeEvent();

        }
    }





#else

public:
    FDS fds;

    void InitFDS(){
        FD_ZERO(&fds.read_fd);
        FD_ZERO(&fds.write_fd);
        FD_ZERO(&fds._read_fd);
        FD_ZERO(&fds._read_fd);
    }

    void LoadEventMap(SOCKET fd, CallBack call_back) {

        Event *accepter = &this->events[cut_index--];

        accepter->data = new MiddleData(fd, call_back);

        accepter->SetSrcFd(&fds);

        accepter->customEventManger = this->customEventManger;

        accepter->el = this;

        accepter->Set(fd, SelectEvent::Read, std::bind(&EventLoop::Accept, this, std::placeholders::_1));
    }



    void Accept(Event *ev) {

        auto *data_ptr = (MiddleData *) ev->data;

        int i;

        for (i = 0; i < cut_index; ++i) {
            if (this->events[i].statu == EventStatu::Free) break;
        }

        if (i == cut_index) {
            std::cout << "[warning]>> max events limited" << std::endl;
            return;
        }

        SOCKET connfd = accept(data_ptr->fd, NULL, NULL);
        if (connfd < 0) {
            std::cout << "[ERROR]>> accept err" << std::endl;
            return;
        }

        printf("accept a clinet %d\n", connfd);
        if (setnonblocking(connfd) == -1) {
            printf("[ERROR]>> clinet %d set nonblock err\n", connfd);
            closesocket(connfd);
            return;
        }

        auto e = &this->events[i];
        e->SetSrcFd(&this->fds);
        e->customEventManger = this->customEventManger;
        e->el = this;
        e->Set(connfd, SelectEvent::Read, data_ptr->callBack);
    }


    int GetEventByFd(int s) {
        int i;
        for (i = 0; i <= MAX_COUNT; ++i) {
            if (this->events[i].fd == s && this->events[i].statu == EventStatu::Using) break;
        }

        if (i > MAX_COUNT) {
            std::cout << "[warning]>> max events limited" << std::endl;
            return -1;
        } else {
            return i;
        }
    }


    void Run() {
        int ret;
        running = true;
        while (running) {
            FD_ZERO(&this->fds._write_fd);
            FD_ZERO(&this->fds._read_fd);
            this->fds._read_fd = this->fds.read_fd;
            this->fds._write_fd = this->fds.write_fd;

            struct timeval tv;
            long now_sec, now_ms;
            TimeEvent *te = this->timeEventManeger->GetNearestEvent();
            if(te){
                GetTime(&now_sec, &now_ms);
                tv.tv_sec = te->when_sec - now_sec;
                if(te->when_ms < now_ms){
                    tv.tv_usec = (te->when_ms + 1000 - now_ms)*1000;
                    tv.tv_sec--;
                } else {
                    tv.tv_usec = (te->when_ms - now_ms)*1000;
                }
                if(tv.tv_sec < 0) tv.tv_sec = 0;
                if(tv.tv_usec < 0) tv.tv_usec = 0;
            } else {
                tv.tv_sec = 0;
                tv.tv_usec = 0;
            }

            ret = select(0, &this->fds._read_fd, &this->fds._write_fd, NULL, &tv);//最后一个参数为NULL，一直等待，直到有数据过来,客户端断开也会触发读/写状态，然后判断recv返回值是否为0，为0这说明客户端断开连接
            if (ret == SOCKET_ERROR) {
                printf("[ERROR]>> select err!");
                WSACleanup();
                exit(-1);
            }

            if (ret > 0) {

                for (int j = 0; j < this->fds._read_fd.fd_count; ++j) {
                    SOCKET s = this->fds._read_fd.fd_array[j];
                    if(FD_ISSET(s, &this->fds._read_fd)){
                        int index = GetEventByFd(s);
                        if (index == -1) {
                            printf("[ERROR]>> find Event Err!");
                            WSACleanup();
                            exit(-1);
                        }
                        (&this->events[index])->Call();
                    }
                }


                for (int k = 0; k < this->fds._write_fd.fd_count; ++k) {
                    SOCKET s = this->fds._write_fd.fd_array[k];
                    if(FD_ISSET(s, &this->fds._write_fd)){
                        int index = GetEventByFd(s);
                        if (index == -1) {
                            printf("[ERROR]>> find Event Err!");
                            WSACleanup();
                            exit(-1);
                        }
                        (&this->events[index])->Call();
                    }
                }


//                for (int i = 0; i <= MAX_COUNT; ++i) {
//                    if (FD_ISSET(i, &this->fds._read_fd) || FD_ISSET(i, &this->fds._write_fd)) {
//                        int index = GetEventByFd(i);
//                        if (index == -1) {
//                            printf("[ERROR]>> find Event Err!");
//                            WSACleanup();
//                            exit(-1);
//                        }
//                        (&this->events[index])->Call();
//                    }
//                }
            }

            //处理自定义事件
            this->customEventManger->ProcEvents();

            //处理时间事件
            this->timeEventManeger->ProcTimeEvent();
        }
    }

#endif
};


#endif //EVENTLOOP_EVENTLOOP_H
