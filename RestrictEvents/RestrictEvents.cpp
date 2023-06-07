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

#include "SoftwareUpdate.hpp"
#include "vnode_types.hpp"

extern "C" {
#include <i386/cpuid.h>
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

static void *vnodePagerOpsKernel;

static const char *binPathSystemInformation;
static const char binPathSystemInformationLegacy[]   = "/Applications/Utilities/System Information.app/Contents/MacOS/System Information";
static const char binPathSystemInformationCatalina[] = "/System/Applications/Utilities/System Information.app/Contents/MacOS/System Information";
static const char binPathSPMemoryReporter[]          = "/System/Library/SystemProfiler/SPMemoryReporter.spreporter/Contents/MacOS/SPMemoryReporter";
static const char binPathAboutExtension[]            = "/System/Library/ExtensionKit/Extensions/AboutExtension.appex/Contents/MacOS/AboutExtension";

static const char binPathDiskArbitrationAgent[]     = "/System/Library/Frameworks/DiskArbitration.framework/Versions/A/Support/DiskArbitrationAgent";

static bool enableMemoryUiPatching;
static bool enablePciUiPatching;
static bool enableCpuNamePatching;
static bool enableDiskArbitrationPatching;
static bool enableAssetPatching;
static bool enableSbvmmPatching;
static bool enableF16cPatching;

static bool verboseProcessLogging;
static mach_vm_address_t orgCsValidateFunc;

static const void *modelFindPatch;
static const void *modelReplPatch;
static size_t modelFindSize;

static bool needsMemPatch;
static const char memFindPatch[] = "MacBookAir\0MacBookPro10";
static const char memReplPatch[] = "HacBookAir\0HacBookPro10";

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

static uint8_t findDiskArbitrationPatch[] = { 0x83, 0xF8, 0x02 };
static uint8_t replDiskArbitrationPatch[] = { 0x83, 0xF8, 0x0F };

const char *procBlacklist[10] = {};

struct RestrictEventsPolicy {

	/**
	 *  Policy to restrict blacklisted process execution
	 */
	static int policyCheckExecve(kauth_cred_t cred, struct vnode *vp, struct vnode *scriptvp, struct label *vnodelabel, struct label *scriptlabel, struct label *execlabel, struct componentname *cnp, u_int *csflags, void *macpolicyattr, size_t macpolicyattrlen) {
		char pathbuf[MAXPATHLEN];
		int len = MAXPATHLEN;
		int err = vn_getpath(vp, pathbuf, &len);

		if (err == 0) {
			// Uncomment for more verbose output.
			DBGLOG_COND(verboseProcessLogging, "rev", "got request %s", pathbuf);

			for (auto &proc : procBlacklist) {
				if (proc == nullptr) break;
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

			//
			// Mountain Lion only has the MacBookAir whitelist in System Information.
			// Mavericks has the MacBookAir/MacBookPro10 whitelist in System Information and SPMemoryReporter.
			// Yosemite and newer have the MacBookAir/MacBookPro10 whitelist in System Information, SPMemoryReporter, and AppleSystemInfo.framework.
			//
			if (modelFindPatch != nullptr && getKernelVersion() >= KernelVersion::Ventura && UNLIKELY(strcmp(path, binPathAboutExtension) == 0)) {
				if (UNLIKELY(KernelPatcher::findAndReplace(const_cast<void *>(data), size, modelFindPatch, modelFindSize, modelReplPatch, modelFindSize))) {
					DBGLOG("rev", "patched %s in AboutExtension", reinterpret_cast<const char *>(modelFindPatch));
					return;
				}
			} else if (modelFindPatch != nullptr && UNLIKELY(strcmp(path, binPathSystemInformation) == 0)) {
				if (UNLIKELY(KernelPatcher::findAndReplace(const_cast<void *>(data), size, modelFindPatch, modelFindSize, modelReplPatch, modelFindSize))) {
					DBGLOG("rev", "patched %s in System Information.app", reinterpret_cast<const char *>(modelFindPatch));
					return;
				}
			} else if (needsMemPatch && modelFindPatch != nullptr && getKernelVersion() >= KernelVersion::Mavericks && UNLIKELY(strcmp(path, binPathSPMemoryReporter) == 0)) {
				 if (UNLIKELY(KernelPatcher::findAndReplace(const_cast<void *>(data), size, modelFindPatch, modelFindSize, modelReplPatch, modelFindSize))) {
					 DBGLOG("rev", "patched %s in SPMemoryReporter.spreporter", reinterpret_cast<const char *>(modelFindPatch));
					 return;
				 }
			} else if (enableDiskArbitrationPatching && UNLIKELY(strcmp(path, binPathDiskArbitrationAgent) == 0)) {
				if (UNLIKELY(KernelPatcher::findAndReplace(const_cast<void *>(data), size,
																									 findDiskArbitrationPatch, sizeof(findDiskArbitrationPatch),
																									 replDiskArbitrationPatch, sizeof(findDiskArbitrationPatch)))) {
					DBGLOG("rev", "patched unreadable disk case in DiskArbitrationAgent");
					return;
				}
			} else if (UserPatcher::matchSharedCachePath(path)) {
				// Model check and CPU name may exist in the same page in AppleSystemInfo.
				if (needsMemPatch && getKernelVersion() >= KernelVersion::Yosemite) {
					if (UNLIKELY(KernelPatcher::findAndReplace(const_cast<void *>(data), size, memFindPatch, sizeof(memFindPatch), memReplPatch, sizeof(memFindPatch)))) {
						DBGLOG("rev", "patched model whitelist in AppleSystemInfo");
					}
				}

				if (cpuReplSize > 0) {
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
	}

	/**
	 *  Handler to patch userspace for Big Sur and newer.
	 */
	static void csValidatePageBigSur(vnode_t vp, memory_object_t pager, memory_object_offset_t page_offset, const void *data, int *validated_p, int *tainted_p, int *nx_p) {
		FunctionCast(csValidatePageBigSur, orgCsValidateFunc)(vp, pager, page_offset, data, validated_p, tainted_p, nx_p);
		performReplacements(vp, data, PAGE_SIZE);
	}

	/**
	 *  Handler to patch userspace for Sierra to Catalina.
	 */
	static void csValidateRangeSierra(vnode_t vp, memory_object_t pager, memory_object_offset_t offset, const void *data, vm_size_t size, unsigned *result) {
		FunctionCast(csValidateRangeSierra, orgCsValidateFunc)(vp, pager, offset, data, size, result);
		performReplacements(vp, data, size);
	}

	/**
	 *  Handler to patch userspace for Mountain Lion to El Capitan.
	 */
	static bool csValidatePageMountainLion(void *blobs, memory_object_kernel_t pager, memory_object_offset_t page_offset, const void *data, int *tainted) {
		bool result = FunctionCast(csValidatePageMountainLion, orgCsValidateFunc)(blobs, pager, page_offset, data, tainted);
		if (pager != nullptr && pager->mo_pager_ops == vnodePagerOpsKernel)
			performReplacements(reinterpret_cast<vnode_pager_t>(pager)->vnode_handle, data, PAGE_SIZE);
		return result;
	}

	static bool readNvramVariable(const char *fullName, const char16_t *unicodeName, const EFI_GUID *guid, void *dst, size_t max) {
		// First try the os-provided NVStorage. If it is loaded, it is not safe to call EFI services.
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

	static void getBlockedProcesses(BaseDeviceInfo *info) {
		// Updates procBlacklist with list of processes to block
		char duip[128] { "auto" };
		if (PE_parse_boot_argn("revblock", duip, sizeof(duip))) {
			DBGLOG("rev", "read revblock from boot-args");
		} else if (readNvramVariable(NVRAM_PREFIX(LILU_VENDOR_GUID, "revblock"), u"revblock", &EfiRuntimeServices::LiluVendorGuid, duip, sizeof(duip))) {
			DBGLOG("rev", "read revblock from NVRAM");
		}

		char *value = reinterpret_cast<char *>(&duip[0]);
		value[sizeof(duip) - 1] = '\0';
		size_t i = 0;

		// Disable notification prompts for mismatched memory configuration on MacPro7,1
		if (strcmp(info->modelIdentifier, "MacPro7,1") == 0) {
			if (strstr(value, "pci", strlen("pci")) || strstr(value, "auto", strlen("auto"))) {
				if (getKernelVersion() >= KernelVersion::Catalina) {
					DBGLOG("rev", "disabling PCIe & memory notifications");
					procBlacklist[i++] = (char *)"/System/Library/CoreServices/ExpansionSlotNotification";
					procBlacklist[i++] = (char *)"/System/Library/CoreServices/MemorySlotNotification";
				}
			}
		}

		// MacBookPro9,1 and MacBookPro10,1 GMUX fails to switch with 'displaypolicyd' active in Big Sur and newer
		if (strstr(value, "gmux", strlen("gmux"))) {
			if (getKernelVersion() >= KernelVersion::BigSur) {
				DBGLOG("rev", "disabling displaypolicyd");
				procBlacklist[i++] = (char *)"/usr/libexec/displaypolicyd";
			}
		}

		// Metal 1 GPUs will hard crash when 'mediaanalysisd' is active on Ventura and newer
		if (strstr(value, "media", strlen("media"))) {
			if (getKernelVersion() >= KernelVersion::Ventura) {
				DBGLOG("rev", "disabling mediaanalysisd");
				procBlacklist[i++] = (char *)"/System/Library/PrivateFrameworks/MediaAnalysis.framework/Versions/A/mediaanalysisd";
			}
		}

		for (auto &proc : procBlacklist) {
			if (proc == nullptr) break;
			DBGLOG("rev", "blocking %s", proc);
		}
	}

	static uint32_t getCoreCount() {
		// I think AMD patches bork the topology structure, go over all the packages assuming single CPU systems.
		// REF: https://github.com/acidanthera/bugtracker/issues/1625#issuecomment-831602457
		// REF: https://github.com/AMD-OSX/bugtracker/issues/112
		uint32_t b = 0, c = 0, d = 0;
		CPUInfo::getCpuid(0, 0, nullptr, &b, &c, &d);
		bool isAMD = (b == CPUInfo::signature_AMD_ebx && c == CPUInfo::signature_AMD_ecx && d == CPUInfo::signature_AMD_edx);

		uint32_t cc = 0, pp = 0;
		if (!isAMD) {
			cc = cpuid_info()->core_count;
			DBGLOG("rev", "calculated %u cores from cpuid_info()", cc);
			return cc;
		}

		pmKextRegister(PM_DISPATCH_VERSION, NULL, &pmCallbacks);
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
		}

		return cc;
	}

	/**
	 * Retrieve which system UI is to be enabled
	 */
	static void processEnableUIPatch(BaseDeviceInfo *info) {
		char duip[128] { "auto" };
		if (PE_parse_boot_argn("revpatch", duip, sizeof(duip))) {
			DBGLOG("rev", "read revpatch from boot-args");
		} else if (readNvramVariable(NVRAM_PREFIX(LILU_VENDOR_GUID, "revpatch"), u"revpatch", &EfiRuntimeServices::LiluVendorGuid, duip, sizeof(duip))) {
			DBGLOG("rev", "read revpatch from NVRAM");
		}

		char *value = reinterpret_cast<char *>(&duip[0]);
		value[sizeof(duip) - 1] = '\0';

		if (strstr(value, "memtab", strlen("memtab"))) {
			enableMemoryUiPatching = true;
		}
		if (strstr(value, "pci", strlen("pci"))) {
			enablePciUiPatching = true;
		}
		if (strstr(value, "cpuname", strlen("cpuname"))) {
			enableCpuNamePatching = true;
		}
		if (strstr(value, "diskread", strlen("diskread"))) {
			enableDiskArbitrationPatching = true;
		}
		if (strstr(value, "asset", strlen("asset"))) {
			enableAssetPatching = true;
		}
		if (strstr(value, "sbvmm", strlen("sbvmm"))) {
			enableSbvmmPatching = true;
		}
		if (strstr(value, "f16c", strlen("f16c"))) {
			enableF16cPatching = true;
		}
		if (strstr(value, "auto", strlen("auto"))) {
			// Do not enable Memory and PCI UI patching on real Macs
			// Reference: https://github.com/acidanthera/bugtracker/issues/2046
			enableMemoryUiPatching = info->firmwareVendor != DeviceInfo::FirmwareVendor::Apple;
			enablePciUiPatching = info->firmwareVendor != DeviceInfo::FirmwareVendor::Apple;
			enableCpuNamePatching = true;
		}

		DBGLOG("rev", "revpatch to enable %s", duip);
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

void rerouteHvVmm(KernelPatcher &patcher);
void reroutef16c(KernelPatcher &patcher);

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
	KernelVersion::Sonoma,
	[]() {
		DBGLOG("rev", "restriction policy plugin loaded");
		verboseProcessLogging = checkKernelArgument("-revproc");
		auto di = BaseDeviceInfo::get();
		RestrictEventsPolicy::getBlockedProcesses(&di);
		RestrictEventsPolicy::processEnableUIPatch(&di);
		restrictEventsPolicy.policy.registerPolicy();
		revassetIsSet = enableAssetPatching;
		revsbvmmIsSet = enableSbvmmPatching;

		if ((lilu.getRunMode() & LiluAPI::RunningNormal) != 0) {
			if (enableMemoryUiPatching | enablePciUiPatching) {
				// Rename existing values to invalid ones to avoid matching.
				if (strcmp(di.modelIdentifier, "MacPro7,1") == 0) {
					// on 13.0 MacPro7,1 string literal is inlined, but "MacPro7," will do the matching.
					modelFindPatch = "MacPro7,";
					modelReplPatch = "HacPro7,";
					// partial matching, thus exclude '\0'.
					modelFindSize  = sizeof("MacPro7,") - 1;
					DBGLOG("rev", "detected MP71");
				} else if (strncmp(di.modelIdentifier, "MacBookAir", strlen("MacBookAir")) == 0) {
					needsMemPatch = true;
					modelFindPatch = "MacBookAir";
					modelReplPatch = "HacBookAir";
					modelFindSize  = sizeof("MacBookAir");
					DBGLOG("rev", "detected MBA");
				} else if (strncmp(di.modelIdentifier, "MacBookPro10", strlen("MacBookPro10")) == 0) {
					needsMemPatch = true;
					modelFindPatch = "MacBookPro10";
					modelReplPatch = "HacBookPro10";
					modelFindSize  = sizeof("MacBookPro10");
					DBGLOG("rev", "detected MBP10");
				}

				if (modelFindPatch != nullptr) {
					binPathSystemInformation = getKernelVersion() >= KernelVersion::Catalina ? binPathSystemInformationCatalina : binPathSystemInformationLegacy;
				}
			}

			needsCpuNamePatch = enableCpuNamePatching ? RestrictEventsPolicy::needsCpuNamePatch() : false;
			if (modelFindPatch != nullptr || needsCpuNamePatch || enableDiskArbitrationPatching ||
				(getKernelVersion() >= KernelVersion::Monterey ||
				(getKernelVersion() == KernelVersion::BigSur && getKernelMinorVersion() >= 4))) {
				lilu.onPatcherLoadForce([](void *user, KernelPatcher &patcher) {
					if (needsCpuNamePatch) RestrictEventsPolicy::calculatePatchedBrandString();
					KernelPatcher::RouteRequest csRoute =
						getKernelVersion() >= KernelVersion::BigSur ?
						KernelPatcher::RouteRequest("_cs_validate_page", RestrictEventsPolicy::csValidatePageBigSur, orgCsValidateFunc) :
							(getKernelVersion() >= KernelVersion::Sierra ?
							KernelPatcher::RouteRequest("_cs_validate_range", RestrictEventsPolicy::csValidateRangeSierra, orgCsValidateFunc) :
							KernelPatcher::RouteRequest("_cs_validate_page", RestrictEventsPolicy::csValidatePageMountainLion, orgCsValidateFunc));
					if (getKernelVersion() < KernelVersion::Sierra) {
						vnodePagerOpsKernel = reinterpret_cast<void *>(patcher.solveSymbol(KernelPatcher::KernelID, "_vnode_pager_ops"));
						if (!vnodePagerOpsKernel)
							SYSLOG("rev", "failed to solve _vnode_pager_ops");
					}

					if (!patcher.routeMultipleLong(KernelPatcher::KernelID, &csRoute, 1))
						SYSLOG("rev", "failed to route cs validation pages");
					if ((getKernelVersion() >= KernelVersion::Monterey ||
						(getKernelVersion() == KernelVersion::BigSur && getKernelMinorVersion() >= 4)) &&
						(revsbvmmIsSet || revassetIsSet))
						rerouteHvVmm(patcher);
					if ((enableF16cPatching) &&
						(getKernelVersion() > KernelVersion::Ventura ||
						(getKernelVersion() == KernelVersion::Ventura && getKernelMinorVersion() >= 4)))
						reroutef16c(patcher);
				});
			}
		}
	}
};
