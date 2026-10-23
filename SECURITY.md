# Security Policy

## Supported versions

Only the latest release receives security fixes.

## Reporting a vulnerability

Please report vulnerabilities privately through GitHub's
[private vulnerability reporting](https://github.com/Shinoed/CpuTemp/security/advisories/new),
not as a public issue. Include the affected version, steps to reproduce and the impact.

You should get a reply within 7 days. Once a fix is released, the advisory is published
and the reporter is credited unless they ask otherwise.

## Scope

- `CpuTempNative.exe`, the setup and the PowerShell helpers (`task.ps1`, `uninstall.ps1`)
- The GitHub Actions build and signing workflow

The PawnIO driver and its IntelMSR module are third-party components; report issues in them
to [namazso/PawnIO](https://github.com/namazso/PawnIO) and
[namazso/PawnIO.Modules](https://github.com/namazso/PawnIO.Modules).
