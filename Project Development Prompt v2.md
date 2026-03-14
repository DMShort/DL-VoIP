# DadLink v2.0 — Development Prompt

## What This Is

DadLink is a self-hosted, encrypted Push-to-Talk (PTT) voice communication system. Think private Discord/TeamSpeak that runs entirely on your own server. Designed for families, gaming groups, and small teams who want reliable, encrypted voice chat without third-party services.

This prompt describes a ground-up rebuild incorporating lessons learned from v1.0.

---

## Core Concept

- Users run a desktop client, connect to a self-hosted server, join voice channels, and hold a hotkey to talk
- All voice traffic is transport-encrypted (hop-by-hop via SRTP between each client and the server)
- The server decrypts incoming voice packets and re-encrypts them with each recipient's keys before forwarding — it does not store or log audio, but it does have momentary access to plaintext audio in memory during re-encryption
- An admin panel (built into the client) manages users, channels, roles, and permissions
- Deployment target: Docker on a Windows or Linux server with a static IP

---

## Architecture Overview

```
┌──────────────────────────────────────────────────────┐
│  Qt Desktop Client (Windows)                         │
│  ├─ WebSocket (Control Channel - Port 9000/TCP)      │
│  │   └─ JSON messages: auth, join, leave, state sync │
│  └─ UDP (Voice Channel - Port 9001/UDP)              │
│      └─ Encrypted Opus audio packets                 │
└──────────────────┬───────────────────────────────────┘
                   │  TLS + SRTP Encrypted
                   │
┌──────────────────▼───────────────────────────────────┐
│  Rust Server (Tokio async runtime)                   │
│  ├─ Axum WebSocket handler (control + admin REST)    │
│  ├─ UDP voice router (decrypt → re-encrypt → forward)│
│  ├─ SRTP session manager (per-user encryption keys)  │
│  ├─ PostgreSQL (users, channels, orgs, permissions)  │
│  └─ Redis (session cache, optional)                  │
└──────────────────────────────────────────────────────┘
```

**Two independent network paths:**
1. **TCP/WebSocket (port 9000)**: Reliable channel for authentication, channel management, presence updates, key exchange, admin API
2. **UDP (port 9001)**: Low-latency channel for encrypted voice packets only

---

## Technology Stack

### Server (Rust)

| Component | Technology | Notes |
|-----------|-----------|-------|
| Language | Rust (latest stable) | Memory safety, async performance |
| Async runtime | Tokio (multi-threaded) | |
| Web framework | Axum | WebSocket + REST in one server |
| Database | PostgreSQL 15+ | Users, channels, orgs, roles, sessions |
| ORM | SQLx | Compile-time checked queries |
| Cache | Redis 7+ | Session cache (optional, graceful degradation if unavailable) |
| Auth | Argon2id (passwords) + JWT (sessions) | |
| Crypto | X25519 ECDH + AES-128-GCM (SRTP) | |
| Config | config-rs | YAML + environment variable overlay (`VOIP__*`) |
| Logging | tracing + tracing-subscriber | Structured, leveled logging |
| Metrics | prometheus crate | Exposed on `/metrics` endpoint |
| Containerization | Docker multi-stage build | Final image <200MB |

### Client (C++ / Qt)

| Component | Technology | Notes |
|-----------|-----------|-------|
| Language | C++17 | |
| GUI | Qt 6 (Widgets) | Core, Widgets, Network, WebSockets modules |
| Audio I/O | PortAudio | Cross-platform, callback-based, low-latency |
| Codec | libopus | 48kHz, 20ms frames, 32kbps default |
| Crypto | OpenSSL 3.x | TLS, X25519, AES-GCM |
| Build | CMake 3.20+ | |
| Target | Windows 10/11 (x64) | Static MSVC runtime for portability |

---

## Server Requirements

### 1. Startup and Initialization

- Load config from `config/server.yaml` with environment variable overrides (`VOIP__DATABASE__URL`, etc.)
- Initialize PostgreSQL connection pool (configurable size, default 20)
- Run database migrations automatically on startup (migrations stored in `server/migrations/` directory, numbered sequentially: `001_initial.sql`, `002_sessions.sql`, etc.)
- Initialize in-memory managers (channel manager, voice router, SRTP session manager)
- Start UDP voice server and WebSocket control server concurrently
- Log startup banner with version, ports, and configuration summary
- Support graceful shutdown on SIGTERM/SIGINT (drain active connections with a 10-second timeout, then force-close)
- TLS is optional — if cert/key files are missing, run without TLS and log a warning (do NOT silently accept plaintext voice packets as a fallback)
- Log output to stdout (for Docker log collection). No file-based logging — rely on Docker's logging driver for rotation and persistence.

### 1b. First-Run Bootstrap (NEW)

On first startup (empty database, after migrations run):
1. Check if any organizations exist. If not, enter bootstrap mode.
2. Read bootstrap config from environment variables:
   - `VOIP__BOOTSTRAP__ORG_NAME` (required) — e.g., "My Family"
   - `VOIP__BOOTSTRAP__ORG_TAG` (required) — e.g., "family"
   - `VOIP__BOOTSTRAP__ADMIN_USERNAME` (required) — e.g., "admin"
   - `VOIP__BOOTSTRAP__ADMIN_PASSWORD` (required) — initial admin password
3. Create the organization, admin user (with `is_admin=true`), and a default "General" channel.
4. Log the created org tag and admin username at `info` level. Do NOT log the password.
5. If bootstrap env vars are missing and no orgs exist, log an error with instructions and exit.
6. If orgs already exist, ignore bootstrap env vars entirely (idempotent — safe to leave in `.env`).

### 2. Authentication and Sessions

**Password authentication:**
- Hash passwords with Argon2id (random salt, default params)
- Verify on login, return JWT on success

**JWT tokens:**
- Claims: `sub` (user_id), `org` (org_id), `roles` (role IDs), `exp`, `iat`, `jti` (UUID)
- Configurable expiration (default 1 hour)
- Sign with HS256 using server-configured secret (HS256 is intentional — single-server deployment means no need for asymmetric verification by external services)

**Session management (NEW — v1 gap):**
- On successful auth, create a session record in the database with: user_id, token_jti, issued_at, expires_at, revoked (bool)
- On every authenticated WebSocket message, validate the JWT AND check the session is not revoked. To avoid a database hit per message, cache revocation status in-memory with a 5-second TTL (or in Redis if available). The cache is invalidated immediately when the local server processes a revocation.
- Provide an admin endpoint to revoke a user's sessions (immediate ban enforcement)
- Clean up expired sessions periodically (background task, every 10 minutes)

**Authentication flow:**
```
Client connects via WebSocket
  → Server sends Challenge { methods: ["password", "token"] }
  → Client sends Authenticate { method, username, password/token, org_tag }
  → Server verifies credentials
  → Server creates session record
  → Server initiates SRTP key exchange (X25519)
  → Server sends AuthSuccess { user_id, token, voice_port, channels, users, key_exchange_init }
```

### 3. Channel Management

- Channels belong to an organization (multi-tenant isolation)
- Hierarchical structure: channels can have a parent_id (tree layout)
- Optional channel passwords (hashed with Argon2)
- Per-channel properties: name, description, position (sort order), max_users
- **Enforce max_users_per_channel** — reject join if channel is full (v1 gap: config existed but was never validated)

**Channel state synchronization:**
- When a user joins a channel, broadcast `UserJoined` to all channel members
- When a user leaves, broadcast `UserLeft`
- Send full channel roster on join
- Track user states: online, talking, muted, deafened

### 4. Voice Routing (Critical Path)

This is the performance-critical hot path. Every design decision here affects latency.

**Packet format (28-byte header + encrypted payload):**
```
[Magic: 2 bytes "VL"] [Version: 1 byte] [Flags: 1 byte] [Sequence: 8 bytes] [Timestamp: 8 bytes] [Channel ID: 4 bytes] [User ID: 4 bytes] [Encrypted Opus payload]
```

- **Version**: Protocol version (start at `1`). Allows future packet format changes without breaking existing clients.
- **Flags**: Reserved for future use (e.g., key rotation in progress, priority packet). Set to `0x00` initially.
- **Channel ID / User ID**: Treated as unsigned 32-bit integers in the packet header. Map from database `SERIAL` (i32) by casting — IDs are always positive.

**Routing logic:**
```
Receive UDP packet
  → Validate magic number
  → Look up sender's SRTP session
  → Decrypt payload (REJECT if decryption fails — do NOT fall back to plaintext)
  → Look up channel members
  → For each recipient (excluding sender):
      → Re-encrypt with recipient's SRTP keys
      → Send via UDP
```

**Performance requirements (improvements over v1):**

1. **Parallel re-encryption**: Do NOT encrypt serially in a loop. Use `tokio::spawn` or `futures::join_all` to encrypt and send to all recipients concurrently. Target: constant latency regardless of channel size.

2. **Lock-free or sharded routing tables**: Replace `RwLock<HashMap>` with `DashMap` or similar concurrent map. The v1 design acquired 3+ read locks per voice packet, causing contention under load.

3. **Buffer pooling**: Pre-allocate a pool of `Vec<u8>` buffers for voice packets. Reuse instead of allocating per packet. Eliminates allocation pressure at 2000+ packets/sec.

4. **Rate limiting**: Enforce per-user packet rate limit (e.g., max 100 packets/sec). Drop packets exceeding the limit. Prevents abuse and protects server CPU.

5. **No debug logging on voice packets in release mode**: Use `trace!` level only. The v1 design logged every voice packet at `debug!` level.

**Target performance:**
- <5ms routing latency (receive → all sends complete)
- Support 50+ users in a single channel without latency degradation
- Support 100+ concurrent connections across all channels

### 5. SRTP Encryption

- Key exchange: X25519 ECDH (ephemeral keys per session)
- Derive symmetric keys via HKDF-SHA256 from shared secret
- Encrypt voice payloads with AES-128-GCM (12-byte nonce from sequence number, 16-byte auth tag)
- **Strictly reject packets that fail decryption** — no plaintext fallback under any circumstances
- Support key rotation: re-exchange keys every N minutes (configurable, default 30)
- **Key rotation transition**: During rotation, maintain a brief dual-key window (~2 seconds) where both old and new keys are accepted for decryption. This prevents dropped packets while the new keys propagate. The old key is discarded after the window expires.

### 6. WebSocket Control Messages

All control messages are JSON over WebSocket.

**Message envelope format:**
```json
{
    "type": "message_type_here",
    "payload": { ... }
}
```

All messages use this envelope. The `type` field determines how `payload` is deserialized. Unknown types are ignored (forward compatibility).

**Message types:**

**Client → Server:**
- `authenticate` — login with password or JWT token
- `join_channel` — join a voice channel by `channel_id` (with optional password)
- `leave_channel` — leave a voice channel by `channel_id`
- `key_exchange_response` — client's X25519 public key
- `set_muted` / `set_deafened` — update user state
- `ping` — keepalive (client MUST send every 10 seconds)

**Server-side connection timeout (NEW — v1 gap):**
- Server expects a `ping` from each client at least every 10 seconds
- If no `ping` (or any other message) is received within 30 seconds, the server considers the connection dead and cleans up (removes from channels, broadcasts `UserLeft`, frees SRTP session)
- This prevents resource leaks when clients disappear without a clean TCP close (e.g., network failure, crash)

**Server → Client:**
- `challenge` — authentication methods available
- `auth_success` — login successful (includes JWT, voice_port, channel list, user list, key exchange init)
- `auth_failed` — login failed
- `channel_joined` — confirmation with `channel_id` and channel roster
- `user_joined` / `user_left` — presence updates with `channel_id` and `user_id`
- `user_state_changed` — mute/deafen/talking state with `channel_id` and `user_id`
- `key_exchange_init` — server's X25519 public key (sent both inside `auth_success` for initial setup and standalone during key rotation)
- `channel_list` — full channel roster for the org
- `channel_updated` — a channel's properties changed (name, description, max_users, etc.) — clients update their channel list in-place
- `channel_deleted` — a channel was removed — clients in that channel are force-removed and shown a notification
- `error` — error with code and message
- `key_exchange_complete` — server confirms SRTP keys are ready; client may now send/receive voice packets
- `pong` — keepalive response

### 7. Admin REST API

Mounted on the same Axum server (port 9000), under `/api/admin/`. Each endpoint requires a specific permission bit (or `ADMIN` which implies all).

**WebSocket Origin validation (NEW — security):**
- On WebSocket upgrade requests, validate the `Origin` header. Reject connections from unexpected origins to prevent cross-site WebSocket hijacking. In self-hosted mode, accept connections with no `Origin` header (desktop clients) or from explicitly configured allowed origins.

**Endpoints:**
- `GET /api/admin/users` — list users in org — requires `CAN_MANAGE_USERS`
- `POST /api/admin/users` — create user — requires `CAN_MANAGE_USERS`
- `PUT /api/admin/users/:id` — update user — requires `CAN_MANAGE_USERS`
- `DELETE /api/admin/users/:id` — delete user — requires `CAN_MANAGE_USERS` (if currently connected: revoke all sessions, force-close WebSocket, broadcast `UserLeft` to their channels)
- `POST /api/admin/users/:id/revoke-sessions` — revoke all sessions — requires `CAN_REVOKE_SESSIONS`
- `GET /api/admin/channels` — list channels — requires `CAN_MANAGE_CHANNELS`
- `POST /api/admin/channels` — create channel — requires `CAN_MANAGE_CHANNELS`
- `PUT /api/admin/channels/:id` — update channel — requires `CAN_MANAGE_CHANNELS` (broadcasts `channel_updated` to all connected clients in the org)
- `DELETE /api/admin/channels/:id` — delete channel — requires `CAN_MANAGE_CHANNELS` (broadcasts `channel_deleted` to all connected clients in the org)
- `GET /api/admin/roles` — list roles — requires `CAN_MANAGE_ROLES`
- `POST /api/admin/roles` — create role — requires `CAN_MANAGE_ROLES`
- `PUT /api/admin/roles/:id` — update role — requires `CAN_MANAGE_ROLES`
- `DELETE /api/admin/roles/:id` — delete role — requires `CAN_MANAGE_ROLES`
- `POST /api/admin/roles/:id/assign/:user_id` — assign role to user — requires `CAN_MANAGE_ROLES`
- `DELETE /api/admin/roles/:id/assign/:user_id` — remove role from user — requires `CAN_MANAGE_ROLES`

### 8. Health and Monitoring (NEW — v1 gap)

**Health endpoint** (`GET /health`):
- Check database connectivity (run `SELECT 1`)
- Check Redis connectivity (if configured)
- Return `200 OK` with `{"status":"healthy","version":"2.0.0","uptime_seconds":N}` or `503` with details

**Metrics endpoint** (`GET /metrics`, Prometheus format, no authentication — should be firewalled or bound to internal network in production):
```
voip_active_connections (gauge)
voip_active_channels (gauge)
voip_voice_packets_forwarded_total (counter)
voip_voice_packets_dropped_total (counter, label: reason)
voip_routing_latency_seconds (histogram, buckets: 1ms, 5ms, 10ms, 25ms, 50ms)
voip_websocket_messages_total (counter, label: type)
voip_auth_attempts_total (counter, label: result)
voip_active_sessions (gauge)
```

### 9. Rate Limiting (NEW — v1 gap)

- **WebSocket**: Max 30 messages/sec per connection. Send error and disconnect on sustained abuse.
- **UDP voice**: Max 100 packets/sec per source address. Drop excess silently.
- **Auth attempts**: Max 5 failed attempts per IP per minute, AND max 10 failed attempts per username per minute. Delay responses after 3 failures. (Dual limiting prevents both brute-force from one IP and distributed attacks against one account, while avoiding NAT false positives from IP-only limiting.)
- **Admin API**: Max 60 requests/min per authenticated user.

### 10. Database Schema

```sql
-- Organizations (multi-tenant)
CREATE TABLE organizations (
    id SERIAL PRIMARY KEY,
    name VARCHAR(255) NOT NULL,
    tag VARCHAR(16) UNIQUE NOT NULL,
    owner_id INTEGER,  -- FK to users(id) added via ALTER TABLE after users table is created (circular dependency)
    max_users INTEGER DEFAULT 100,
    max_channels INTEGER DEFAULT 50,
    created_at TIMESTAMPTZ DEFAULT NOW()
);

-- Users (scoped to org)
CREATE TABLE users (
    id SERIAL PRIMARY KEY,
    org_id INTEGER NOT NULL REFERENCES organizations(id),
    username VARCHAR(64) NOT NULL,
    password_hash TEXT NOT NULL,
    display_name VARCHAR(128),
    is_admin BOOLEAN DEFAULT FALSE,
    is_active BOOLEAN DEFAULT TRUE,
    last_login TIMESTAMPTZ,
    created_at TIMESTAMPTZ DEFAULT NOW(),
    UNIQUE(org_id, username)
);

-- Sessions (NEW — for token revocation)
CREATE TABLE sessions (
    id SERIAL PRIMARY KEY,
    user_id INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    token_jti UUID NOT NULL UNIQUE,
    issued_at TIMESTAMPTZ NOT NULL,
    expires_at TIMESTAMPTZ NOT NULL,
    revoked BOOLEAN DEFAULT FALSE,
    revoked_at TIMESTAMPTZ,
    ip_address INET,
    created_at TIMESTAMPTZ DEFAULT NOW()
);
CREATE INDEX idx_sessions_user ON sessions(user_id);
CREATE INDEX idx_sessions_jti ON sessions(token_jti);
CREATE INDEX idx_sessions_expires ON sessions(expires_at) WHERE NOT revoked;

-- Roles
CREATE TABLE roles (
    id SERIAL PRIMARY KEY,
    org_id INTEGER NOT NULL REFERENCES organizations(id),
    name VARCHAR(64) NOT NULL,
    permissions INTEGER NOT NULL DEFAULT 0,  -- bitflag (see Permission Bits below)
    priority INTEGER DEFAULT 0,
    color VARCHAR(7),  -- hex color for display
    created_at TIMESTAMPTZ DEFAULT NOW(),
    UNIQUE(org_id, name)
);

-- User-Role assignments
CREATE TABLE user_roles (
    user_id INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    role_id INTEGER NOT NULL REFERENCES roles(id) ON DELETE CASCADE,
    PRIMARY KEY(user_id, role_id)
);

-- Channels (hierarchical)
CREATE TABLE channels (
    id SERIAL PRIMARY KEY,
    org_id INTEGER NOT NULL REFERENCES organizations(id),
    parent_id INTEGER REFERENCES channels(id),
    name VARCHAR(64) NOT NULL,
    description TEXT,
    password_hash TEXT,
    position INTEGER DEFAULT 0,
    max_users INTEGER DEFAULT 50,
    created_at TIMESTAMPTZ DEFAULT NOW(),
    UNIQUE(org_id, parent_id, name)
);

-- Channel ACLs (per role)
CREATE TABLE channel_acl (
    channel_id INTEGER NOT NULL REFERENCES channels(id) ON DELETE CASCADE,
    role_id INTEGER NOT NULL REFERENCES roles(id) ON DELETE CASCADE,
    allow_join BOOLEAN DEFAULT TRUE,
    allow_speak BOOLEAN DEFAULT TRUE,
    allow_manage BOOLEAN DEFAULT FALSE,
    PRIMARY KEY(channel_id, role_id)
);

-- Audit log
CREATE TABLE audit_log (
    id BIGSERIAL PRIMARY KEY,
    org_id INTEGER REFERENCES organizations(id),
    user_id INTEGER REFERENCES users(id),
    action VARCHAR(64) NOT NULL,
    target_type VARCHAR(32),
    target_id INTEGER,
    details JSONB,
    ip_address INET,
    created_at TIMESTAMPTZ DEFAULT NOW()
);
CREATE INDEX idx_audit_org ON audit_log(org_id, created_at DESC);

-- Deferred FK for organizations.owner_id (resolves circular dependency with users)
ALTER TABLE organizations ADD CONSTRAINT fk_organizations_owner
    FOREIGN KEY (owner_id) REFERENCES users(id) ON DELETE SET NULL;

-- Root-level channel uniqueness (NULL parent_id is not caught by the composite UNIQUE constraint)
CREATE UNIQUE INDEX idx_channels_root_unique ON channels(org_id, name) WHERE parent_id IS NULL;
```

**Permission bits** (used in `roles.permissions` bitflag):

| Bit | Value | Permission | Description |
|-----|-------|-----------|-------------|
| 0 | 1 | `CAN_JOIN` | Join voice channels |
| 1 | 2 | `CAN_SPEAK` | Transmit voice in channels |
| 2 | 4 | `CAN_MANAGE_CHANNELS` | Create, edit, delete channels |
| 3 | 8 | `CAN_MANAGE_USERS` | Create, edit, deactivate users |
| 4 | 16 | `CAN_MANAGE_ROLES` | Create, edit, delete roles and assignments |
| 5 | 32 | `CAN_REVOKE_SESSIONS` | Force-disconnect users |
| 6 | 64 | `CAN_VIEW_AUDIT_LOG` | View audit log entries |
| 7 | 128 | `ADMIN` | Full access (implies all other permissions) |

**ACL enforcement:**
- Channel ACLs (`channel_acl` table) override the role's base permissions for specific channels
- On `join_channel`: check the user's effective `CAN_JOIN` for that channel (role permission AND channel ACL). Reject with error if denied.
- On voice routing: check `allow_speak` from the channel ACL for the sender's role. Drop packets silently if denied — do NOT add this check to the hot path per-packet; instead, cache the result when the user joins the channel and invalidate on ACL change.

**Audit log retention:**
- Run a background task (daily) to delete audit log entries older than 90 days
- Configurable via `VOIP__AUDIT__RETENTION_DAYS` (default 90)

---

## Client Requirements

### 1. Application Lifecycle

**Startup:**
1. Load configuration from `config.json` (fall back to defaults if missing)
2. Show login dialog (pre-fill saved credentials if "remember me" was checked)
3. On successful login, show main window
4. Connect WebSocket, authenticate, receive channel list
5. Auto-join last-used channel (saved in config) — if the channel no longer exists, show the channel list without auto-joining and log "Previous channel unavailable"
6. Initialize audio engine and voice session

**Shutdown:**
1. Stop voice session (stop audio streams)
2. Send leave messages for all channels
3. Close WebSocket gracefully
4. Save current config (last server, last channel, window position)
5. Clean up resources

### 2. Login Dialog

- Fields: Server address, port, organization tag, username, password
- Checkbox: "Use TLS" (default on)
- Checkbox: "Remember me" (stores username and last-issued JWT token in config — never stores the password locally; on next launch, attempts token-based re-auth, falls back to password prompt if token is expired/revoked)
- **Show a progress spinner while connecting** (v1 gap: UI appeared frozen during connect)
- Display clear error messages on failure (not raw error codes)
- Validate inputs before attempting connection

### 3. Main Window Layout

```
┌─────────────────────────────────────────────────────┐
│  Menu Bar: [File] [Settings] [Help]                 │
├─────────────────────────────────────────────────────┤
│  User Info: "Logged in as: username @ org"          │
├───────────┬──────────────────┬──────────────────────┤
│ Channels  │  Channel Users   │  Activity Log        │
│           │                  │                      │
│ #general  │  🔊 Alice        │  [12:01] Bob joined  │
│   [PTT:F1]│  🔇 Bob (muted)  │  [12:02] Alice PTT   │
│ #team-1   │  🎤 Charlie (tx) │  [12:03] Connected   │
│   [PTT:F2]│                  │                      │
│ #team-2   │                  │                      │
│   [PTT:F3]│                  │                      │
├───────────┴──────────────────┴──────────────────────┤
│  Voice Controls:                                    │
│  [🔇 Mute] [🔇 Deafen] [⚙ Settings] [❌ Disconnect]│
│  Input: ████████░░░░  Output: ██████░░░░░░          │
│  PTT: Idle | Latency: 45ms | Quality: Good         │
│  Input Vol: [====|======] Output Vol: [======|====] │
├─────────────────────────────────────────────────────┤
│  Status Bar: Connected to 192.168.1.100:9000 (TLS)  │
└─────────────────────────────────────────────────────┘
```

### 4. Audio Engine

**PortAudio configuration:**
- Sample rate: 48000 Hz
- Frame size: 960 samples (20ms)
- Format: 32-bit float, mono
- Use default low-latency device settings

**Real-time thread safety (CRITICAL — v1 bug fix):**
- Audio capture and playback callbacks run on PortAudio's real-time thread
- These callbacks MUST NOT: allocate memory, acquire mutexes, perform I/O, call system functions
- Use only: atomic variables, lock-free ring buffers (SPSC queues), pre-allocated buffers
- **Replace all `std::mutex` usage in audio callbacks with atomics or lock-free structures**
- For PTT channel state in the audio callback: use `std::atomic<uint64_t>` as a bitfield (each bit = one channel's PTT state) instead of a mutex-protected `std::set`

**Volume control (v1 gap — wire it up):**
- Input volume: multiply captured samples by `input_volume_` (atomic float, range 0.0-2.0) before encoding
- Output volume: multiply decoded samples by `output_volume_` (atomic float, range 0.0-2.0) before playback
- Connect UI sliders to these atomic values
- Volume changes take effect immediately (next audio callback)

**Device management:**
- Enumerate input/output devices on startup
- Allow device selection in settings dialog
- **Detect device disconnection and fall back to system default** (v1 gap)
- Show notification when device changes

### 5. Opus Codec

- Encoder: 48kHz, mono, 32kbps, complexity 10, VOIP application mode
- Enable FEC (Forward Error Correction) for packet loss resilience
- Enable DTX (Discontinuous Transmission) to save bandwidth during silence
- Decoder: matching config, with PLC (Packet Loss Concealment) enabled

### 6. Voice Session Pipeline

**Transmit path:**
```
Audio callback captures 960 samples (20ms)
  → Write to lock-free ring buffer (SPSC queue)
  → Encoder thread reads from ring buffer
  → Encode with Opus → ~40-80 bytes
  → Encrypt with SRTP (AES-128-GCM)
  → Build voice packet (28-byte header + encrypted payload)
  → Send via UDP socket
```

**Receive path:**
```
UDP socket receives packet (background thread)
  → Validate magic number, parse header
  → Decrypt with sender's SRTP session
  → Insert into per-channel jitter buffer (20-40ms adaptive)
  → Jitter buffer outputs ordered frames
  → Decode with Opus (PLC for missing frames)
  → Mix channels → write to playback ring buffer
  → Audio callback reads from ring buffer → speakers
```

**Multi-channel support:**
- User can listen to multiple channels simultaneously
- One jitter buffer per active channel (independently tracks sequence numbers and timing per source)
- Audio from multiple channels is mixed via simple summation before playback, with hard clipping at [-1.0, 1.0] to prevent overflow
- PTT transmits to one channel at a time (determined by which hotkey is held)
- Hot-mic mode: always transmit to a designated channel (overridden by PTT)

### 7. Push-to-Talk / Hotkeys

- Global keyboard hook (`SetWindowsHookEx` with `WH_KEYBOARD_LL`)
- Works even when the application is not focused
- Per-channel hotkey assignment (e.g., F1 for #general, F2 for #team-1)
- Support: F1-F12, number keys, numpad keys, and modifier combos
- Collision detection: prevent assigning the same key to two channels
- Visual indicator: PTT status label changes color (green = transmitting, gray = idle)

### 8. WebSocket Client

- Use Qt's `QWebSocket` for WebSocket communication
- All server callbacks marshaled to the GUI thread via `QMetaObject::invokeMethod(Qt::QueuedConnection)`
- JSON message serialization/deserialization

**Auto-reconnection (NEW — v1 gap):**
- On unexpected disconnect, immediately show "Reconnecting..." status
- Retry with exponential backoff: 1s, 2s, 4s, 8s, 16s, 30s (cap at 30s)
- On reconnect: re-authenticate with saved JWT token (if not expired) or saved credentials
- Re-join previously joined channels
- After 5 minutes of failed reconnection, show "Connection lost" dialog with manual retry button
- Cancel reconnection if user clicks Disconnect

**TLS/SSL (v1 bug fix):**
- Do NOT call `ignoreSslErrors()` unconditionally
- For self-signed certificates: prompt the user to accept the certificate on first connection, then pin it (store fingerprint in config)
- For valid certificates: validate normally via Qt's SSL stack
- Show lock icon in status bar when TLS is active

### 9. Encryption (Client-Side)

**Key exchange:**
1. On auth success, server sends its X25519 public key
2. Client generates ephemeral X25519 keypair
3. Client performs ECDH to derive shared secret
4. Client derives SRTP keys via HKDF-SHA256 (16-byte master key + 14-byte salt)
5. Client creates SRTP session with derived keys
6. Client sends its public key back to server
7. Server derives shared secret, creates its SRTP session, and sends `key_exchange_complete`
8. Client begins sending/receiving encrypted voice packets only after receiving `key_exchange_complete`

**SRTP session:**
- AES-128-GCM encryption/decryption
- Nonce derived from packet sequence number (prevents replay)
- Auth tag appended to each packet (integrity verification)
- **Reject packets that fail authentication** — do not play corrupted audio

### 10. Configuration File

Load from `config.json` in the application directory. Create with defaults if missing.

```json
{
    "server": {
        "address": "",
        "port": 9000,
        "voice_port": 9001,
        "org_tag": "",
        "use_tls": true,
        "tls_pinned_certs": {}
    },
    "user": {
        "username": "",
        "remember_me": false,
        "saved_token": null,
        "last_channel_id": null
    },
    "voice": {
        "hot_mic_enabled": false,
        "hot_mic_channel_id": null
    },
    "audio": {
        "input_device_id": -1,
        "output_device_id": -1,
        "input_volume": 1.0,
        "output_volume": 1.0,
        "noise_gate_threshold": 0.01
    },
    "opus": {
        "bitrate": 32000,
        "complexity": 10,
        "enable_fec": true,
        "enable_dtx": true
    },
    "hotkeys": {},
    "ui": {
        "theme": "dark",
        "window_x": 100,
        "window_y": 100,
        "window_width": 900,
        "window_height": 600,
        "show_activity_log": true
    }
}
```

All settings should be loaded on startup and saved on shutdown (and on settings dialog close).

### 11. Admin Panel

Built into the main window as a tab (visible only to users with admin role).

**Tabs:**
- **Users**: Table with columns (username, display name, roles, last login, active). Buttons: Add, Edit, Delete, Revoke Sessions.
- **Channels**: Tree view reflecting hierarchy. Buttons: Add, Edit, Delete, Reorder.
- **Roles**: Table with columns (name, permissions, color, priority). Buttons: Add, Edit, Delete.
- **Role Assignment**: Select user → see their roles → add/remove roles.

All operations call the admin REST API and refresh the view on success.

### 12. Error Handling and User Feedback

- **Activity log**: Timestamped entries with emoji category markers (🎤 audio, 🌐 network, 🔑 crypto, ⚠️ warning, ❌ error)
- **Status bar**: Connection state, latency, voice quality indicator
- **Dialog boxes**: Only for critical errors requiring user action (not for routine events)
- **Toast-style notifications**: For transient events (user joined, device changed) — use QSystemTrayIcon or custom widget
- Cap activity log at 1000 entries (remove oldest)
- **Never show raw error codes or stack traces to the user** — translate to human-readable messages

---

## Deployment

### Docker Compose (Production)

```yaml
services:
  postgres:
    image: postgres:15-alpine
    restart: unless-stopped
    environment:
      POSTGRES_DB: voip
      POSTGRES_USER: voip
      POSTGRES_PASSWORD: ${POSTGRES_PASSWORD}
    volumes:
      - postgres_data:/var/lib/postgresql/data
    healthcheck:
      test: ["CMD-SHELL", "pg_isready -U voip"]
      interval: 10s
      timeout: 5s
      retries: 5

  voip-server:
    build:
      context: ./server
      dockerfile: Dockerfile
    restart: unless-stopped
    depends_on:
      postgres:
        condition: service_healthy
    environment:
      VOIP__DATABASE__URL: postgresql://voip:${POSTGRES_PASSWORD}@postgres:5432/voip
      VOIP__DATABASE__MAX_CONNECTIONS: 20
      VOIP__SERVER__BIND_ADDRESS: 0.0.0.0
      VOIP__SERVER__CONTROL_PORT: 9000
      VOIP__SERVER__VOICE_PORT: 9001
      VOIP__SECURITY__JWT_SECRET: ${JWT_SECRET}
      VOIP__BOOTSTRAP__ORG_NAME: ${BOOTSTRAP_ORG_NAME:-}
      VOIP__BOOTSTRAP__ORG_TAG: ${BOOTSTRAP_ORG_TAG:-}
      VOIP__BOOTSTRAP__ADMIN_USERNAME: ${BOOTSTRAP_ADMIN_USERNAME:-}
      VOIP__BOOTSTRAP__ADMIN_PASSWORD: ${BOOTSTRAP_ADMIN_PASSWORD:-}
      RUST_LOG: voip_server=info
    ports:
      - "9000:9000"
      - "9001:9001/udp"
    healthcheck:
      test: ["CMD", "/app/healthcheck"]  # Compiled into server binary — avoids curl/wget dependency in minimal Docker image
      interval: 30s
      timeout: 10s
      retries: 3
      start_period: 30s

volumes:
  postgres_data:
```

**Required `.env` file:**
```
POSTGRES_PASSWORD=<generated-random-string>
JWT_SECRET=<generated-random-string-at-least-48-chars>

# Bootstrap (only needed on first run — ignored after org exists)
BOOTSTRAP_ORG_NAME=My Family
BOOTSTRAP_ORG_TAG=family
BOOTSTRAP_ADMIN_USERNAME=admin
BOOTSTRAP_ADMIN_PASSWORD=<change-me-on-first-login>
```

### Database Backup

- Schedule daily `pg_dump` backups via a cron job or a sidecar container (e.g., `prodrigestivill/postgres-backup-local`)
- Retain last 7 daily backups in a mounted volume (`postgres_backups:/backups`)
- Document restore procedure in deployment README

### Target Platforms
- **Server**: Windows Server 2019+ (via Docker Desktop) or any Linux with Docker
- **Client**: Windows 10/11 x64 (portable — no installer required, no VC++ redistributable)

---

## Quality Attributes (Priority Order)

### 1. Reliability
- Server must not crash on malformed packets, invalid JSON, or unexpected disconnects
- All errors handled gracefully — never `unwrap()` in production paths
- Auto-reconnection in client
- Token revocation for immediate user bans
- Reject all unauthenticated/unencrypted traffic

### 2. Usability
- Login-to-talking in under 30 seconds
- PTT "just works" — no audio setup wizard needed
- Clear visual feedback for all state changes
- Config file loaded and saved automatically
- All UI controls actually functional (no decorative widgets)

### 3. Efficiency
- Voice routing latency under 5ms (server-side)
- End-to-end voice latency under 150ms
- No mutex locks on real-time audio threads
- Parallel voice packet re-encryption
- Minimal memory allocation in the voice hot path

### 4. Security
- No plaintext fallback — encryption is mandatory
- Proper TLS certificate validation
- Rate limiting on all entry points
- Audit logging for admin actions
- Passwords never stored in plaintext (Argon2id)

---

## What NOT to Build (Scope Boundaries)

- No mobile clients (iOS/Android) — future version
- No video calling — future version
- No screen sharing — future version
- No file transfers — future version
- No voice activity detection (VAD) — PTT only for now
- No web client — desktop only
- No multi-server clustering — single server deployment
- No Redis requirement — make it optional with graceful degradation
