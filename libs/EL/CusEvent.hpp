#ifndef CUS_EVENT_HPP
#define CUS_EVENT_HPP

#include <iostream>
#include <functional>
#include <list>
#include <cstring>
#include <vector>

#define pvoid void *

class EventManger;
typedef std::function<void(EventManger *, std::vector<pvoid> args)> Callback;

class EventInfo{
public:
    EventInfo(const std::string &name, const Callback &callback, EventManger *eventManger) : name(name),
                                                                                             callback(callback),
                                                                                             eventManger(eventManger) {}

    EventInfo(const std::string &name, const Callback &callback, EventManger *eventManger, std::vector<pvoid> args) : name(name),
                                                                                                                      callback(
                                                                                                                              callback),
                                                                                                                      eventManger(
                                                                                                                              eventManger),
                                                                                                                      args(args) {}
    std::string name;
    Callback callback;
    EventManger *eventManger;
    std::vector<pvoid> args;
    EventInfo(const EventInfo& eventInfo){
        this->name = eventInfo.name;
        this->args = eventInfo.args;
        this->callback = eventInfo.callback;
        this->eventManger = eventInfo.eventManger;
    }

    void Call(){
        callback(eventManger, args);
    }

    ~EventInfo(){

    }
};

class EventManger{
private:
    std::list<EventInfo *> events;
    std::list<EventInfo *> fired;
public:
    void On(std::string name, Callback callback){
        for(auto e : events){
            if(e->name == name){
                e->callback = callback;
                return;
            }
        }
        events.push_back(new EventInfo(name, callback, this));
    }

    void Remove(std::string name){
        auto f = [&](EventInfo *e){
            if(e->name == name){
                delete e;
                return true;
            }
            return false;
        };
        fired.remove_if(f);
        events.remove_if(f);
    }

    void Emit(std::string name, std::vector<pvoid> args) throw(std::runtime_error) {
        auto e = getEventByName(name);
        if(e == nullptr){
            char buff[50];
            int n =sprintf(buff, "[ERROR]>> can not find event %s to  emit", name.c_str());
            buff[n] = '\0';
            throw std::runtime_error(buff);
        }

        fired.push_back(new EventInfo(e->name, e->callback, e->eventManger, args));
    }

    EventInfo *getEventByName(std::string name){
        for (auto i : events) {
            if(i->name == name) return i;
        }
        return nullptr;
    }

    bool IsFired(std::string name){
        for(auto e : fired){
            if(e->name == name) return true;
        }
        return false;
    }

    void ProcEvents(){
        if(events.size() == 0 || fired.size() == 0) return;
        for (auto e : fired) {
            e->Call();
        }

        fired.remove_if([&](EventInfo *e){
            delete e;
            return true;
        });
    }
};

#endif //CUS_EVENT_HPP
