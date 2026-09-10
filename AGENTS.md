# UnrealAgent contributor instructions

This standalone repository is Git-backed. The former WorldDataEngine checkout is a separate Diversion repository; do not change its configuration or reintroduce this plugin there.

- Read README.md and Docs/AgentArchitecture.md before changing implementation.
- Preserve UnrealAgent module names, reflected declarations, serialized fields and protocol contracts.
- Use Unreal-native PascalCase identifiers, Unreal type prefixes, lowercase b for booleans, tabs and Allman braces. Avoid mechanical line wrapping.
- Project-owned source documentation uses Chinese; do not alter third-party ownership notices.
- Project-owned code is MIT-licensed; preserve THIRD_PARTY_NOTICES.md and upstream baseline attribution.
- Never commit Binaries, Intermediate, Saved, node_modules, credentials, account data, user configuration or generated artifacts.
- Runtime configuration belongs in the host project's Saved/UnrealAgent directory. Committed templates use placeholders.
- External Providers depend on MCPCore and ProviderSDK, not MCPEditor.
- Preserve policy, validation, cancellation and audit boundaries. No arbitrary tool execution bypass.
- ACP upgrades require a reviewed lockfile, matching installer SHA-256, actual installation and protocol validation. Never present adapter version as protocol version.
- Run Scripts/check_source_release.py and the narrowest relevant tests. Distinguish static checks, builds, automation and real authenticated sessions.
- Do not infer complete Action coverage from the number of contract entries.
