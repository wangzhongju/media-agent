#pragma once

#include "object_meta.h"

#include <stdint.h>

#include <string>
#include <vector>

struct CBaseMeta {
    bool eosFlag = false;
    virtual ~CBaseMeta() = default;
};

struct CFrameMeta : public CBaseMeta {
    uint64_t index = 0;
    int64_t timestampMs = 0;
    int padIndex = 0;
    int camId = 0;
    int taskId = 0;
    int priority = 0;
    std::string taskIdc;
    std::string alertLevels;
    std::vector<CObjectMeta*> objs;
};

