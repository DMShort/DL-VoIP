-- Invite codes for user self-registration
CREATE TABLE invites (
    id SERIAL PRIMARY KEY,
    org_id INTEGER NOT NULL REFERENCES organizations(id),
    code VARCHAR(32) UNIQUE NOT NULL,
    created_by INTEGER NOT NULL REFERENCES users(id),
    max_uses INTEGER,            -- NULL = unlimited
    use_count INTEGER NOT NULL DEFAULT 0,
    expires_at TIMESTAMPTZ,      -- NULL = never expires
    created_at TIMESTAMPTZ DEFAULT NOW()
);
CREATE INDEX idx_invites_org ON invites(org_id);
