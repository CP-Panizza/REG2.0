//
// Created by cmj on 20-4-15.
//

#ifndef EVENTLOOP_EVENT_H
#define EVENTLOOP_EVENT_H


#include <utility>
#include "EventLoop.hpp"
#include "CusEvent.hpp"

#define pvoid void *

typedef struct {
    fd_set read_fd;
    fd_set write_fd;
    fd_set _read_fd;
    fd_set _write_fd;
} FDS;

enum EventStatu {
    Free,
    Using
};

enum SelectEvent{
    Read,
    Write
};

class EventLoop;

class Event {
public:
    typedef std::function<void(Event *)> CallBack;
#ifndef _WIN64
    int src_fd; //事件源fd exp: select_fd, epoll_fd
    int fd;
    int events;

    void SetSrcFd(int _src_fd){
        this->src_fd = _src_fd;
    }

    void Set(int _fd, int _event, CallBack _callback) {
        this->fd = _fd;
        this->events = _event;
        this->callBack = _callback;

        struct epoll_event epv;
        memset(&epv, 0, sizeof(epv));
        epv.events = static_cast<uint32_t>(this->events);
        epv.data.ptr = this;
        int op;

        if (this->statu == EventStatu::Using) {
            op = EPOLL_CTL_MOD;
        } else {
            op = EPOLL_CTL_ADD;
            this->statu = EventStatu ::Using;
        }

        if (epoll_ctl(this->src_fd, op, this->fd, &epv) < 0) {
            printf("epoll_ctl failed\n");
        } else {
            printf("epoll_ctl [fd = %d] events[%0x] success\n", this->fd, this->events);
        }
    }

    void Del(){
        if(this->statu == EventStatu::Free) return;
        epoll_ctl(this->src_fd, EPOLL_CTL_DEL, this->fd, nullptr);
        this->statu = EventStatu ::Free;
    }
#else

    SOCKET fd;
    FDS *src_fd;
    SelectEvent events;
    void SetSrcFd(FDS *_src_fd){
        this->src_fd = _src_fd;
    }

    void Set(SOCKET _fd, SelectEvent _event, CallBack _callback) {
        this->fd = _fd;
        this->events = _event;
        this->callBack = _callback;

        if(_event == SelectEvent::Read){
            printf("set before read_count:%d\n", this->src_fd->read_fd.fd_count);
        }

        if(_event == SelectEvent::Read){
            FD_CLR(this->fd, &this->src_fd->write_fd);
            FD_SET(this->fd, &this->src_fd->read_fd);
        } else {
            FD_SET(this->fd, &this->src_fd->write_fd);
            FD_CLR(this->fd, &this->src_fd->read_fd);
        }

        if (this->statu != EventStatu::Using) {
            this->statu = EventStatu ::Using;
        }
    }

    void Del(){
        if(this->statu == EventStatu::Free) return;
        FD_CLR(this->fd, &this->src_fd->write_fd);
        FD_CLR(this->fd, &this->src_fd->read_fd);
        this->statu = EventStatu ::Free;
    }

#endif

    pvoid data;
    char buff[MAXLINE];
    int len;
    CallBack callBack;
    EventStatu statu = EventStatu::Free;
    EventManger *customEventManger;
    EventLoop *el;

    void ClearBuffer() {
        memset(this->buff, 0, sizeof(this->buff));
        this->len = 0;
    }

    void Call() {
        callBack(this);
    }
};


#endif //EVENTLOOP_EVENT_H
