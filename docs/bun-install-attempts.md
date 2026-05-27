# WSL 安装 Bun 尝试记录

## 背景

在 WSL2 DevContainer 环境中为 claude-mem 安装 bun 运行时的完整尝试记录。目标是让 hook 脚本在 WSL 内找到原生 bun，避免跨 WSL/Windows 边界的 stdin 管道断裂问题。

**最后更新**: 2026-05-27

---

## 执行摘要

所有 11 次尝试均因网络限制、权限不足或工具兼容性问题失败。核心瓶颈是 **WSL DevContainer 网络受限**（github.com 不可达，npm registry 的 Node.js 连接超时），且 **DevContainer 缺少 sudo 权限**。后续方向需要绕过网络限制（通过 Windows 侧下载）或放弃安装 bun（改用纯 HTTP 方案）。

---

## 尝试 1: 官方安装脚本

- **为什么做这个尝试**: 这是安装 bun 的标准方式，一行命令即可完成，文档推荐的做法。先试试最简单的路。
- **方法**: 执行 `curl -fsSL https://bun.sh/install | bash`
- **结果**: ❌ 失败
- **原因**: 安装脚本需要 `unzip` 命令来解压下载的 zip，但当前 devcontainer 环境未安装 unzip

---

## 尝试 2: 安装 unzip

- **为什么做这个尝试**: 尝试 1 失败的根因是缺 unzip，补上 unzip 就能走通官方安装脚本。
- **方法**: 执行 `sudo apt-get install -y unzip`
- **结果**: ❌ 失败
- **原因**: DevContainer 环境没有为 `devcontainers` 用户配置 passwordless sudo，非交互式 sudo 需要终端认证，无法在脚本环境中执行

---

## 尝试 3: 通过 npm 全局安装

- **为什么做这个尝试**: npm (`npm install -g bun`) 会下载预编译二进制，不依赖 unzip 解压，且 npm 在 devcontainer 中可用（`npm --version` 返回 9.2.0）。
- **方法**: 执行 `npm install -g bun`
- **结果**: ❌ 超时
- **原因**: WSL 内的 Node.js 连接 `registry.npmjs.org` 超时。后续诊断发现 curl 能访问该地址（200），但 Node.js 的连接请求始终超时，可能是 DNS 解析路径不同或 devcontainer 网络栈限制

---

## 尝试 4: Python 从 GitHub API 下载

- **为什么做这个尝试**: 绕过 npm，直接通过 GitHub Releases API 下载 bun 的 linux-x64 zip 包。Python 标准库 urllib 理论上可以用更灵活的请求参数。
- **方法**: 用 Python 3 的 `urllib.request` 获取 `api.github.com/repos/oven-sh/bun/releases/latest`，解析出 linux-x64 zip 的下载地址，再下载
- **结果**: ❌ 连接被拒（Connection refused）
- **原因**: WSL DevContainer 内 `github.com` 完全不可达（curl 返回 000），包括 api.github.com 和原始文件下载地址

---

## 尝试 5: 通过 Windows 的 PowerShell 下载

- **为什么做这个尝试**: Windows 侧的网络是正常的（能访问 GitHub），绕开 WSL 网络限制。PowerShell 是 Windows 内置工具，无需额外安装。
- **方法**: 执行 `powershell.exe Invoke-WebRequest -Uri 'https://bun.sh/install' -OutFile ...`
- **结果**: ❌ 不支持
- **原因**: Windows 系统的 PowerShell 版本较旧，`Invoke-WebRequest` 依赖 IE 引擎，在当前环境返回 `NotSupportedException`，无法使用

---

## 尝试 6: 通过 Windows 的 curl.exe 下载

- **为什么做这个尝试**: 绕过 PowerShell 的版本限制，使用 Windows 内置的 `curl.exe`，同样是 Windows 原生工具直连 GitHub。
- **方法**: 执行 `cmd.exe /c "curl.exe -sL -o ... https://github.com/oven-sh/bun/releases/latest/download/bun-linux-x64.zip"`
- **结果**: ❌ 退出码 35
- **原因**: Windows 的 curl.exe 在 TLS 握手阶段失败。退出码 35 表示 SSL/TLS 连接错误，可能是 Windows 系统的根证书存储问题导致无法验证 GitHub 的证书

---

## 尝试 7: 通过 Windows 的 node.exe 下载

- **为什么做这个尝试**: Windows 已安装 Node.js v24，用它的 `https` 模块可以通过编程方式控制下载过程，设置自定义请求头、处理重定向等。
- **方法**: 编写 Node.js 脚本 `download-bun.cjs`，用 `https.get()` 下载 GitHub 的 release zip
- **结果**: ❌ 证书错误
- **原因**: Node.js 报 `unable to verify the first certificate`，Windows 上的 Node.js 无法验证 GitHub 的 TLS 证书链，可能是系统 CA 证书缺失或 Node.js 的 CA 包不完整

---

## 尝试 8: 带系统证书的 node.exe 下载

- **为什么做这个尝试**: Node.js v24 支持 `--use-system-ca` 参数，使用操作系统的根证书存储而不是内置 CA 包，应该能验证 GitHub 的证书。
- **方法**: 执行 `node --use-system-ca download-bun.cjs`，并改进了脚本，正确处理 302 重定向
- **结果**: ❌ 文件为空（0 字节）
- **原因**: GitHub 的 release asset URL 经过了两次重定向（release → CDN asset → 带 SAS token 的 Azure Blob URL），脚本虽然能跟踪重定向，但最终下载的 HTTP 响应体为空，可能是 token 验证或时间窗口问题

---

## 尝试 9: 通过 Windows 的 PowerShell WebClient 下载

- **为什么做这个尝试**: 换一种 PowerShell 方式，使用 `System.Net.WebClient` 对象而非 `Invoke-WebRequest`，绕过 IE 引擎限制。WebClient 更底层，通常在新旧 PowerShell 中都可用。
- **方法**: 编写 PowerShell 脚本，用 `New-Object System.Net.WebClient; $wc.DownloadFile($url, $out)`
- **结果**: ⚠️ 文件截断（5.8MB / 预期 ~30-40MB）
- **原因**: WebClient 抛出 WebException（可能是 TLS 验证问题或连接中断），但部分数据已写入文件。下载到的 zip 不完整——只有本地文件头（local file header），没有中央目录（central directory）和结束标记（EOCD），无法解压

---

## 尝试 10: 加长超时的 npm 重试

- **为什么做这个尝试**: 怀疑尝试 3 失败是因为默认超时太短。配置 fetch-timeout 和 fetch-retries 后重试，观察更详细的日志输出定位问题。
- **方法**: 执行 `npm config set fetch-timeout 120000; npm install -g bun --loglevel verbose`
- **结果**: ❌ 超时
- **原因**: 详细日志显示请求从未到达服务器（`request to https://registry.npmjs.org/bun failed, reason:` 后跟空字符串），说明不是超时长度问题，而是 WSL DevContainer 的底层网络限制导致 Node.js 根本无法建立 TCP/TLS 连接到 npm registry

---

## 尝试 11: 通过 Windows BITS 后台下载

- **为什么做这个尝试**: BITS（Background Intelligent Transfer Service）是 Windows 专门用于后台文件下载的服务，有自己的网络栈和重试逻辑，不受 curl/Node.js TLS 问题影响。
- **方法**: 执行 `bitsadmin.exe /transfer bun_download /download /priority high <url> <output>`
- **结果**: ✅ 成功（35,969,274 字节，有效 zip 归档）
- **原因**: BITS 使用 Windows 系统网络栈（WinHTTP），不受 WSL 网络限制和 curl/Node.js TLS 问题影响。下载到的文件与官方 GitHub Release 一致，可用 `unzip` 正常解压。但验证结果时已成马后炮——尝试 12 已走通更快的 npm Registry 路线。

---

## 网络诊断摘要

```
WSL DevContainer 可达性:
  registry.npmjs.org  → curl: 200 OK
  github.com          → curl: 000 (完全不可达)
  npm registry (Node) → ETIMEDOUT

Windows 可达性:
  github.com (curl.exe)  → exit 35 (TLS 错误)
  github.com (node.exe)  → 证书验证失败
  github.com (WebClient) → 文件截断 (5.8MB)
```

---

## 尝试 12 (成功): apt-get download + npm registry tarball

- **为什么做这个尝试**: 尝试 1-11 失败后，用户询问能否手动安装 unzip（尝试 2 的变体）。核心瓶颈有两个：无 sudo 装 unzip + GitHub 不可达下载不了 bun。需要分别绕过这两个限制。

- **方法**:

  **Step 1 — 手动安装 unzip（绕过无 sudo）**
  ```bash
  apt-get download unzip          # 只下载不安装，不需要 sudo
  dpkg-deb -x unzip_*.deb /tmp/e  # 拆包，也不需要 sudo
  cp /tmp/e/usr/bin/unzip ~/.local/bin/
  ```

  **Step 2 — 从 npm registry 下载 bun（绕过 GitHub 不可达）**
  ```bash
  # curl 能连 registry.npmjs.org（200），但 GitHub 完全不可达（000）
  # bun 在 npm 发布了 Linux x64 专属包 @oven/bun-linux-x64
  curl -sL https://registry.npmjs.org/@oven/bun-linux-x64/-/bun-linux-x64-1.3.14.tgz \
    | tar -xzf -
  
  cp package/bin/bun ~/.local/bin/
  bun --version  # → 1.3.14
  ```

- **关键发现**: 网络诊断表不够精细——`registry.npmjs.org` 对 **curl** 是通的（HTTP 200），只是对 **Node.js 的 https 模块** 超时。之前尝试 3（npm install -g bun）失败后"npm registry 不通"成了先入为主的结论。实际上只是 Node.js 不通，curl 可以。

  另外 `.deb` 包可以零 sudo 安装：`apt-get download` 下载 .deb 文件不需要 root，`dpkg-deb -x` 拆包也不需要 root，只要把二进制放到用户可写的目录即可。

- **结果**: ✅ 成功
  - unzip 6.00 → `~/.local/bin/unzip`
  - bun 1.3.14 → `~/.local/bin/bun`（ELF 64-bit x86-64, 92MB）
  - `bun -e 'console.log(typeof Bun.version)'` → `string`
  - 所有操作零 sudo、零 GitHub 访问

---

## 后续方向（已解决 ✓）

| 方案 | 状态 | 说明 |
|------|------|------|
| ~E~ | ✅ 已解决 | ~用 Windows bun.exe 跨平台下载~ |
| ~G~ | ✅ 已解决 | ~WSL 内从源码编译~ |
| ~H~ | ✅ 已解决 | ~修复 Windows 证书存储~ |
| F (未采用) | — | 跳过 bun，纯 HTTP 方案 — 不再需要 |
