#ifndef AMDHV_HPP
#define AMDHV_HPP

#include <IOKit/IOService.h>

#include "../../include/AMDHVShared.h"

/*
 * Root service of the kext. Matches on IOResources (see Info.plist), probes
 * SVM on start() and hands out user clients.
 *
 * Boot-args:
 *   -amdhv_off          kill switch: the kext loads but does nothing
 *   amdhv_enable_svm=1  turn SVM on (default is probe only)
 *   amdhv_selftest=1    verify EFER/VM_HSAVE_PA on every CPU after enabling
 *   amdhv_loglevel=N    see Log.hpp
 *   amdhv_iolog=N       see Log.hpp
 */
class com_amdhv_AMDHV : public IOService {
    OSDeclareDefaultStructors(com_amdhv_AMDHV)

public:
    bool start(IOService *provider) override;
    void stop(IOService *provider) override;

    /* Refreshes the dynamic fields and copies the current state out. */
    void getInfo(AMDHVInfo *out);
    void getStats(AMDHVStats *out);

    /* Returns false when SVM is not active. */
    bool runSelfTest(uint32_t *ok, uint32_t *total);

private:
    static IOReturn sleepWakeHandler(void *target, void *refCon, UInt32 messageType,
                                     IOService *provider, void *messageArgument,
                                     vm_size_t argSize);

    AMDHVInfo     fInfo;
    IONotifier *fPMNotifier;
    bool        fSelfTest;
};

#endif /* AMDHV_HPP */
