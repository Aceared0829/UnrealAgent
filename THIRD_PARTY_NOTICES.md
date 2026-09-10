# Third-party notices

## Project-owned code

Project-owned code is distributed under [MIT](LICENSE.md). Existing `Copyright ZhaoZining. All Rights Reserved.` headers identify ownership; the MIT grant applies to that code.

## ue-mcp compatibility data

`Config/ActionContracts.json` and `Tests/Baselines/db-lyon-ue-mcp.json` identify a compatibility baseline from [db-lyon/ue-mcp](https://github.com/db-lyon/ue-mcp), revision `e2d4c455b4bf247aa2df0971ea969c8c3960ba7c`. Upstream contract names and baseline data are not wholly original work. UnrealAgent does not require the upstream plugin to be installed. The upstream license is retained for that material and any derived portions:

MIT License

Copyright (c) 2026 David Bingham

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

## External runtime dependencies

- Unreal Engine headers, libraries and tools are not redistributed. Their use remains governed by Epic's applicable license.
- [Codex ACP](https://github.com/agentclientprotocol/codex-acp) is an external Apache-2.0 adapter. Its dependency tree is pinned in `Scripts/CodexACP.package-lock.json` and downloaded only on installation.
- OpenAI Codex, the ACP SDK, Node.js, npm and transitive packages retain their own licenses. Installed packages contain the corresponding notices; do not strip them when redistributing a runtime.
- Cursor and FFmpeg are optional external tools, not bundled. Users are responsible for their licenses and accounts.

No endorsement by OpenAI, Epic Games, Cursor or upstream maintainers is implied.
