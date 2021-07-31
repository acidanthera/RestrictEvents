//
//  RestrictEvents.cpp
//  RestrictEvents
//
//  Copyright © 2020 vit9696. All rights reserved.
//

#include <IOKit/IOService.h>
#include <Headers/kern_api.hpp>
#include <Headers/kern_devinfo.hpp>
#include <Headers/kern_nvram.hpp>
#include <Headers/kern_efi.hpp>
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

static bool disableMemoryPciManagementPatching;
static bool disableCpuNamePatching;
static bool disableAllPatching;
static bool verboseProcessLogging;
static mach_vm_address_t orgCsValidateFunc;

static const void *memFindPatch;
static const void *memReplPatch;
static size_t memFindSize;

static constexpr size_t CpuSignatureWords = 12;
static const char *cpuFindPatch;
static size_t cpuFindSize;
static uint8_t cpuReplPatch[CpuSignatureWords * sizeof(uint32_t) + 1];
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
	 *  Common userspace replacement code
	 */
	static void performReplacements(vnode_t vp, const void *data, vm_size_t size) {
		char path[PATH_MAX];
		int pathlen = PATH_MAX;
		if (vn_getpath(vp, path, &pathlen) == 0) {
			//DBGLOG("rev", "csValidatePage %s", path);

			if (memFindPatch != nullptr && UNLIKELY(strcmp(path, "/System/Applications/Utilities/System Information.app/Contents/MacOS/System Information") == 0)) {
				if (UNLIKELY(KernelPatcher::findAndReplace(const_cast<void *>(data), size, memFindPatch, memFindSize, memReplPatch, memFindSize))) {
					DBGLOG("rev", "patched %s in System Information.app", reinterpret_cast<const char *>(memFindPatch));
					return;
				}
			} else if (cpuReplSize > 0 && UserPatcher::matchSharedCachePath(path)) {
				if (UNLIKELY(KernelPatcher::findAndReplace(const_cast<void *>(data), size, cpuFindPatch, cpuFindSize, cpuReplPatch, cpuReplSize))) {
					DBGLOG("rev", "patched %s in AppleSystemInfo", reinterpret_cast<const char *>(cpuFindPatch + 1));
					return;
				} else if (needsUnlockCoreCount && UNLIKELY(KernelPatcher::findAndReplace(const_cast<void *>(data), size, findUnlockCoreCount, sizeof(findUnlockCoreCount), replUnlockCoreCount, sizeof(replUnlockCoreCount)))) {
					DBGLOG("rev", "patched core count in AppleSystemInfo");
					return;
				}
			}
		}
	}

	/**
	 *  Handler to patch userspace for Big Sur
	 */
	static void csValidatePage(vnode_t vp, memory_object_t pager, memory_object_offset_t page_offset, const void *data, int *validated_p, int *tainted_p, int *nx_p) {
		FunctionCast(csValidatePage, orgCsValidateFunc)(vp, pager, page_offset, data, validated_p, tainted_p, nx_p);
		performReplacements(vp, data, PAGE_SIZE);
	}

	/**
	 *  Handler to patch userspace prior to Big Sur
	 */
	static void csValidateRange(vnode_t vp, memory_object_t pager, memory_object_offset_t offset, const void *data, vm_size_t size, unsigned *result) {
		FunctionCast(csValidateRange, orgCsValidateFunc)(vp, pager, offset, data, size, result);
		performReplacements(vp, data, size);
	}

	static bool readNvramVariable(const char *fullName, const char16_t *unicodeName, const EFI_GUID *guid, void *dst, size_t max) {
		// Firstry try the os-provided NVStorage. If it is loaded, it is not safe to call EFI services.
		NVStorage storage;
		if (storage.init()) {
			uint32_t size = 0;
			auto buf = storage.read(fullName, size, NVStorage::OptRaw);
			if (buf) {
				// Do not care if the value is a little bigger.
				if (size <= max) {
					memcpy(dst, buf, size);
				}
				Buffer::deleter(buf);
			}

			storage.deinit();

			return buf && size <= max;
		}

		// Otherwise use EFI services if available.
		auto rt = EfiRuntimeServices::get(true);
		if (rt) {
			uint64_t size = max;
			uint32_t attr = 0;
			auto status = rt->getVariable(unicodeName, guid, &attr, &size, dst);

			rt->put();
			return status == EFI_SUCCESS && size <= max;
		}

		return false;
	}

	/**
	 * Return true when CPU brand string patch is needed
	 */
	static bool needsCpuNamePatch() {
		// Default to true on non-Intel
		uint32_t b = 0, c = 0, d = 0;
		CPUInfo::getCpuid(0, 0, nullptr, &b, &c, &d);
		int patchCpu = b != CPUInfo::signature_INTEL_ebx || c != CPUInfo::signature_INTEL_ecx || d != CPUInfo::signature_INTEL_edx;
		if (PE_parse_boot_argn("revcpu", &patchCpu, sizeof(patchCpu)))
			DBGLOG("rev", "read revcpu override from boot-args - %d", patchCpu);
		else if (readNvramVariable(NVRAM_PREFIX(LILU_VENDOR_GUID, "revcpu"), u"revcpu", &EfiRuntimeServices::LiluVendorGuid, &patchCpu, sizeof(patchCpu)))
			DBGLOG("rev", "read revcpu override from NVRAM - %d", patchCpu);
		else
			DBGLOG("rev", "using CPUID-based revcpu value - %d", patchCpu);
		if (patchCpu == 0) return false;

		uint32_t patch[CpuSignatureWords] {};
		if (PE_parse_boot_argn("revcpuname", patch, sizeof(patch))) {
			DBGLOG("rev", "read revcpuname from boot-args");
		} else if (readNvramVariable(NVRAM_PREFIX(LILU_VENDOR_GUID, "revcpuname"), u"revcpuname", &EfiRuntimeServices::LiluVendorGuid, patch, sizeof(patch))) {
			DBGLOG("rev", "read revcpuname from NVRAM");
		} else {
			DBGLOG("rev", "read revcpuname from default");
			CPUInfo::getCpuid(0x80000002, 0, &patch[0], &patch[1], &patch[2], &patch[3]);
			CPUInfo::getCpuid(0x80000003, 0, &patch[4], &patch[5], &patch[6], &patch[7]);
			CPUInfo::getCpuid(0x80000004, 0, &patch[8], &patch[9], &patch[10], &patch[11]);
		}

		char *brandStr = reinterpret_cast<char *>(&patch[0]);
		brandStr[sizeof(patch) - 1] = '\0';
        while (brandStr[0] == ' ') brandStr++;
		if (brandStr[0] == '\0') return false;
		auto len = strlen(brandStr);
		memcpy(&cpuReplPatch[1], brandStr, len);
		cpuReplSize = len + 2;

		DBGLOG("rev", "requested to patch CPU name to %s", brandStr);
		return true;
	}

	static uint32_t getCoreCount() {
		// I think AMD patches bork the topology structure, go over all the packages assuming single CPU systems.
		// REF: https://github.com/acidanthera/bugtracker/issues/1625#issuecomment-831602457
		// REF: https://github.com/AMD-OSX/bugtracker/issues/112
		uint32_t b = 0, c = 0, d = 0;
		CPUInfo::getCpuid(0, 0, nullptr, &b, &c, &d);
		bool isAMD = (b == CPUInfo::signature_AMD_ebx && c == CPUInfo::signature_AMD_ecx && d == CPUInfo::signature_AMD_edx);

		pmKextRegister(PM_DISPATCH_VERSION, NULL, &pmCallbacks);
		uint32_t cc = 0, pp = 0;
		auto pkg = pmCallbacks.GetPkgRoot();
		while (pkg != nullptr) {
			auto core = pkg->cores;
			while (core != nullptr) {
				cc++;
				core = core->next_in_pkg;
			}
			DBGLOG("rev", "calculated %u cores in pkg %u amd %d", cc, pp, isAMD);
			pp++;
			pkg = pkg->next;
			if (!isAMD) break;
		}

		return cc;
	}

	/**
	 * Retrieve which system UI is to be disabled - all, mempci, cpuname, none (default)
	 */
	static void processDisableUIPatch() {
		char duip[128] {};
		if (PE_parse_boot_argn("revnopatch", duip, sizeof(duip))) {
			DBGLOG("rev", "read revnopatch from boot-args");
		} else if (readNvramVariable(NVRAM_PREFIX(LILU_VENDOR_GUID, "revnopatch"), u"revnopatch", &EfiRuntimeServices::LiluVendorGuid, duip, sizeof(duip))) {
			DBGLOG("rev", "read revnopatch from NVRAM");
		}
        
		char *value = reinterpret_cast<char *>(&duip[0]);
		value[sizeof(duip) - 1] = '\0';

		if (strcmp(value, "all") == 0) {
			// Disable all UI patches
			disableAllPatching = true;
		} else if (strcmp(value, "mempci") == 0) {
			disableMemoryPciManagementPatching = true;
		} else if (strcmp(value, "cpuname") == 0) {
			disableCpuNamePatching = true;
		}

		DBGLOG("rev", "revnopatch to disable %s", duip);
	}

	/**
	 * Compute CPU brand string patch
	 */
	static void calculatePatchedBrandString() {
		auto cc = getCoreCount();

		switch (cc) {
			case 1:
				cpuFindPatch = "\0" "Intel Core i5";
				cpuFindSize = sizeof("\0" "Intel Core i5");
				break;
			case 2:
				cpuFindPatch = getKernelVersion() >= KernelVersion::Catalina ? "\0" "Dual-Core Intel Core i5" : "\0" "Intel Core i5";
				cpuFindSize = getKernelVersion() >= KernelVersion::Catalina ? sizeof("\0" "Dual-Core Intel Core i5") : sizeof("\0" "Intel Core i5");
				break;
			case 4:
				cpuFindPatch = getKernelVersion() >= KernelVersion::Catalina ? "\0" "Quad-Core Intel Core i5" : "\0" "Intel Core i5";
				cpuFindSize = getKernelVersion() >= KernelVersion::Catalina ? sizeof("\0" "Quad-Core Intel Core i5") : sizeof("\0" "Intel Core i5");
				break;
			case 6:
				cpuFindPatch = getKernelVersion() >= KernelVersion::Catalina ? "\0" "6-Core Intel Core i5" : "\0" "Intel Core i5";
				cpuFindSize = getKernelVersion() >= KernelVersion::Catalina ? sizeof("\0" "6-Core Intel Core i5") : sizeof("\0" "Intel Core i5");
				break;
			case 8:
				cpuFindPatch = "\0" "8-Core Intel Xeon W";
				cpuFindSize = sizeof("\0" "8-Core Intel Xeon W");
				break;
			case 10:
				cpuFindPatch = "\0" "10-Core Intel Xeon W";
				cpuFindSize = sizeof("\0" "10-Core Intel Xeon W");
				break;
			case 12:
				cpuFindPatch = "\0" "12-Core Intel Xeon W";
				cpuFindSize = sizeof("\0" "12-Core Intel Xeon W");
				break;
			case 14:
				cpuFindPatch = "\0" "14-Core Intel Xeon W";
				cpuFindSize = sizeof("\0" "14-Core Intel Xeon W");
				break;
			case 16:
				cpuFindPatch = "\0" "16-Core Intel Xeon W";
				cpuFindSize = sizeof("\0" "16-Core Intel Xeon W");
				break;
			case 18:
				cpuFindPatch = "\0" "18-Core Intel Xeon W";
				cpuFindSize = sizeof("\0" "18-Core Intel Xeon W");
				break;
			case 24:
				cpuFindPatch = "\0" "24-Core Intel Xeon W";
				cpuFindSize = sizeof("\0" "24-Core Intel Xeon W");
				break;
			case 28:
				cpuFindPatch = "\0" "28-Core Intel Xeon W";
				cpuFindSize = sizeof("\0" "28-Core Intel Xeon W");
				break;
			default:
				cpuFindPatch = "\0" "28-Core Intel Xeon W";
				cpuFindSize = sizeof("\0" "28-Core Intel Xeon W");
				replUnlockCoreCount[16] = cc;
				needsUnlockCoreCount = true;
				break;
		}

		DBGLOG("rev", "chosen %s patch for %u core CPU", cpuFindPatch + 1, cc);
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
	KernelVersion::Monterey,
	[]() {
		DBGLOG("rev", "restriction policy plugin loaded");
		verboseProcessLogging = checkKernelArgument("-revproc");
		RestrictEventsPolicy::processDisableUIPatch();
		restrictEventsPolicy.policy.registerPolicy();

		if ((lilu.getRunMode() & LiluAPI::RunningNormal) != 0) {
			if (!(disableMemoryPciManagementPatching || disableAllPatching)) {
				// Rename existing values to invalid ones to avoid matching.
				auto di = BaseDeviceInfo::get();
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
			}
			
			needsCpuNamePatch = !(disableCpuNamePatching || disableAllPatching) == true ? RestrictEventsPolicy::needsCpuNamePatch() : false;
			if (memFindPatch != nullptr || needsCpuNamePatch) {
				lilu.onPatcherLoadForce([](void *user, KernelPatcher &patcher) {
					if (needsCpuNamePatch) RestrictEventsPolicy::calculatePatchedBrandString();
					KernelPatcher::RouteRequest csRoute =
						getKernelVersion() >= KernelVersion::BigSur ?
						KernelPatcher::RouteRequest("_cs_validate_page", RestrictEventsPolicy::csValidatePage, orgCsValidateFunc) :
						KernelPatcher::RouteRequest("_cs_validate_range", RestrictEventsPolicy::csValidateRange, orgCsValidateFunc);
					if (!patcher.routeMultipleLong(KernelPatcher::KernelID, &csRoute, 1)) {
						SYSLOG("rev", "failed to route cs validation pages");
					}
				});
			}
		}
	}
};
