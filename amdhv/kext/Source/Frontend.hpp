/*
 * Frontend.hpp - native mode: the kext registers itself with xnu as the hypervisor provider, in the place of Apple's
 * AppleHV, so that the unmodified Hypervisor.framework talks to this kext directly (no shim, no DYLD_INSERT_LIBRARIES).
 *
 *   amdhv_hv=1        register the trap tables and callbacks: clients can create VMs and vCPUs and run them
 *   amdhv_hv=2        debugging only: answer success to the traps that are not implemented
 *   amdhv_hv_sysctl   write 1 into the variable behind kern.hv_support once the traps are registered (default on)
 *
 * Without amdhv_hv nothing here runs.
 */
#ifndef AMDHV_FRONTEND_HPP
#define AMDHV_FRONTEND_HPP

class com_amdhv_AMDHV;

namespace amdhv {
namespace hv {

/* Registers when boot-arg amdhv_hv is given; harmless otherwise. */
void start(com_amdhv_AMDHV *service);
/* Undoes everything (kext stop). */
void stop();

/* Run-time control, used by the user client (amd-info hv ...). Mode 0 unregisters, 1 and 2 register (or just change the mode). */
bool setMode(uint32_t mode);
bool setSysctl(bool on);
bool setTprShadow(bool on);      /* offer the TPR shadow to VMs created afterwards */
bool tprShadow();
uint32_t mode();
bool registered();
bool sysctlTaken();

} // namespace hv
} // namespace amdhv

#endif
