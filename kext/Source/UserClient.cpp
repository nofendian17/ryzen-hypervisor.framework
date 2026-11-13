#include <IOKit/IOLib.h>
#include <IOKit/IOLocks.h>
#include <pexpert/pexpert.h>

#include "Frontend.hpp"
#include "Log.hpp"
#include "AMDHV.hpp"
#include "UserClient.hpp"
#include "SVM.hpp"

#define super IOUserClient

OSDefineMetaClassAndStructors(com_amdhv_AMDHVUserClient, IOUserClient)

bool com_amdhv_AMDHVUserClient::initWithTask(task_t owningTask, void *securityToken,
                                                     UInt32 type)
{
    if (!super::initWithTask(owningTask, securityToken, type))
        return false;

    fOwner   = nullptr;
    fIsAdmin = clientHasPrivilege(securityToken, kIOClientPrivilegeAdministrator) ==
               kIOReturnSuccess;
    /*
     * Read-only methods (info, log, stats) are open to everyone. Methods that
     * change state need root (fIsAdmin).
     */
    return true;
}

bool com_amdhv_AMDHVUserClient::start(IOService *provider)
{
    if (!super::start(provider))
        return false;
    fOwner = OSDynamicCast(com_amdhv_AMDHV, provider);
    if (!fOwner)
        return false;

    AMDHV_STAT_INC(clientOpens);
    AMDHV_DBG("user client opened (admin=%d)", fIsAdmin);
    return true;
}

IOReturn com_amdhv_AMDHVUserClient::clientClose()
{
    if (!isInactive())
        terminate();
    return kIOReturnSuccess;
}

void com_amdhv_AMDHVUserClient::free()
{
    super::free();
}

IOReturn com_amdhv_AMDHVUserClient::externalMethod(uint32_t selector,
                                                           IOExternalMethodArguments *arguments,
                                                           IOExternalMethodDispatch *dispatch,
                                                           OSObject *target, void *reference)
{
    typedef com_amdhv_AMDHVUserClient C;

    static const IOExternalMethodDispatch kMethods[kAMDHVMethodCount] = {
        /* kAMDHVMethodGetInfo */
        { (IOExternalMethodAction)&C::sGetInfo, 0, 0, 0, sizeof(AMDHVInfo) },
        /* kAMDHVMethodReadLog: 1 scalar in (cursor), struct out */
        { (IOExternalMethodAction)&C::sReadLog, 1, 0, 0, sizeof(AMDHVLogChunk) },
        /* kAMDHVMethodSetLogLevel: 1 scalar in (level) */
        { (IOExternalMethodAction)&C::sSetLogLevel, 1, 0, 0, 0 },
        /* kAMDHVMethodGetStats */
        { (IOExternalMethodAction)&C::sGetStats, 0, 0, 0, sizeof(AMDHVStats) },
        /* kAMDHVMethodSelfTest: 2 scalars out (ok, total) */
        { (IOExternalMethodAction)&C::sSelfTest, 0, 0, 2, 0 },
        /* kAMDHVMethodHvControl: 2 scalars in (cmd, value), 2 scalars out (state bits, mode) */
        { (IOExternalMethodAction)&C::sHvControl, 2, 0, 2, 0 },
    };

    static_assert(sizeof(kMethods) / sizeof(kMethods[0]) == kAMDHVMethodCount,
                  "one dispatch entry per selector");

    if (selector >= kAMDHVMethodCount) {
        AMDHV_WARN("user client: unknown selector %u", selector);
        return kIOReturnUnsupported;
    }

    AMDHV_STAT_INC(clientCalls);
    AMDHV_TRACE("user client: selector %u", selector);

    dispatch = const_cast<IOExternalMethodDispatch *>(&kMethods[selector]);
    target   = this;
    return super::externalMethod(selector, arguments, dispatch, target, reference);
}

/* Common prologue of the methods below. */
#define AMDHV_ME()                                                                         \
    auto *me = OSDynamicCast(com_amdhv_AMDHVUserClient, target);                 \
    if (!me || !me->fOwner)                                                              \
        return kIOReturnNotAttached

#define AMDHV_ADMIN()                                                                      \
    if (!me->fIsAdmin)                                                                   \
        return kIOReturnNotPrivileged

IOReturn com_amdhv_AMDHVUserClient::sGetInfo(OSObject *target, void *,
                                                     IOExternalMethodArguments *args)
{
    AMDHV_ME();
    AMDHVInfo info;
    me->fOwner->getInfo(&info);
    bcopy(&info, args->structureOutput, sizeof(info));
    return kIOReturnSuccess;
}

IOReturn com_amdhv_AMDHVUserClient::sReadLog(OSObject *target, void *,
                                                     IOExternalMethodArguments *args)
{
    AMDHV_ME();
    /* Fill IOKit's own output buffer directly, 4 KiB is too much for the kernel stack. */
    amdhv::log::read(args->scalarInput[0], (AMDHVLogChunk *)args->structureOutput);
    return kIOReturnSuccess;
}

IOReturn com_amdhv_AMDHVUserClient::sSetLogLevel(OSObject *target, void *,
                                                         IOExternalMethodArguments *args)
{
    AMDHV_ME();
    AMDHV_ADMIN();
    if (args->scalarInput[0] > AMDHV_LOG_TRACE)
        return kIOReturnBadArgument;

    amdhv::log::setLevel((uint32_t)args->scalarInput[0]);
    AMDHV_SYNC("log level set to %u by user client", (uint32_t)args->scalarInput[0]);
    return kIOReturnSuccess;
}

IOReturn com_amdhv_AMDHVUserClient::sHvControl(OSObject *target, void *,
                                                       IOExternalMethodArguments *args)
{
    AMDHV_ME();
    uint32_t cmd = (uint32_t)args->scalarInput[0], value = (uint32_t)args->scalarInput[1];
    if (cmd == kAMDHVHvSvmRescan) {
        AMDHV_ADMIN();
        uint32_t enabled = amdhv::svm::rescan();
        args->scalarOutput[0] = (amdhv::hv::registered() ? kAMDHVHvStateRegistered : 0) | (amdhv::hv::sysctlTaken() ? kAMDHVHvStateSysctl : 0) | (amdhv::hv::tprShadow() ? kAMDHVHvStateTpr : 0);
        args->scalarOutput[1] = enabled;
        return kIOReturnSuccess;
    }
    if (cmd != kAMDHVHvGet) {
        AMDHV_ADMIN();
        bool ok = cmd == kAMDHVHvMode ? amdhv::hv::setMode(value)
                : cmd == kAMDHVHvSysctl ? amdhv::hv::setSysctl(value != 0)
                : cmd == kAMDHVHvTpr ? amdhv::hv::setTprShadow(value != 0) : false;
        if (!ok)
            return cmd > kAMDHVHvTpr ? kIOReturnBadArgument : (cmd == kAMDHVHvSysctl ? kIOReturnUnsupported : kIOReturnNotReady);
    }
    args->scalarOutput[0] = (amdhv::hv::registered() ? kAMDHVHvStateRegistered : 0) | (amdhv::hv::sysctlTaken() ? kAMDHVHvStateSysctl : 0) | (amdhv::hv::tprShadow() ? kAMDHVHvStateTpr : 0);
    args->scalarOutput[1] = amdhv::hv::mode();
    return kIOReturnSuccess;
}

IOReturn com_amdhv_AMDHVUserClient::sGetStats(OSObject *target, void *,
                                                      IOExternalMethodArguments *args)
{
    AMDHV_ME();
    AMDHVStats stats;
    me->fOwner->getStats(&stats);
    bcopy(&stats, args->structureOutput, sizeof(stats));
    return kIOReturnSuccess;
}

IOReturn com_amdhv_AMDHVUserClient::sSelfTest(OSObject *target, void *,
                                                      IOExternalMethodArguments *args)
{
    AMDHV_ME();
    AMDHV_ADMIN();
    uint32_t ok = 0, total = 0;
    me->fOwner->runSelfTest(&ok, &total);
    args->scalarOutput[0] = ok;
    args->scalarOutput[1] = total;
    return kIOReturnSuccess;
}
