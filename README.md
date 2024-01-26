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

### sltest.efi

To test that slbounce can work on your device, run `sltest.efi` with an absolute path to
your `tcblaunch.exe` file. (Get that file from your Windows distribution). `sltest.efi`
will immediately try switching to EL2 and issue PSCI power off command right after. If
your device powers off when you run this command, you can successfully switch to EL2.
If your device reboots or hangs, there is some issue and SL was not successful.

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

These include:

- Making a copy of cmd-db data
- Removing zap-shader node

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

