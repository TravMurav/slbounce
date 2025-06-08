slbounce.efi - Alternative implementation of Qualcomm's Secure Launch
=====================================================================

Qualcomm based Windows-on-ARM devices implement special "Secure Launch"
mechanism which allows Windows to perform DRTM and make sure Windows boots into
a "trusted" state even if some "untrusted" code was running on the system prior
to that. Unfortunately, Qualcomm's implementation of that mechanism is based
on letting Windows take over Qualcomm's own hypervisor (QHEE or Gunyah-based)
via vendor locked API. Practially, this means that it's impossible to use EL2
(and thus features like virtualization) without performing "Secure Launch".
(See [Qualcomm Secure Launch](https://github.com/TravMurav/Qcom-Secure-Launch)
for more details on this process.)

slbounce makes use of a Microsoft signed TCB binary shipped with Windows to
perform "Secure Launch" but instead of booting Windows, allows booting arbitrary
software (i.e. Linux-based OS) in EL2 by hooking EFI `ExitBootServices`.

> [!TIP]
> See [Theory of operation](theory_of_operation.md) document for a deeper
> explaination on how slbounce works.

Usage
-----

> [!IMPORTANT]
> To use slbounce or sltest you need to acquire a Microsoft-signed TCB binary
> `tcblaunch.exe`. This binary can usually be found in your Windows installation
> in `/Windows/System32/tcblaunch.exe`.

### sltest.efi

As "Secure Launch" depends on both firmware and signed TCB binary working
correctly (that is, in accordance with slbounce assumptions), a tool is provided
to quickly test that slbounce can take over EL2 correctly.

To test that slbonce can work on your device, run `sltest.efi` with an absolute
path to your `tcblaunch.exe` file. `sltest.efi` will immediately try switching
to EL2 and draw a green line on top of the screen. If you see a green line on
the screen, you can successfully switch to EL2. If your device reboots or hangs,
there is some issue and "Secure Launch" was not successful.

You can run it from EFI shell like this:

```
fs0:\> sltest.efi path\to\tcblaunch.exe
```

### slbounce.efi

slbounce is an efi driver that performs "Secure Launch" as part of EFI
`ExitBootServices` call. To use it, place `tcblaunch.exe` to the root of the FS
with `slbounce.efi`, then load the driver.

In EFI shell you can load it like this:

```
fs0:\> load slbounce.efi
```

Alternatively, you can use features of your bootloader to load the driver as
part of boot process, for example systemd-boot will automatically load it if you
place the driver in `/EFI/systemd/drivers/slbouncea64.efi` (where `aa64` is the
cpu architecture suffix).

The driver will hook `ExitBootServices` with it's own code which will perform
"Secure Launch" as the last step of EBS. This means that your bootloader (i.e.
Linux's efi-stub) will see the CPU switching from EL1 to EL2 when it returns
from EBS. If "Secure Launch" fails, the device will likely hang or reboot.

#### Linux-specific DeviceTree modifications

Linux requires some changes to the device DeviceTree to correctly boot in EL2.
Starting from Linux 6.16, upstream provides EL2-specific DT overlays for
supported chipsets as well as `-el2` variants of dtb files for most WoA devices.

For prior kernel versions, dtbo sources are provided in this repository and you
can create a `-el2` dtb yourself by applying the overlay manually. For example:

```
fdtoverlay \
	-i /boot/sc8280xp-lenovo-thinkpad-x13s.dtb \
	-o /boot/sc8280xp-lenovo-thinkpad-x13s-el2.dtb \
	./out/dtbo/sc8280xp-el2.dtbo
```

In default configuration, slbounce will try to detect if loaded DeviceTree is
usable in EL2. This is helpful if you wish to "dual-boot" Linux in EL1 and EL2
modes as some Linux drivers rely on Qualcomm's hypervisor providing certain
services. This way you can add two menu items in your bootloader, sepcifying
"normal" and "EL2" devicetree in each.

> [!TIP]
> You can disable the DTB detection with a compilation flag (see below) if you
> want slbounce to always switch to EL2. This may be useful if you want to use
> an OS different from Linux.

### dtbhack.efi

> [!NOTE]
> dtbhack is currently only useful to boot modem cpu on sc7180 devices, you
> should ignore this tool in most cases.

dtbhack is a set of experimental hacks that allow modifying the dtb at runtime
to experiment with "helping" linux boot in EL2. It's features include applying
DT overlays and installing the devicetree into the EFI system table, as well
as performing some other pre-boot hacks. As of Linux 6.16 most of those hacks
are not necessary anymore.

To use it, do:
```
fs0:\> dtbhack.efi path\to\your.dtb
```

If you wish to use overlays, do:
```
fs0:\> dtbhack.efi path\to\your.dtb dtbo\symbols.dtbo dtbo\overlay1.dtbo ...
```

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

You can enable extended debugging messages by adding `DEBUG=1` to make cmdline.
To make slbounce unconditionally switch to EL2 instead of trying to guess based
on the loaded dtb, add `SLBOUNCE_ALWAYS_SWITCH=1`.

You can also build optional dtbo blobs:

```
make dtbs
```

Frequently asked questions
--------------------------

### What are the security implications?

A careful consideration was given to security implications of the results of the
research leading to this implementation. However there is no apparent security
issues within the current Secure-Launch process and to the best of the author's
knowledge this implementation being public does not open any new attack vectors
on the Microsoft Windows security.

**There is no security problems in Qualcomm's firmware** - Qualcomm allows an
third party OS to run in EL2. This means that the ability to run arbitrary code
in EL2 is intended.

**There is no security problems in tcblauch.exe** - Error handling is an
intended and deliberately designed part of the Secure-Launch process. Being able
to inflict an error in tcblaunch.exe initialization implies already controlling
the system at that point. This means that the system and the TPM state is
already compromised and is not trustworthy. Notably, reviewing
[Microsoft Security Servicing Criteria for Windows](https://www.microsoft.com/en-us/msrc/windows-security-servicing-criteria)
suggests that requiring UEFI Secure Boot to be disabled (Which is mandatory to
run this app or to tamper with `winload.efi`) doesn't meet the servicing
criteria since the security is manually broken by the user.

Thus in both cases the only "Security" that could be broken is "Security of the
vendor lock-in solution". Author assumes in good faith that no vendor lock-in
was intended with Secure-Launch on general-purpose Qualcomm-based computers and
thus doesn't feel the need to notify abovementioned parties for this non-issue.

### Is this implementation perfectly correct?

No. This implementation is a best effort attempt and might contain some
oversights compared to the intended Secure-Launch process. However the fact that
slbounce works on multiple generations of Qualcomm based devices suggests that
this implementation is very close to being correct. Nonetheless no correctness
guarantees are given and using this software might have various issues on
specific devices.

### Can this be used on android devices?

No. Qualcomm QHEE checks a devcfg flag that allows Secure Launch. This flag is
only set on devices that were shipped with Windows installed. Thus, since it's
not set on Android devices, you can't use Secure Launch and slbounce.

License
-------

Source code files are marked with SPDX license identifiers. A license of choice
for this project code is 3-Clause BSD License.

Note that dependencies of this project may use different licesnses:

- arm64-sysreg-lib: MIT License
- gnu-efi: 2-Clause BSD License
- dtc (libfdt): 2-Clause BSD License

