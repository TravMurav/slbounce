slbounce.efi - Tiny implementation of SecureLaunch for qcom WoA
================================================================

On Qualcomm based devices the Hyper-V is launched in EL2 using the Secure-Launch mechanism.

This app attempts to reimplement minimal interaction needed to upload Secure-Launch
application to the Qualcomm's firmware and trigger a takeover.

Since the firmware would check that the payload PE is signed by Microsoft, and the only
Secure-Launch application, signed by MS is `tcblaunch.exe`, you are unlikely to use this
with anything else...

Usage
-----

To test that slbounce can work on your device, run `sltest.efi` with an absolute path to
your `tcblaunch.exe` file. (Get that file from your Windows distribution). `sltest.efi`
will immediately try switching to EL2 and issue PSCI power off command right after. If
your device powers off when you run this command, you can successfully switch to EL2.
If your device reboots or hangs, there is some issue and SL was not successful.

```
fs0:\> sltest.efi path\to\tcblaunch.exe
```

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

