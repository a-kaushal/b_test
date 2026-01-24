#pragma once
#include <vector>
#include <deque>
#include <string>
#include <memory>
#include "WorldState.h"

// 1. The Generic Task Interface
struct IProfileTask {
    virtual bool Execute(WorldState* state) = 0;
    virtual void OnInterrupt() {} // Called if Priority System overrides this task
    virtual std::string GetName() const = 0;
    virtual ~IProfileTask() = default;
};

// 2. Base Profile Class
class BotProfile {
public:
    std::deque<std::shared_ptr<IProfileTask>> taskQueue;
    WorldState* State = nullptr;
    std::string profileName = "Unnamed Profile";

    virtual ~BotProfile() = default;

    virtual void Setup(WorldState* state) { this->State = state; }
    virtual void Tick() = 0;

    void SetName(std::string name) { profileName = name; }
    void AddTask(std::shared_ptr<IProfileTask> task) { taskQueue.push_back(task); }
};

typedef BotProfile* (*CreateProfileFn)();