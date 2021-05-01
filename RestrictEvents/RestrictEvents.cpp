//
//  RestrictEvents.cpp
//  RestrictEvents
//
//  Copyright © 2020 vit9696. All rights reserved.
//

#include <IOKit/IOService.h>
#include <Headers/kern_api.hpp>
#include <Headers/kern_devinfo.hpp>
#include <Headers/plugin_start.hpp>
#include <Headers/kern_policy.hpp>

extern "C" {
#include <i386/pmCPU.h>
}


static const char *bootargOff[] {
	"-revoff"
};

static const char *bootargDebug[] {
	"-revdbg"
};

static const char *bootargBeta[] {
	"-revbeta"
};

static bool verboseProcessLogging;
static mach_vm_address_t orgCsValidatePage;

static const char *dscPath;
static const void *memFindPatch;
static const void *memReplPatch;
static size_t memFindSize;

static const char *cpuFindPatch;
static size_t cpuFindSize;
static uint32_t cpuReplPatch[12];
static size_t cpuReplSize;

static bool needsCpuNamePatch;
static bool needsUnlockCoreCount;
static uint8_t findUnlockCoreCount[] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0F, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x1C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
static uint8_t replUnlockCoreCount[] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0F, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x1C };
static pmCallBacks_t pmCallbacks;

struct RestrictEventsPolicy {

	/**
	 *  Policy to restrict blacklisted process execution
	 */
	static int policyCheckExecve(kauth_cred_t cred, struct vnode *vp, struct vnode *scriptvp, struct label *vnodelabel, struct label *scriptlabel, struct label *execlabel, struct componentname *cnp, u_int *csflags, void *macpolicyattr, size_t macpolicyattrlen) {

		static const char *procBlacklist[] {
			"/System/Library/CoreServices/ExpansionSlotNotification",
			"/System/Library/CoreServices/MemorySlotNotification",
			"/usr/libexec/firmwarecheckers/eficheck/eficheck",
		};

		char pathbuf[MAXPATHLEN];
		int len = MAXPATHLEN;
		int err = vn_getpath(vp, pathbuf, &len);

		if (err == 0) {
			// Uncomment for more verbose output.
			DBGLOG_COND(verboseProcessLogging, "rev", "got request %s", pathbuf);

			for (auto &proc : procBlacklist) {
				if (strcmp(pathbuf, proc) == 0) {
					DBGLOG("rev", "restricting process %s", pathbuf);
					return EPERM;
				}
			}
		}

		return 0;
	}

	/**
	 *  Handler to patch userspace
	 */
	static void csValidatePage(vnode *vp, memory_object_t pager, memory_object_offset_t page_offset, const void *data, int *validated_p, int *tainted_p, int *nx_p) {
		FunctionCast(csValidatePage, orgCsValidatePage)(vp, pager, page_offset, data, validated_p, tainted_p, nx_p);
		char path[PATH_MAX];
		int pathlen = PATH_MAX;
		if (vn_getpath(vp, path, &pathlen) == 0) {
			//DBGLOG("rev", "csValidatePage %s", path);

			if (memFindPatch != nullptr && UNLIKELY(strcmp(path, "/System/Applications/Utilities/System Information.app/Contents/MacOS/System Information") == 0)) {
				if (UNLIKELY(KernelPatcher::findAndReplace(const_cast<void *>(data), PAGE_SIZE, memFindPatch, memFindSize, memReplPatch, memFindSize))) {
					DBGLOG("rev", "patched %s in System Information.app", reinterpret_cast<const char *>(memFindPatch));
					return;
				}
			} else if (cpuReplPatch[0] != '\0' && UNLIKELY(strcmp(path, dscPath) == 0)) {
				if (UNLIKELY(KernelPatcher::findAndReplace(const_cast<void *>(data), PAGE_SIZE, cpuFindPatch, cpuFindSize, cpuReplPatch, cpuReplSize))) {
					DBGLOG("rev", "patched %s in AppleSystemInfo", reinterpret_cast<const char *>(cpuFindPatch));
					return;
				} else if (needsUnlockCoreCount && UNLIKELY(KernelPatcher::findAndReplace(const_cast<void *>(data), PAGE_SIZE, findUnlockCoreCount, sizeof(findUnlockCoreCount), replUnlockCoreCount, sizeof(replUnlockCoreCount)))) {
					DBGLOG("rev", "patched core count in AppleSystemInfo");
					return;
				}
			}
		}
	}

	/**
	 * Return true when CPU brand string patch is needed
	 */
	static bool needsCpuNamePatch() {
		// Default to true on non-Intel
		uint32_t b = 0, c = 0, d = 0;
		CPUInfo::getCpuid(0, 0, nullptr, &b, &c, &d);
		int patchCpu = b != CPUInfo::signature_INTEL_ebx || c != CPUInfo::signature_INTEL_ecx || d != CPUInfo::signature_INTEL_edx;
		PE_parse_boot_argn("revcpu", &patchCpu, sizeof(patchCpu));
		return patchCpu != 0;
	}

	/**
	 * Compute CPU brand string patch
	 */
	static void calculatePatchedBrandString() {
		if (!PE_parse_boot_argn("revcpuname", cpuReplPatch, sizeof(cpuReplPatch))) {
			CPUInfo::getCpuid(0x80000002, 0, &cpuReplPatch[0], &cpuReplPatch[1], &cpuReplPatch[2], &cpuReplPatch[3]);
			CPUInfo::getCpuid(0x80000003, 0, &cpuReplPatch[4], &cpuReplPatch[5], &cpuReplPatch[6], &cpuReplPatch[7]);
			CPUInfo::getCpuid(0x80000004, 0, &cpuReplPatch[8], &cpuReplPatch[9], &cpuReplPatch[10], &cpuReplPatch[11]);
		}

		char *brandStr = reinterpret_cast<char *>(&cpuReplPatch[0]);
		brandStr[sizeof(cpuReplPatch) - 1] = '\0';
		if (brandStr[0] == '\0') return;
		cpuReplSize = strlen(brandStr) + 1;

		pmKextRegister(PM_DISPATCH_VERSION, NULL, &pmCallbacks);
		uint8_t cc = 0;
		auto core = pmCallbacks.GetPkgRoot()->cores;
		while (core != nullptr) {
			if (core->lcpus) cc++;
			core = core->next_in_pkg;
		}

		DBGLOG("rev", "requested to patch CPU name to %s with %u", brandStr, cc);

		switch (cc) {
			case 2:
				cpuFindPatch = "Dual-Core Intel Core i5";
				break;
			case 4:
				cpuFindPatch = "Quad-Core Intel Core i5";
				break;
			case 6:
				cpuFindPatch = "6-Core Intel Core i5";
				break;
			case 8:
				cpuFindPatch = "8-Core Intel Xeon W";
				break;
			case 10:
				cpuFindPatch = "10-Core Intel Xeon W";
				break;
			case 12:
				cpuFindPatch = "12-Core Intel Xeon W";
				break;
			case 14:
				cpuFindPatch = "14-Core Intel Xeon W";
				break;
			case 16:
				cpuFindPatch = "16-Core Intel Xeon W";
				break;
			case 18:
				cpuFindPatch = "18-Core Intel Xeon W";
				break;
			case 24:
				cpuFindPatch = "24-Core Intel Xeon W";
				break;
			case 28:
				cpuFindPatch = "28-Core Intel Xeon W";
				break;
			default:
				replUnlockCoreCount[16] = cc;
				needsUnlockCoreCount = true;
				cpuFindPatch = "28-Core Intel Xeon W";
				break;
		}

		cpuFindSize = strlen(cpuFindPatch) + 1;
	}

	/**
	 *  Default dummy BSD init policy
	 */
	static void policyInitBSD(mac_policy_conf *conf) {
		DBGLOG("rev", "init bsd policy on %u", getKernelVersion());
	}

	/**
	 *  TrustedBSD policy options
	 */
	mac_policy_ops policyOps {
		.mpo_policy_initbsd = policyInitBSD,
		.mpo_vnode_check_exec = policyCheckExecve
	};

	/**
	 *  Full policy name
	 */
#ifdef DEBUG
	static constexpr const char *fullName {xStringify(PRODUCT_NAME) " Kernel Extension " xStringify(MODULE_VERSION) " DEBUG build"};
#else
	static constexpr const char *fullName {xStringify(PRODUCT_NAME) " Kernel Extension " xStringify(MODULE_VERSION)};
#endif

	/**
	 *  Policy controller
	 */
	Policy policy;

	/**
	 Policy constructor.
	 */
	RestrictEventsPolicy() : policy(xStringify(PRODUCT_NAME), fullName, &policyOps) {}
};

static RestrictEventsPolicy restrictEventsPolicy;

PluginConfiguration ADDPR(config) {
	xStringify(PRODUCT_NAME),
	parseModuleVersion(xStringify(MODULE_VERSION)),
	LiluAPI::AllowNormal | LiluAPI::AllowInstallerRecovery | LiluAPI::AllowSafeMode,
	bootargOff,
	arrsize(bootargOff),
	bootargDebug,
	arrsize(bootargDebug),
	bootargBeta,
	arrsize(bootargBeta),
	KernelVersion::MountainLion,
	KernelVersion::BigSur,
	[]() {
		DBGLOG("rev", "restriction policy plugin loaded");
		verboseProcessLogging = checkKernelArgument("-revproc");
		restrictEventsPolicy.policy.registerPolicy();

		if (!checkKernelArgument("-revnopatch") && (lilu.getRunMode() & LiluAPI::RunningNormal) != 0) {
			auto di = BaseDeviceInfo::get();
			// Rename existing values to invalid ones to avoid matching.
			if (strcmp(di.modelIdentifier, "MacPro7,1") == 0) {
				memFindPatch = "MacPro7,1";
				memReplPatch = "HacPro7,1";
				memFindSize  = sizeof("MacPro7,1");
				DBGLOG("rev", "detected MP71");
			} else if (strncmp(di.modelIdentifier, "MacBookAir", strlen("MacBookAir")) == 0) {
				memFindPatch = "MacBookAir";
				memReplPatch = "HacBookAir";
				memFindSize  = sizeof("MacBookAir");
				DBGLOG("rev", "detected MBA");
			}

			needsCpuNamePatch = RestrictEventsPolicy::needsCpuNamePatch();
			if (memFindPatch != nullptr || needsCpuNamePatch) {
				dscPath = UserPatcher::getSharedCachePath();;

				lilu.onPatcherLoadForce([](void *user, KernelPatcher &patcher) {
					if (needsCpuNamePatch) RestrictEventsPolicy::calculatePatchedBrandString();
					KernelPatcher::RouteRequest csRoute("_cs_validate_page", RestrictEventsPolicy::csValidatePage, orgCsValidatePage);
					if (!patcher.routeMultipleLong(KernelPatcher::KernelID, &csRoute, 1)) {
						SYSLOG("rev", "failed to route cs validation pages");
					}
				});
			}
		}
	}
};