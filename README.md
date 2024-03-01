slbounce.efi - Tiny implementation of SecureLaunch for qcom WoA
================================================================

On Qualcomm based devices the Hyper-V is launched in EL2 using the Secure-Launch mechanism.

This app attempts to reimplement minimal interaction needed to upload Secure-Launch
application to the Qualcomm's firmware and trigger a takeover.

Since the firmware would check that the payload PE is signed by Microsoft, and the only
Secure-Launch application, signed by MS is `tcblaunch.exe`, you are unlikely to use this
with anything else...

You can find an overview of the implemented process in [Theory of operation](theory_of_operation.md) document.


Usage
-----

### sltest.efi

To test that slbounce can work on your device, run `sltest.efi` with an absolute path to
your `tcblaunch.exe` file. (Get that file from your Windows distribution). `sltest.efi`
will immediately try switching to EL2 and draw a green line on top of the screen. If
you see a green line on the screen, you can successfully switch to EL2. If your device
reboots or hangs, there is some issue and SL was not successful.

```
fs0:\> sltest.efi path\to\tcblaunch.exe
```

### slbounce.efi

To actually boot an OS with Secure-Launch switch to EL2, you can use `slbounce.efi`.

> [!CAUTION]
> Since Secure-Launch failure will crash the firmware in most cases, you must make
> sure you can remove `slbounce.efi` from any boot order without it ever having a
> chance to load.

Make sure `tcblaunch.exe` is placed in the root of the FS, then load the EFI driver:

```
fs0:\> load slbounce.efi
```

The driver will replace `BS->ExitBootServices` in the system table with it's own
function. It will call EBS and perform switch to EL2 right after. Thus your bootloader
(i.e. grub or linux's efi-stub) would experence the cpu swithing to EL2 when it calls
EBS.

If Secure-Launch fails at that point, the device will likely hang or reboot.

Unfortunately due to many firmware-spefic quirks implemented in Linux, it's not as simple
as just booting your OS in EL2 after the SL happened. One would need to preform some
changes to how Linux boots in order to not crash trying to talk to now non-existent
hyp firmware.

### dtbhack.efi

Even though "Making Linux work" is out of scope for this project, since Linux is likely
the most interesting software to run in EL2 on those devices, a quick hack is supplied
in this repo to help with initial testing and bring-up.

`dtbhack.efi` is a very simple app that installs your device DTB into the UEFI system
table and performs minimal (seemingly) necesary hacks to workaround some booting issues.
The tool allows you to apply some dtb overlays. A set of possibly useful dtb overlays is
maintained in `dtbo/` dir. Build them using `make dtbs`.

To use it, do:
```
fs0:\> dtbhack.efi path\to\your.dtb
```

Please note that this will not fix every issue but only attempts to work around the most
boot-critical ones.


Build
-----

Make sure you have submodules:

```
git submodule update --init --recursive
```

Then build the project:

```
make
```

You can also build optional dtbo blobs:

```
make dtbs
```

Frequently asked questions
--------------------------

### What are the security implications?

A careful consideration was given to security implications of the results of the research
leading to this implementation. However there is no apparent security issues within the
current Secure-Launch process and to the best of the author's knowledge this implementation
being public does not open any new attack vectors on the Microsoft Windows security.

**There is no security problems in Qualcomm's firmware** - Qualcomm allows an third party
OS to run in EL2. This means that the ability to run arbitrary code in EL2 is intended.

**There is no security problems in tcblauch.exe** - Error handling is an intended and
deliberately designed part of the Secure-Launch process. Being able to inflict an error in
tcblaunch.exe initialization implies already controlling the system at that point. This
means that the system and the TPM state is already compromised and is not trustworthy.
Notably, reviewing [Microsoft Security Servicing Criteria for Windows](https://www.microsoft.com/en-us/msrc/windows-security-servicing-criteria)
suggests that requiring UEFI Secure Boot to be disabled (Which is mandatory to run this app
or to tamper with `winload.efi`) doesn't meet the servicing criteria since the security is
manually broken by the user.

Thus in both cases the only "Security" that could be broken is "Security of the vendor lock-in
solution". Author assumes in good faith that no vendor lock-in was intended with Secure-Launch
on general-purpose Qualcomm-based computers and thus doesn't feel the need to notify
abovementioned parties for this non-issue.

### Is this implementation perfectly correct?

No. This implementation is a best effort attempt and might contain some oversignts compared
to the intended Secure-Launch process. However the fact that slbounce works on multiple
generations of Qualcomm based devices suggests that this implementation is very close to
being correct. Nonetheless no correctness guarantees are given and using this software might
have various issues on specific devices.


License
-------

Source code files are marked with SPDX license identifiers. A license of choice for this
project code is 3-Clause BSD License.

Note that dependencies of this project may use different licesnses:

- arm64-sysreg-lib: MIT License
- gnu-efi: 2-Clause BSD License
- dtc (libfdt): 2-Clause BSD License

