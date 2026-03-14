-- Organizations (multi-tenant)
CREATE TABLE organizations (
    id SERIAL PRIMARY KEY,
    name VARCHAR(255) NOT NULL,
    tag VARCHAR(16) UNIQUE NOT NULL,
    owner_id INTEGER,  -- FK added via ALTER TABLE after users table
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

-- Deferred FK for organizations.owner_id
ALTER TABLE organizations ADD CONSTRAINT fk_organizations_owner
    FOREIGN KEY (owner_id) REFERENCES users(id) ON DELETE SET NULL;

-- Roles
CREATE TABLE roles (
    id SERIAL PRIMARY KEY,
    org_id INTEGER NOT NULL REFERENCES organizations(id),
    name VARCHAR(64) NOT NULL,
    permissions INTEGER NOT NULL DEFAULT 0,
    priority INTEGER DEFAULT 0,
    color VARCHAR(7),
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

-- Root-level channel uniqueness (NULL parent_id not caught by composite UNIQUE)
CREATE UNIQUE INDEX idx_channels_root_unique ON channels(org_id, name) WHERE parent_id IS NULL;

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
