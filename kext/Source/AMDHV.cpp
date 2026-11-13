#include <IOKit/IOLib.h>
#include <IOKit/IOMessage.h>
#include <pexpert/pexpert.h>

#include "Frontend.hpp"
#include "Log.hpp"
#include "AMDHV.hpp"
#include "SVM.hpp"

#define super IOService

#define AMDHV_VERSION "0.1.0"

OSDefineMetaClassAndStructors(com_amdhv_AMDHV, IOService)

namespace {

bool bootArgFlag(const char *name)
{
    uint32_t v = 0;
    return PE_parse_boot_argn(name, &v, sizeof(v));
}

bool bootArgValue(const char *name, uint32_t *out)
{
    return PE_parse_boot_argn(name, out, sizeof(*out));
}

} // namespace

bool com_amdhv_AMDHV::start(IOService *provider)
{
    /* Kill switch first, before we touch anything. Plain IOLog: our own log is not up yet. */
    if (bootArgFlag("-amdhv_off")) {
        IOLog("AMDHV: boot-arg -amdhv_off present, not starting\n");
        return false;
    }

    if (!super::start(provider))
        return false;

    amdhv::log::init();
    AMDHV_INFO("AMDHV " AMDHV_VERSION " (ABI %d) built " __DATE__ " " __TIME__, AMDHV_ABI_VERSION);

    amdhv::svm::probe(&fInfo, true);

    AMDHV_INFO("cpus=%u amd=%u family=0x%x model=0x%x step=%u", fInfo.nCpus, fInfo.vendorIsAmd,
             fInfo.cpuFamily, fInfo.cpuModel, fInfo.cpuStepping);
    AMDHV_INFO("svm=%u disabledByBios=%u locked=%u rev=%u asids=%u features=0x%x usableCpus=%u",
             fInfo.svmSupported, fInfo.svmDisabledByBios, fInfo.svmLocked, fInfo.svmRevision,
             fInfo.numAsids, fInfo.svmFeatures, fInfo.nCpusSvmUsable);

    uint32_t selftest = 0;
    bootArgValue("amdhv_selftest", &selftest);
    fSelfTest = selftest != 0;

    /* IOPMrootDomain is only forward-declared in the public SDK headers. It derives from
     * IOService with single inheritance, so a reinterpret_cast is safe. */
    IOService *root = reinterpret_cast<IOService *>(getPMRootDomain());
    fPMNotifier = root ? root->registerInterest(gIOPriorityPowerStateInterest,
                                                &com_amdhv_AMDHV::sleepWakeHandler, this)
                       : nullptr;
    if (!fPMNotifier)
        AMDHV_WARN("could not register for sleep/wake notifications");

    registerService();

    if (!fInfo.vendorIsAmd || !fInfo.svmSupported) {
        AMDHV_WARN("no SVM on this CPU, diagnostics only");
    } else if (fInfo.svmDisabledByBios) {
        AMDHV_WARN("SVM is disabled in firmware (VM_CR.SVMDIS), enable 'SVM Mode' in BIOS");
    } else if (fInfo.nCpusSvmUsable != fInfo.nCpus) {
        AMDHV_WARN("SVM is usable on only %u of %u CPUs, refusing to enable", fInfo.nCpusSvmUsable,
                 fInfo.nCpus);
    } else {
        /* Enabling SVM changes CPU state, so it is opt-in. */
        uint32_t enable = 0;
        bootArgValue("amdhv_enable_svm", &enable);
        if (enable) {
            AMDHV_INFO("enabling SVM (EFER.SVME and VM_HSAVE_PA on every CPU)");
            uint32_t n = amdhv::svm::enableAll();
            if (n && fSelfTest) {
                uint32_t ok, total;
                amdhv::svm::verifyAll(&ok, &total);
            }
        } else {
            AMDHV_INFO("probe only; add boot-arg amdhv_enable_svm=1 to enable SVM");
        }
    }
    amdhv::hv::start(this);        /* the native front end, only with boot-arg amdhv_hv=1 */
    return true;
}

void com_amdhv_AMDHV::stop(IOService *provider)
{
    AMDHV_INFO("stopping");
    amdhv::hv::stop();
    if (fPMNotifier) {
        fPMNotifier->remove();
        fPMNotifier = nullptr;
    }
    amdhv::svm::disableAll();
    super::stop(provider);
}

IOReturn com_amdhv_AMDHV::sleepWakeHandler(void *target, void *, UInt32 messageType,
                                                   IOService *, void *, vm_size_t)
{
    com_amdhv_AMDHV *me = static_cast<com_amdhv_AMDHV *>(target);

    switch (messageType) {
    case kIOMessageSystemWillSleep:
        AMDHV_STAT_INC(sleepEvents);
        amdhv::svm::suspend();
        break;
    case kIOMessageSystemHasPoweredOn:
        AMDHV_STAT_INC(wakeEvents);
        if (amdhv::svm::resume() && me->fSelfTest) {
            uint32_t ok, total;
            amdhv::svm::verifyAll(&ok, &total);
        }
        break;
    default:
        break;
    }

    /* Returning success acknowledges the notification. */
    return kIOReturnSuccess;
}

void com_amdhv_AMDHV::getInfo(AMDHVInfo *out)
{
    /* The CPUs are counted at start-up, and on some systems (macOS 26) only one is online then: keep the numbers live. */
    fInfo.nCpus           = amdhv::svm::cpuCount();
    fInfo.nCpusSvmUsable  = amdhv::svm::usableCpuCount();
    fInfo.nCpusSvmActive  = amdhv::svm::activeCpuCount();
    *out = fInfo;
}

void com_amdhv_AMDHV::getStats(AMDHVStats *out)
{
    amdhv::gStats.logLevel = amdhv::log::level();
    *out = amdhv::gStats;
}

bool com_amdhv_AMDHV::runSelfTest(uint32_t *ok, uint32_t *total)
{
    return amdhv::svm::verifyAll(ok, total);
}
