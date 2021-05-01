//
//  RestrictEvents.cpp
//  RestrictEvents
//
//  Copyright © 2020 vit9696. All rights reserved.
//

#include <IOKit/IOService.h>
#include <Headers/kern_api.hpp>
#include <Headers/plugin_start.hpp>
#include <Headers/kern_policy.hpp>

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

static const void *memFindPatch;
static const void *memReplPatch;
static size_t memFindSize;

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
			}
		}
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

		if (!checkKernelArgument("-revnopatch")) {
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

			if (memFindPatch != nullptr) {
				lilu.onPatcherLoadForce([](void *user, KernelPatcher &patcher) {
					KernelPatcher::RouteRequest csRoute("_cs_validate_page", RestrictEventsPolicy::csValidatePage, orgCsValidatePage);
					if (!patcher.routeMultipleLong(KernelPatcher::KernelID, &csRoute, 1)) {
						SYSLOG("rev", "failed to route cs validation pages");
					}
				});
			}
		}
	}
};
