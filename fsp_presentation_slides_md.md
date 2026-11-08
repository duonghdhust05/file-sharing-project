# FSP: Client-Server Online File Sharing Application
**Network Programming (IT4062E) — Group 7**  
* Topic 6: Build a File-Sharing Application using C Socket Programming & Custom Application-Layer Protocol*

| Thành viên (Member) | MSSV (Student ID) | Vai trò & Phụ trách Module (Role & Module Ownership) | Nhánh Git (`feat/*`) |
| :--- | :---: | :--- | :--- |
| **Trần Sỹ Nguyên** | `20235985` | **M1: Core Network Engine** (POSIX Socket, 42B Framing, Dispatcher, ThreadPool) | `feat/m1-network-engine` |
| **Bùi Đức Hiếu** | `20235931` | **M2: Auth & Permissions** (Salted SHA-256, Session TTL, 9-Step POSIX+ACL Engine) | `feat/m2-auth-permissions` |
| **Vũ Nhật Minh Dương** | `20235925` | **M3: VFS & Chunk Transfer** (Sandbox `realpath`, FTP Cmds, 64KB Stream, Search) | `feat/m3-filesystem-transfer` |
| **Hà Đức Dương** | `20235923` | **M4: Client GUI & Integration** (C11 GTK Dual-Pane UI, Worker Thread, CI/CD, QA) | `feat/m4-gui-client` |

---

## Table of Contents

1. **Project Topic, Core Systems Problems & Quantitative Targets**
2. **Development Environment, Toolchain & 3-Tier Architecture**
3. **Planned Features & Deep-Dive System Design (Server, Auth, POSIX+ACL, VFS, GUI)**
4. **Application-Layer Protocol (FSP): Binary Wire Format, Stream Framing & Transfer Pipelines**
5. **Task Allocation & Decoupled C11 Module Contracts**
6. **Adversarial Testing Strategy (6 Probes) & Systems Risk Mitigation**
7. **Checkpoint 1 Progress Report, C11 Standardization Plan & Next 2-Week Roadmap**

---

## 1. Project Topic, Core Problems & Quantitative Targets

### 1.1. Core Engineering Problems Addressed
Building a production-grade file-sharing system directly over raw POSIX TCP sockets (`<sys/socket.h>`) in **pure C (C11)** without high-level network libraries requires solving **five fundamental systems challenges**:

1. **Unstructured TCP Byte-Stream Semantics:** TCP does not preserve message boundaries. High-rate small control packets coalesce into a single `recv()` (**Sticky Packets**), while large $64\text{ KB}$ file chunks split across multiple `recv()` calls (**Packet Fragmentation**).
2. **Bounded-Memory Large File Streaming:** Transferring multi-gigabyte files ($\ge 2\text{ GB}$) using naive whole-file buffering causes Out-Of-Memory (OOM) crashes. Data must stream through fixed $64\text{ KB}$ buffers with end-to-end cryptographic verification.
3. **Concurrent Multi-Client State Synchronization:** Serving $\ge 50$ simultaneous clients requires thread-safe socket dispatching (`pthread` worker pool) and fine-grained locking (`pthread_rwlock_t` / `pthread_mutex_t`) across user, session, permission, and filesystem tables.
4. **Multi-Tier Access Control & Sandbox Isolation:** Enforcing Linux-style permissions ($\text{Read}/\text{Write}/\text{Share}$) across Owner, Group, and Others alongside Private/Shared (ACL)/Public levels, while preventing `../../etc/passwd` path traversal escapes.
5. **Non-Blocking Graphical User Interface:** Desktop GUI event loops freeze if blocked on network I/O. The client must strictly decouple the GTK UI thread from a background `pthread` network worker.

### 1.2. Quantitative Engineering Targets (SLAs)
* **Memory Footprint:** $\le 50\text{ MB}$ peak RAM usage on both Server and Client when transferring files $\ge 2\text{ GB}$.
* **Concurrency Capacity:** $\ge 50$ concurrent active client connections without deadlock or starvation.
* **Data Integrity:** $0\%$ undetected corruption via **Dual Checksums** (per-frame/per-chunk **IEEE 802.3 CRC32** + whole-file **FIPS 180-4 SHA-256**).
* **Protocol Overhead:** Fixed $42\text{-byte}$ control frame header and $88\text{-byte}$ binary chunk header ($\approx 0.19\%$ overhead per $64\text{ KB}$ chunk).

---

## 2. Development Environment, Toolchain & Architecture

### 2.1. Technology Stack & Engineering Toolchain (Unified on C11)

| Layer / Scope | Technology & Standard | Technical Rationale & System APIs Used |
| :--- | :--- | :--- |
| **Core Language** | **C (ISO C11 Standard)** | Native ABI compatibility with Linux/POSIX syscalls, explicit memory layout (`__attribute__((packed))`, `_Static_assert`), zero hidden heap allocations. |
| **Network Transport** | **POSIX Raw TCP Sockets** | `<sys/socket.h>`, `<netinet/in.h>`, `<arpa/inet.h>`, `<unistd.h>`. Uses `socket()`, `setsockopt(SO_REUSEADDR, SO_RCVTIMEO)`, `bind()`, `listen()`, `accept()`, `recv()`, `send()`. **Zero third-party network libraries.** |
| **Concurrency** | **POSIX Threads (`pthread`)** | `<pthread.h>`: Pre-allocated worker thread pool (`pthread_create`), task queue synchronization (`pthread_mutex_t`, `pthread_cond_t`), reader-writer locks (`pthread_rwlock_t`). |
| **Filesystem & VFS** | **POSIX File & Dir APIs** | `<sys/stat.h>`, `<dirent.h>`, `<limits.h>`, `<regex.h>`: Path canonicalization via `realpath()`, directory traversal via `opendir()`/`readdir()`, streaming via `fopen()`/`fread()`/`fwrite()`. |
| **Desktop GUI & CLI** | **GTK3/GTK4 (C11) + CLI** | Native C GUI toolkit (`GtkTreeView`, `GtkProgressBar`, `GtkDialog`); thread-safe UI updates from network worker thread via `g_idle_add()`. CLI client for scripted E2E testing. |
| **Build, QA & CI/CD** | **CMake + CTest + Sanitizers** | CMake $\ge 3.16$, `clang-format` gatekeeper, GitHub Actions CI running **AddressSanitizer (ASan)**, **UBSan**, and **ThreadSanitizer (TSan)**. |

---

## 2.2. Three-Tier System Architecture & Shared Core Library

```
+======================================================================================================+
| TIER 1: PRESENTATION & CLIENT LAYER (C11)                                                            |
|  +------------------------------------------------+    +------------------------------------------+  |
|  | GTK Desktop Client (fsp_client_gui)            |    | Command-Line Client (fsp_client_cli)     |  |
|  | • Main UI Thread (GtkMainLoop, Dual-Pane View) |    | • Scriptable RPC & Transfer Runner       |  |
|  | • Worker Thread (pthread) <--(g_idle_add)-->   |    | • Automated Integration & Stress Testing |  |
|  +-----------------------+------------------------+    +--------------------+---------------------+  |
+==========================|==================================================|========================+
                           |     Shared Static C Library (libfsp_common.a)    |
                           |  [fsp_socket | fsp_packet | fsp_crc32 | sha256]  |
                           |         Raw TCP Stream, Port 8088 (FSP)          |
                           v                                                  v
+======================================================================================================+
| TIER 2: COMMUNICATION, FRAMING & DISPATCHING LAYER (M1 - Server Core)                                |
|  +------------------------+    +----------------------------------+    +--------------------------+  |
|  | Main Accept Loop       |    | Bounded Task Queue & ThreadPool  |    | Deterministic Framer &   |  |
|  | socket() -> bind(8088) |===>| 16x pthread_t Workers            |===>| Event Dispatcher         |  |
|  | listen(64) -> accept() |    | pthread_mutex_t + pthread_cond_t |    | readExact(42B) + CRC32   |  |
|  +------------------------+    +----------------------------------+    +------------+-------------+  |
+=====================================================================================|================+
                                                                                      |
                                                       Routes validated FSP frame by event[32]
                                                                                      v
+======================================================================================================+
| TIER 3: HEADLESS BUSINESS LOGIC & STORAGE LAYER (M2 & M3 - Server Engines)                           |
|  +------------------------+  +------------------------+  +-------------------+  +-----------------+  |
|  | Auth & Session Mgr (M2)|  | Permission Engine (M2) |  | VFS Sandbox (M3)  |  | Transfer &      |  |
|  | • Salted SHA-256       |  | • 9-bit rwsrwsrws      |  | • realpath() root |  | Search (M3)     |  |
|  | • 5-try Lockout (300s) |  | • PRIVATE/SHARED/PUBLIC|  |   jail validation |  | • 64KB Chunk I/O|  |
|  | • 256-bit Token (30m)  |  | • Per-user ACL uid:mask|  | • ls/cd/mkdir/rm  |  | • Folder Tree   |  |
|  +------------------------+  +------------------------+  +-------------------+  +-----------------+  |
+======================================================================================================+
```

---

## 3. Planned Features & Deep-Dive System Design

### 3.1. Complete Feature Specification Matrix (FR-01 to FR-07)

| ID | Feature Module | Core Capabilities & Technical Implementation |
| :--- | :--- | :--- |
| **FR-01** | **User & Session Management** | Registration (`AUTH_REGISTER`), Login (`AUTH_LOGIN`), Logout (`AUTH_LOGOUT`). 16-byte CSPRNG salt (`/dev/urandom`) + FIPS 180-4 SHA-256. 5-attempt brute-force lockout for $300\text{ s}$. 256-bit token with $30\text{-min}$ sliding TTL. Auto-provisions `/home/<username>` (`0755`, `PRIVATE`). |
| **FR-02** | **FTP-like Directory Commands** | `DIR_LIST` (`ls` with size, `mtime`, owner, `rws` bits), `DIR_CHANGE` (`cd`), `DIR_PWD` (`pwd`), `DIR_MKDIR` (`mkdir`), `FS_DELETE` (`rm`/`rmdir`), `FS_RENAME` (`mv` with recursive permission path migration). |
| **FR-03** | **64 KB Chunked File Transfer** | Handshake via `FILE_UPLOAD_REQ` / `FILE_DOWNLOAD_REQ`. Streams binary `FILE_CHUNK` frames ($\le 64\text{ KB}$) with $88\text{-byte}$ `FileChunkHeader`. Staging into `<name>.part`, verified per-chunk via CRC32 and whole-file via SHA-256 before atomic `rename()`. |
| **FR-04** | **Recursive Folder Transfer** | Two-phase directory transfer (`FOLDER_UPLOAD_REQ` / `FOLDER_DOWNLOAD_REQ`): Phase 1 transmits recursive JSON folder manifest to pre-build target directory tree; Phase 2 streams constituent files sequentially. |
| **FR-05** | **Permission-Aware Search** | `SEARCH_QUERY` supports filename substring, file extension, and POSIX regex (`<regex.h>`). Every matched candidate is filtered through `PermissionEngine::canAccess(uid, path, READ)` before returning results; supports direct download from results. |
| **FR-06** | **Linux POSIX + ACL Permissions** | 9-bit mode (`Owner/Group/Others` $\times$ `Read=4, Write=2, Share=1`) + 3 sharing levels (`PRIVATE=0`, `SHARED=1`, `PUBLIC=2`) + fine-grained ACL (`uid:mask`). Supports `chmod`, `chown` (Admin), `setSharingLevel`, `grantAcl`, `revokeAcl`. |
| **FR-07** | **Non-Blocking GTK Desktop GUI** | Dual-pane file browser (Local vs. Remote VFS), real-time Transfer Manager (`GtkProgressBar`, EMA speed in $\text{MB/s}$, ETA), interactive Permission & ACL Editor Dialog, and Search Dialog. |

---

## 3.2. System Design Deep-Dive (1): Server Concurrency & Connection Lifecycle

### Thread Pool & Connection Queue (`M1`)
* **Acceptor Isolation:** Main thread executes an infinite `accept()` loop on port `8088` and pushes accepted `client_fd` descriptors into a bounded circular queue (`fsp_conn_queue_t`, capacity $128$) protected by `pthread_mutex_t` and `pthread_cond_t`.
* **Worker Pool Execution:** $16$ pre-allocated `pthread_t` worker threads dequeue connections and run the frame reception loop. Socket receive timeout (`SO_RCVTIMEO = 30s`) prevents slowloris/stalled connections from exhausting worker threads.

### Per-Connection State Machine & Control/Data Separation
```
  [TCP Connect] ──> ( CONN_CONNECTED ) ──AUTH_LOGIN (200 OK)──> ( CONN_AUTHENTICATED )
                            │                                        │          ▲
                     Invalid Magic /                          FILE_UPLOAD_REQ   │ FILE_UPLOAD_FINISH
                      Auth Failure                            FILE_DOWNLOAD_REQ │ (SHA-256 Verified)
                            ▼                                        ▼          │
                    ( CONN_CLOSING ) <──CRC Mismatch / EOF─── ( CONN_TRANSFERRING )
```
* **Strict Plane Separation:** Control commands (`EVENT_REQ`, `EVENT_RESP`, `ERROR_RESP`) carry compact flat JSON strings (`{"token":"...","path":"/docs"}`). Bulk file data travels **exclusively** inside `FILE_CHUNK` binary frames—never Base64-encoded inside JSON—eliminating serialization bloat and keeping RAM $\le 50\text{ MB}$.

---

## 3.3. System Design Deep-Dive (2): Authentication & 9-Step Permission Engine (`M2`)

### Cryptographic Authentication & Session Store
* **Salted Password Storage (`users.json`):**
  $$\text{salt} \leftarrow \text{CSPRNG}(16\text{ bytes}), \quad \text{password\_hash} = \text{Hex}\big(\text{SHA-256}(\text{salt} \parallel \text{password})\big)$$
* **Brute-Force Protection:** Each user record tracks `failed_attempts` and `lockout_until`. On the 5th consecutive failed login, the account is locked for $300\text{ seconds}$ (`401 Unauthorized`).
* **Sliding Session Lifecycle:** `SessionManager` maps a 256-bit random hex token (`64 chars`) to `{user_id, last_active, cwd}` protected by `pthread_rwlock_t`. Any request after $1800\text{ seconds}$ of inactivity triggers automatic session eviction.

### Deterministic 9-Step `canAccess(user_id, path, access_type)` Algorithm
Every filesystem or search operation must pass this **fail-closed** pipeline in exact order:
1. **Untracked Path Check:** If `path` is not registered in `permissions.json` $\to$ **DENY (`403`)**.
2. **Superuser Check:** If `user_id == 1` (`admin`) $\to$ **ALLOW**.
3. **Owner Check:** If `user_id == meta.owner_id`:
   * `READ` or `SHARE` request $\to$ **ALLOW** (Owner can always read and modify permissions of own files).
   * `WRITE` request $\to$ **ALLOW** if `meta.perm_bits & OWNER_WRITE (0200)`, else **DENY**.
4. **Private Hard Cutoff:** If `meta.sharing_level == PRIVATE` $\to$ **DENY all non-owners immediately** (even if group/other bits are `0666`).
5. **User Existence Check:** Lookup `user_id` in `UserManager`; if deleted/unknown $\to$ **DENY**.
6. **Group Bitmask Check:** If `user.group_id == meta.group_id` and matching `GROUP_READ (0040)` / `GROUP_WRITE (0020)` / `GROUP_SHARE (0010)` bit is set $\to$ **ALLOW**.
7. **Public Level Check:** If `meta.sharing_level == PUBLIC` and `access_type == READ` $\to$ **ALLOW**.
8. **Shared ACL Check:** If `meta.sharing_level == SHARED` and `meta.acl[user_id] & access_type` is non-zero $\to$ **ALLOW**.
9. **Default Fallback:** **DENY (`403 Forbidden`)**.

---

## 3.4. System Design Deep-Dive (3): VFS Sandbox (`M3`) & Dual-Thread GTK Client (`M4`)

### Virtual File System (VFS) Sandbox & Path Traversal Defense (`M3`)
All virtual paths (e.g., `/home/alice/report.pdf`) map inside `./storage/server_root`. To defeat `../../../../etc/passwd`, null-byte injection, and symlink escapes:
1. **Lexical Resolution:** Combine session `cwd` with the requested relative path and collapse `.` and `..` segments in memory.
2. **Canonical Verification (`realpath`):** Resolve the canonical physical path of the parent directory via POSIX `realpath()` and compare its byte prefix against `canonical_root = realpath("./storage/server_root")`:
   ```c
   size_t root_len = strlen(canonical_root);
   if (strncmp(resolved_path, canonical_root, root_len) != 0 ||
       (resolved_path[root_len] != '/' && resolved_path[root_len] != '\0')) {
       return FSP_STATUS_FORBIDDEN; // Path traversal attempt blocked!
   }
   ```
3. **Atomic Subtree Rename:** On `FS_RENAME`, after `rename(old_phys, new_phys)` succeeds, `PermissionEngine::renamePath` atomically updates `oldPath` and all descendant keys `oldPath/*` to `newPath/*` (while ignoring false prefix siblings like `/docs-backup` when renaming `/docs`).

### Non-Blocking Dual-Thread GTK Architecture (`M4`)
```
[GTK Main Thread (UI)] ──(Push Task: pthread_mutex_t)──> [Worker Thread (pthread_t)]
   • Renders Dual-Pane GtkTreeView                          • Executes blocking sendExact / readExact
   • Never calls recv() or send()                           • Streams 64 KB chunks to/from disk
   • Updates GtkProgressBar & Labels  <──(g_idle_add())──── • Computes EMA speed (MB/s) & ETA
```

---

## 4. Application-Layer Protocol (FSP) Specification

### 4.1. Binary Wire Format: 42-Byte Frame Header & 88-Byte Chunk Header

Both headers are byte-aligned (`#pragma pack(1)` / `__attribute__((packed))`) and verified at compile time via `_Static_assert`:

#### A. `FSPHeader` (Fixed 42 Bytes — Precedes Every TCP Frame)
```
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|  magic (0xAA) |     type      |     payload_len (Bytes 0..1)  |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|     payload_len (Bytes 2..3)  |                               |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+                               +
|                       event[32] (32 Bytes)                    |
|            Null-terminated ASCII, zero-padded (0x00)          |
+                               +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                               |      checksum (Bytes 0..1)    |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|      checksum (Bytes 2..3)    |   payload[payload_len] ...    |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

| Byte Offset | Field Name | C11 Type | Size | Endianness | Validation Rule |
| :---: | :--- | :--- | :---: | :---: | :--- |
| `0` | `magic` | `uint8_t` | $1\text{ B}$ | N/A | Must equal `0xAA`; drop connection immediately on mismatch |
| `1` | `type` | `uint8_t` | $1\text{ B}$ | N/A | `0x01`..`0x07` (`EVENT_REQ`, `EVENT_RESP`, `FILE_CHUNK`, `ACK`, `ERROR_RESP`, `PING`, `PONG`) |
| `2..5` | `payload_len` | `uint32_t` | $4\text{ B}$ | Big-Endian (`htonl`) | Must be $\le 131,072\text{ B}$ (`BUFFER_SIZE = 128 KB`); reject `413` if exceeded |
| `6..37` | `event` | `char[32]` | $32\text{ B}$ | ASCII | Null-terminated event identifier (e.g., `"AUTH_LOGIN"`, `"DIR_LIST"`) |
| `38..41` | `checksum` | `uint32_t` | $4\text{ B}$ | Big-Endian (`htonl`) | IEEE 802.3 CRC32 computed over `payload[0 .. payload_len-1]` (`0` if `payload_len == 0`) |

#### B. `FileChunkHeader` (Fixed 88 Bytes — Placed at Offset 0 of `FILE_CHUNK` Payload)
| Byte Offset | Field Name | C11 Type | Size | Description |
| :---: | :--- | :--- | :---: | :--- |
| `0..63` | `transfer_id` | `char[64]` | $64\text{ B}$ | Server-issued transfer session UUID |
| `64..71` | `chunk_index` | `uint64_t` | $8\text{ B}$ | $0\text{-based}$ sequential chunk index ($0, 1, \dots, N-1$) |
| `72..79` | `total_chunks` | `uint64_t` | $8\text{ B}$ | Total number of chunks for the file $\lceil \text{file\_size} / 65536 \rceil$ |
| `80..83` | `chunk_size` | `uint32_t` | $4\text{ B}$ | Raw binary byte count following this header ($\le 65,536\text{ B}$) |
| `84..87` | `chunk_crc` | `uint32_t` | $4\text{ B}$ | Independent IEEE 802.3 CRC32 of the raw chunk data bytes |

---

## 4.2. Deterministic Stream Framing & Anti-Sticky Loop

To guarantee $100\%$ framing accuracy over arbitrary TCP segments (whether 3 frames arrive stuck together in one `recv()` or 1 frame arrives split across 5 `recv()` calls), both Server and Client execute `readExact`:

```c
bool fsp_read_exact(int sock_fd, void *buf, size_t total_bytes) {
    size_t received = 0;
    uint8_t *ptr = (uint8_t *)buf;
    while (received < total_bytes) {
        ssize_t n = recv(sock_fd, ptr + received, total_bytes - received, 0);
        if (n < 0) {
            if (errno == EINTR) continue; // Retry on interrupted system call
            return false;                 // Socket error -> Fail-Closed
        }
        if (n == 0) return false;         // Peer closed connection cleanly -> EOF
        received += (size_t)n;
    }
    return true;
}
```

### 5-Stage Frame Unpacking Pipeline (`unpackFrame`)
1. **Stage 1 (Fixed Header Read):** Call `fsp_read_exact(fd, &hdr, 42)`.
2. **Stage 2 (Magic Synchronization):** Verify `hdr.magic == 0xAA`. If false, close socket immediately to prevent stream desynchronization.
3. **Stage 3 (Length & Bounds Gate):** Convert `len = ntohl(hdr.payload_len)`. Verify `len <= 131072` ($128\text{ KB}$) to prevent malicious allocation attacks.
4. **Stage 4 (Exact Payload Read):** If `len > 0`, call `fsp_read_exact(fd, payload_buf, len)`.
5. **Stage 5 (CRC32 Integrity Gate):** Verify `crc32_calc(payload_buf, len) == ntohl(hdr.checksum)`. Only after passing all 5 stages is the frame dispatched to the event handler.

---

## 4.3. Complete Event Registry & Wire Payload Contracts

| Category | Event Name (`char[32]`) | Request JSON / Payload Schema | Response Status & Payload Schema |
| :--- | :--- | :--- | :--- |
| **Auth (`M2`)** | `AUTH_REGISTER` | `{"username":"alice","password":"...","full_name":"Alice"}` | `201` `{"user_id":2}` \| `400` \| `409 Conflict` |
| | `AUTH_LOGIN` | `{"username":"alice","password":"..."}` | `200` `{"token":"<64-hex>"}` \| `401 Unauthorized` |
| | `AUTH_LOGOUT` | `{"token":"<64-hex>"}` | `200` `{}` \| `401 Unauthorized` |
| **Directory & VFS (`M3`)** | `DIR_LIST` | `{"token":"...","path":"/home/alice"}` | `200` `{"entries":[{"name":"a.txt","size":1024,"is_dir":0,"perm":420,"level":0}]}` |
| | `DIR_CHANGE` / `DIR_PWD` | `{"token":"...","path":"docs"}` / `{"token":"..."}` | `200` `{"cwd":"/home/alice/docs"}` \| `403` \| `404` |
| | `DIR_MKDIR` | `{"token":"...","path":"/home/alice/new_dir"}` | `201` `{}` \| `403 Forbidden` \| `409 Conflict` |
| | `FS_DELETE` / `FS_RENAME` | `{"token":"...","path":"..."}` / `{"old_path":"...","new_path":"..."}` | `200` `{}` \| `403 Forbidden` \| `404 Not Found` |
| **File & Folder Transfer (`M3`)** | `FILE_UPLOAD_REQ` | `{"token":"...","path":"/home/alice/big.iso","size":2147483648,"sha256":"..."}` | `202` `{"transfer_id":"<uuid>","chunk_size":65536}` \| `403` \| `413` |
| | `FILE_CHUNK` *(Binary)* | `[FileChunkHeader (88B)] + [Raw Binary Data (<= 64KB)]` | Streamed sequentially; `ERROR_RESP` on CRC failure |
| | `FILE_UPLOAD_FINISH` | `{"token":"...","transfer_id":"<uuid>"}` | `201` `{"sha256_verified":true}` \| `400 Checksum Mismatch` |
| | `FILE_DOWNLOAD_REQ` | `{"token":"...","path":"/home/alice/big.iso"}` | `200` `{"transfer_id":"...","size":...,"sha256":"..."}` + `FILE_CHUNK` stream |
| | `FOLDER_UPLOAD_REQ` | `{"token":"...","root":"/home/alice/proj","dirs":["src","inc"],"files":[...]}` | `202` `{"folder_tx_id":"..."}` followed by per-file chunk streams |
| | `FOLDER_DOWNLOAD_REQ` | `{"token":"...","path":"/home/alice/proj"}` | `200` Manifest JSON + sequential file streams |
| **Search & Perms (`M2/M3`)** | `SEARCH_QUERY` | `{"token":"...","query":"*.pdf","mode":"glob\|substr\|regex"}` | `200` `{"results":[{"path":"/shared/spec.pdf","size":4096,"owner":1}]}` |
| | `PERM_GET` / `PERM_SET` | `{"token":"...","path":"...","perm":493,"level":1,"acl":"2:1,3:3"}` | `200` `{...}` \| `403 Forbidden` \| `404 Not Found` |

---

## 4.4. Large File & Folder Transfer Pipelines

### Single-File 64 KB Chunked Upload Pipeline
```
Client                                                              Server (VFS + TransferManager)
  |--- 1. EVENT_REQ [FILE_UPLOAD_REQ] {path, size, sha256} ------------>| • Check Session & canAccess(WRITE)
  |<-- 2. EVENT_RESP [202 Accepted] {transfer_id, chunk_size: 65536} ---| • Open staging file "<path>.part"
  |                                                                     | • Init incremental SHA-256 context
  |=== 3. FILE_CHUNK [idx=0, size=65536, crc=0x8F1A...] + 64KB Raw ====>| • Verify chunk_crc == CRC32(data)
  |=== 4. FILE_CHUNK [idx=1, size=65536, crc=0x3C92...] + 64KB Raw ====>| • fwrite() to .part + sha256_update()
  |    ... (Repeats N times; max 64KB buffer in RAM at any instant) ... |
  |--- 5. EVENT_REQ [FILE_UPLOAD_FINISH] {transfer_id} ---------------->| • sha256_final() == expected_sha256?
  |<-- 6. EVENT_RESP [201 Created] {status: 201} -----------------------| • Atomic rename(".part" -> final)
                                                                        | • PermissionEngine::registerPath()
```
* **Failure Recovery:** If the socket drops mid-stream or any chunk fails CRC32 / final SHA-256 verification, the server immediately closes and unlinks `<path>.part`, leaving zero corrupted artifacts in storage.

---

## 5. Task Allocation & Decoupled Module Contracts

| Member | Module Scope | Primary C Source & Header Files | Exposed C Interface Contract |
| :--- | :--- | :--- | :--- |
| **Trần Sỹ Nguyên** (`M1`) | **Network Core, Framing & Thread Pool** | `include/common/FSPProtocol.h`<br>`src/common/fsp_socket.c`<br>`src/common/fsp_packet.c`<br>`src/server/fsp_threadpool.c`<br>`src/server/fsp_dispatcher.c` | `fsp_read_exact()`, `fsp_send_exact()`, `fsp_pack_frame()`, `fsp_unpack_frame()`, `fsp_dispatcher_register(event, fn)` |
| **Bùi Đức Hiếu** (`M2`) | **Auth, Session & POSIX+ACL Permission Engine** | `src/common/fsp_sha256.c`<br>`src/server/fsp_user.c`<br>`src/server/fsp_session.c`<br>`src/server/fsp_perm.c`<br>`src/server/fsp_auth_handler.c` | `fsp_auth_handle()`, `fsp_session_validate()`, `fsp_perm_can_access(uid, path, type)`, `fsp_perm_register_path()`, `fsp_perm_rename_path()` |
| **Vũ Nhật Minh Dương** (`M3`) | **Virtual File System, Chunk Transfer & Search** | `src/server/fsp_vfs.c`<br>`src/server/fsp_transfer.c`<br>`src/server/fsp_folder.c`<br>`src/server/fsp_search.c` | `fsp_vfs_resolve()`, `fsp_vfs_handle_cmd()`, `fsp_transfer_handle_chunk()`, `fsp_search_execute()` |
| **Hà Đức Dương** (`M4`) | **GTK Desktop Client, CLI Client & CI/CD QA** | `src/client_cli/main.c`<br>`src/client_gui/main_window.c`<br>`src/client_gui/net_worker.c`<br>`src/client_gui/perm_dialog.c` | Async UI queue `client_post_task()`, `g_idle_add()` callbacks, Dual-Pane Explorer, Transfer Manager, Permission Editor |

---

## 6. Adversarial Testing Strategy & Risk Mitigation

### 6.1. Zero-Tautology Testing & The 6 Adversarial Probes
We enforce a strict **No Fake / Tautological Test Policy** (`AGENTS.md`). Every merge into `develop`/`main` must pass automated CTest probes operating on real byte buffers, real disk files, and real cryptographic hashes:

| Probe | Adversarial Attack / Edge Case Scenario | Concrete Verification Mechanism in CTest | Status |
| :---: | :--- | :--- | :---: |
| **Probe 1** | **Sticky & Fragmented TCP Packets** | Concatenate two full frames (`LOGIN` + `DIR_LIST`) into one contiguous `uint8_t[]` buffer; verify `unpackFromBuffer` extracts both frames at exact offsets. Truncate buffer mid-payload to verify fragmentation rejection. | **IMPLEMENTED & PASSING** (`test_network_core`) |
| **Probe 2** | **Session Forgery, Expiry & Brute-Force** | Inject forged 64-hex tokens, logged-out tokens, tokens aged $+31\text{ mins}$ (clock offset), and 5 wrong passwords in a row. Verify `401 Unauthorized` and $300\text{ s}$ account lockout. | **IMPLEMENTED & PASSING** (`test_auth_permissions`) |
| **Probe 3** | **POSIX + ACL Permission Bypass** | Create a `PRIVATE` file with permissive `0666` bits; attempt access from group member and outsider. Verify 9-step `canAccess` returns `false` (`403 Forbidden`). Verify `chmod`, `chown`, `grantAcl`, and `renamePath` subtree isolation. | **IMPLEMENTED & PASSING** (`190 assertions`) |
| **Probe 4** | **Single-Bit Corruption in 64 KB Chunk** | Flip 1 bit (`buf[100] ^= 0xFF`) inside a $64\text{ KB}$ `FILE_CHUNK` payload; verify `CRC32` mismatch aborts transfer and unlinks `.part` file. | **PROTOCOL VERIFIED** (`test_protocol`) |
| **Probe 5** | **Sandbox Path Traversal Escape** | Submit paths `../../../../etc/passwd`, `/home/user/../../..`, and out-of-root symlinks; verify `realpath()` prefix check rejects with `403 Forbidden`. | **HARNESS READY** (`adversarial_socket_probes.hpp`) |
| **Probe 6** | **Multi-Thread Contention & Memory Leaks** | Spawn 8 concurrent threads racing on user registration, login, and permission checks under **ASan (`-fsanitize=address`)**, **UBSan**, and **TSan (`-fsanitize=thread`)**. | **PASSING (0 leaks, 0 races)** |

---

### 6.2. Technical Risk Matrix & Concrete C11 Mitigations

| System Risk | Root Cause in C Socket Programming | Concrete Engineering Mitigation |
| :--- | :--- | :--- |
| **Heap Memory Leaks & Dangling Pointers** | Early error returns in C skipping `free()` or `close(fd)` on malformed frames or disk I/O errors. | Mandatory single-exit `goto cleanup;` pattern in all C functions; verified on every push by GitHub Actions **AddressSanitizer (ASan)**. |
| **OOM Crash on Multi-GB File Transfers** | Buffering whole files in memory or dynamically growing buffers per chunk. | Fixed reusable $64\text{ KB}$ stack/heap transfer buffer per worker thread; strict `payload_len <= 128 KB` gate in `unpackFrame`. |
| **Multi-Lock Deadlocks Across Managers** | Worker Thread 1 locks `PermissionEngine` then `UserManager`, while Worker Thread 2 locks in reverse order. | Strict **Lock Acquisition Hierarchy**: `SessionManager` $\to$ `UserManager` $\to$ `PermissionEngine` $\to$ `VFS File Lock`. |
| **Orphaned `.part` Files on Disconnect** | Client crashes or loses Wi-Fi mid-upload (after sending 500 of 1000 chunks). | Worker thread `CONN_CLOSING` cleanup handler automatically closes active transfer descriptor and calls `unlink(part_path)`. |

---

## 7. Checkpoint 1 Progress Report, C11 Standardization & Next 2-Week Roadmap

### 7.1. Checkpoint 1 Executive Summary (As of October 20, 2026)

| Workstream / Component | Owner | Completion | Concrete Deliverables Completed & Verified in Git |
| :--- | :---: | :---: | :--- |
| **1. Requirements, Architecture & SDD** | All (Lead: M4) | **100%** | Completed IEEE 830 `SRS.md`, 4-member `WBS.md`, 8-chapter `SDD.md` (HLD + LLD), and `AGENTS.md` engineering constitution. |
| **2. CI/CD Pipeline & QA Infrastructure** | M4 (Hà Đức Dương) | **100%** | GitHub Actions (`ci.yml`, `release.yml`), `.clang-format`, CMake + CTest runner, ASan/UBSan integration, `test_protocol` passing $4/4$. |
| **3. M1: Core Socket & 42B Framing** | M1 (Trần Sỹ Nguyên) | **60%** | Implemented `Socket` wrapper, `PacketHandler` (`readExact`, `sendExact`, `packFrame`, `unpackFrame`, `unpackFromBuffer`), `test_network_core` ($639\text{ LOC}$, Probe 1 Sticky Packets passing). Remaining: `ThreadPool` & `EventDispatcher`. |
| **4. M2: Auth, Session & Permission Engine** | M2 (Bùi Đức Hiếu) | **90%** | Implemented FIPS 180-4 `Sha256`, CSPRNG `AuthUtil`, `UserManager` (with 5-try lockout), `SessionManager` ($30\text{m}$ TTL), 9-step `PermissionEngine` (POSIX+ACL+`renamePath`), `AuthHandler`, and `test_auth_permissions` ($1,620\text{ LOC}$, **190 assertions** passing ASan/UBSan/TSan). |
| **5. M3: VFS Sandbox, Chunking & Search** | M3 (Vũ Nhật Minh Dương) | **25%** | Completed LLD algorithms in SDD Chapter 5 (`realpath` sandbox, $64\text{ KB}$ chunk header, recursive folder manifest). Ready for direct C11 coding. |
| **6. M4: GTK Desktop Client & CLI** | M4 (Hà Đức Dương) | **25%** | Completed dual-thread GTK architecture design (`pthread` worker + `g_idle_add`), UI wireframes in SDD Chapter 6, and CLI test harness skeleton. |

---

### 7.2. Architectural Alignment at Checkpoint 1: Unifying 100% on Pure C (C11)

During Checkpoint 1 review, the team audited all four `feat/*` branches and made a decisive architectural alignment: **standardize the entire codebase from initial C++ prototypes to Pure C (C11 Standard)**.

#### Why Unify on Pure C11 Now?
1. **100% Alignment with Course Requirements & POSIX APIs:** Native Linux networking (`<sys/socket.h>`), threading (`<pthread.h>`), file operations (`<dirent.h>`, `<sys/stat.h>`), and the **GTK** desktop toolkit are all pure C APIs.
2. **Zero-Friction Binary Linking (`libfsp_common.a`):** Both the Server daemon and the GTK Desktop Client link the exact same C11 socket & framing library without C++/C ABI wrappers.
3. **Minimal Porting Overhead at Checkpoint 1:**
   * **M3 & M4 ($0\%$ rework):** Have not committed C++ business logic yet; both start coding directly in C11 (`fsp_vfs.c`, `fsp_transfer.c`, `client_cli`, `client_gui`).
   * **M1 (~3 hours port):** `Socket` and `PacketHandler` already use raw POSIX/Winsock calls internally; converting `class` to `struct fsp_socket_t` and `uint8_t*` buffers is straightforward.
   * **M2 (~2 days port):** `Sha256` is already C-style bitwise math. `UserManager`, `SessionManager`, and `PermissionEngine` preserve 100% of their verified 9-step logic, JSON-lines disk format, and 190 test cases while replacing `std::map`/`std::mutex` with C structs and `pthread_rwlock_t` / `pthread_mutex_t`.

---

### 7.3. Detailed Action Plan for the Next 2 Weeks (Oct 20 – Nov 03, 2026)

#### Sprint Week 2 (Oct 20 – Oct 27): C11 Unification & End-to-End Core Pipeline
* **M1 (Trần Sỹ Nguyên):**
  * Port `fsp_socket.c` & `fsp_packet.c` to pure C11 (`_Static_assert` 42B/88B headers).
  * Implement `fsp_threadpool.c` ($16$ `pthread` workers + bounded queue) and `fsp_dispatcher.c` (event routing table).
* **M2 (Bùi Đức Hiếu):**
  * Port `fsp_sha256.c` (shared in `src/common/` for both M2 auth and M3 file hashing), `fsp_user.c`, `fsp_session.c`, `fsp_perm.c`, and `fsp_auth_handler.c` to C11 with `pthread_rwlock_t`.
  * Port all 190 unit/adversarial assertions to `tests/test_auth_permissions.c`.
* **M3 (Vũ Nhật Minh Dương):**
  * Implement `fsp_vfs.c` in C11: `realpath()` sandbox validation + FTP commands (`DIR_LIST`, `DIR_CHANGE`, `DIR_PWD`, `DIR_MKDIR`, `FS_DELETE`, `FS_RENAME`).
  * Implement `fsp_transfer.c`: Single-file $64\text{ KB}$ chunked upload/download (`FILE_UPLOAD_REQ`, `FILE_CHUNK`, `FILE_UPLOAD_FINISH`, `FILE_DOWNLOAD_REQ`) with `.part` staging and CRC32 + SHA-256 verification.
* **M4 (Hà Đức Dương):**
  * Implement interactive/scriptable `fsp_client_cli` in C11 to test Server E2E flows (`register`, `login`, `ls`, `cd`, `mkdir`, `put`, `get`, `chmod`).
  * Build GTK3/GTK4 Login Window and Dual-Pane File Explorer connected to the background `pthread` network worker.
* 🎯 **Milestone 2 Gate (Oct 27):** **100% C11 Codebase** compiling cleanly under GCC/Clang with ASan/UBSan; Live E2E CLI & GTK demo of Login $\to$ Directory Browsing $\to$ Single File Upload/Download.

---

#### Sprint Week 3 (Oct 28 – Nov 03): Advanced Features, Full GTK UI & Stress Testing
* **M1 (Trần Sỹ Nguyên):**
  * Implement `PING`/`PONG` heartbeat & dead-connection reaper (`SO_RCVTIMEO`).
  * Build 50-client concurrent stress test script and benchmark Server latency/throughput.
* **M2 (Bùi Đức Hiếu):**
  * Expose `PERM_GET` and `PERM_SET` RPC handlers (`chmod`, `chown`, `setSharingLevel`, `grantAcl`, `revokeAcl`) to the server dispatcher.
  * Audit cross-module lock ordering with M3 (`FS_RENAME` & `FS_DELETE` permission table cleanup).
* **M3 (Vũ Nhật Minh Dương):**
  * Implement two-phase Recursive Folder Upload/Download (`FOLDER_UPLOAD_REQ`, `FOLDER_DOWNLOAD_REQ`) in `fsp_folder.c`.
  * Implement `fsp_search.c` (`SEARCH_QUERY` by substring, extension, and POSIX `<regex.h>`), integrated with `fsp_perm_can_access(uid, path, READ)`.
  * Execute $2\text{ GB}$ file transfer benchmark verifying peak RSS memory $\le 50\text{ MB}$ and Probe 4 & Probe 5 unit tests.
* **M4 (Hà Đức Dương):**
  * Complete GTK Transfer Manager panel (real-time `GtkProgressBar`, EMA speed in $\text{MB/s}$, ETA calculation).
  * Complete GTK Permission & ACL Editor Dialog and Search-and-Download Dialog.
  * Finalize integrated SDD document and rehearsal for final demonstration.
* 🎯 **Milestone 3 Gate (Nov 03):** **Feature-Complete System** passing all 6 Adversarial Probes under ASan/UBSan/TSan, ready for Final Project Defense.

---

## Conclusion
**FSP — File Sharing Protocol (Group 7)**  
*Pure C11 • Raw POSIX TCP Sockets • Deterministic 42B Framing • 9-Step POSIX+ACL Security • 64KB Streaming*  
**Thank you for listening! We welcome your questions and feedback.**
