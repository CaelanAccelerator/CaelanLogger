#pragma once
#include <BackendLogger.h>
class AsyncLogger {
public:
    AsyncLogger(size_t bufSize):backend(bufSize), bufSize(bufSize){        
        backend.start();
    }
    ~AsyncLogger() { backend.stop(); }

    ThreadLogger& tls() {
        thread_local ThreadLogger tl(bufSize,&backend);
        return tl;
    }

private:
    BackendLogger backend;
    size_t bufSize;
};
