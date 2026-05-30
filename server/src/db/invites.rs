use chrono::{DateTime, Utc};
use sqlx::PgPool;

use super::models::Invite;

pub async fn create_invite(
    pool: &PgPool,
    org_id: i32,
    created_by: i32,
    code: &str,
    max_uses: Option<i32>,
    expires_at: Option<DateTime<Utc>>,
) -> anyhow::Result<Invite> {
    let invite = sqlx::query_as::<_, Invite>(
        r#"
        INSERT INTO invites (org_id, created_by, code, max_uses, expires_at)
        VALUES ($1, $2, $3, $4, $5)
        RETURNING *
        "#,
    )
    .bind(org_id)
    .bind(created_by)
    .bind(code)
    .bind(max_uses)
    .bind(expires_at)
    .fetch_one(pool)
    .await?;

    Ok(invite)
}

pub async fn find_by_code(pool: &PgPool, code: &str) -> anyhow::Result<Option<Invite>> {
    let invite = sqlx::query_as::<_, Invite>(
        "SELECT * FROM invites WHERE code = $1",
    )
    .bind(code)
    .fetch_optional(pool)
    .await?;

    Ok(invite)
}

pub async fn list_by_org(pool: &PgPool, org_id: i32) -> anyhow::Result<Vec<Invite>> {
    let invites = sqlx::query_as::<_, Invite>(
        "SELECT * FROM invites WHERE org_id = $1 ORDER BY created_at DESC",
    )
    .bind(org_id)
    .fetch_all(pool)
    .await?;

    Ok(invites)
}

pub async fn increment_use(pool: &PgPool, code: &str) -> anyhow::Result<()> {
    sqlx::query("UPDATE invites SET use_count = use_count + 1 WHERE code = $1")
        .bind(code)
        .execute(pool)
        .await?;

    Ok(())
}

pub async fn delete_by_code(pool: &PgPool, org_id: i32, code: &str) -> anyhow::Result<bool> {
    let result = sqlx::query(
        "DELETE FROM invites WHERE code = $1 AND org_id = $2",
    )
    .bind(code)
    .bind(org_id)
    .execute(pool)
    .await?;

    Ok(result.rows_affected() > 0)
}
