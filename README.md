slbounce.efi - Tiny implementation of SecureLaunch for qcom WoA
================================================================

On Qualcomm based devices the Hyper-V is launched in EL2 using the SecureLaunch mechanism.

This app attempts to reimplement minimal interaction needed to upload SecureLaunch
application to the Qualcomm's firmware and trigger a takeover.

Since the firmware would check that the payload PE is signed by Microsoft, and the only
SecureLaunch application, signed by MS is `tcblaunch.exe`, you are unlikely to use this
with anything else...

Usage
-----

```
fs0:\> slbounce.efi path\to\tcblaunch.exe
```
