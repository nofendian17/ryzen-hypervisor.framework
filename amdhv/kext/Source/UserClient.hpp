#ifndef AMDHV_USERCLIENT_HPP
#define AMDHV_USERCLIENT_HPP

#include <IOKit/IOUserClient.h>

class com_amdhv_AMDHV;

/*
 * One instance per IOServiceOpen() from userland (amd-info, ...). Diagnostics only:
 * CPU/SVM info, the kernel log ring, event counters, a self test and run-time control
 * of the native front end (mode, kern.hv_support, TPR shadow, SVM rescan). The VM work
 * itself goes through the Hypervisor.framework trap tables (Frontend.cpp).
 */
class com_amdhv_AMDHVUserClient : public IOUserClient {
    OSDeclareDefaultStructors(com_amdhv_AMDHVUserClient)

public:
    bool initWithTask(task_t owningTask, void *securityToken, UInt32 type) override;
    bool start(IOService *provider) override;
    IOReturn clientClose() override;
    void free() override;
    IOReturn externalMethod(uint32_t selector, IOExternalMethodArguments *arguments,
                            IOExternalMethodDispatch *dispatch, OSObject *target,
                            void *reference) override;

private:
    static IOReturn sGetInfo(OSObject *target, void *reference, IOExternalMethodArguments *args);
    static IOReturn sReadLog(OSObject *target, void *reference, IOExternalMethodArguments *args);
    static IOReturn sSetLogLevel(OSObject *target, void *reference,
                                 IOExternalMethodArguments *args);
    static IOReturn sGetStats(OSObject *target, void *reference, IOExternalMethodArguments *args);
    static IOReturn sSelfTest(OSObject *target, void *reference, IOExternalMethodArguments *args);
    static IOReturn sHvControl(OSObject *target, void *reference, IOExternalMethodArguments *args);

    com_amdhv_AMDHV   *fOwner;
    bool                       fIsAdmin;
};

#endif /* AMDHV_USERCLIENT_HPP */
