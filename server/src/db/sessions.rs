use chrono::{DateTime, Utc};
use sqlx::PgPool;
use uuid::Uuid;

use super::models::Session;

pub async fn create_session(
    pool: &PgPool,
    user_id: i32,
    token_jti: Uuid,
    issued_at: DateTime<Utc>,
    expires_at: DateTime<Utc>,
    ip_address: Option<&str>,
) -> anyhow::Result<Session> {
    let session = sqlx::query_as::<_, Session>(
        r#"
        INSERT INTO sessions (user_id, token_jti, issued_at, expires_at, ip_address)
        VALUES ($1, $2, $3, $4, $5::INET)
        RETURNING *
        "#,
    )
    .bind(user_id)
    .bind(token_jti)
    .bind(issued_at)
    .bind(expires_at)
    .bind(ip_address)
    .fetch_one(pool)
    .await?;

    Ok(session)
}

pub async fn find_by_jti(pool: &PgPool, jti: Uuid) -> anyhow::Result<Option<Session>> {
    let session = sqlx::query_as::<_, Session>(
        "SELECT * FROM sessions WHERE token_jti = $1",
    )
    .bind(jti)
    .fetch_optional(pool)
    .await?;

    Ok(session)
}

pub async fn is_revoked(pool: &PgPool, jti: Uuid) -> anyhow::Result<bool> {
    let result = sqlx::query_scalar::<_, bool>(
        "SELECT COALESCE(revoked, FALSE) FROM sessions WHERE token_jti = $1",
    )
    .bind(jti)
    .fetch_optional(pool)
    .await?;

    // If session not found, treat as revoked
    Ok(result.unwrap_or(true))
}

pub async fn revoke_session(pool: &PgPool, jti: Uuid) -> anyhow::Result<()> {
    sqlx::query(
        "UPDATE sessions SET revoked = TRUE, revoked_at = NOW() WHERE token_jti = $1",
    )
    .bind(jti)
    .execute(pool)
    .await?;

    Ok(())
}

pub async fn revoke_all_for_user(pool: &PgPool, user_id: i32) -> anyhow::Result<u64> {
    let result = sqlx::query(
        "UPDATE sessions SET revoked = TRUE, revoked_at = NOW() WHERE user_id = $1 AND revoked = FALSE",
    )
    .bind(user_id)
    .execute(pool)
    .await?;

    Ok(result.rows_affected())
}

pub async fn cleanup_expired(pool: &PgPool) -> anyhow::Result<u64> {
    let result = sqlx::query(
        "DELETE FROM sessions WHERE expires_at < NOW()",
    )
    .execute(pool)
    .await?;

    Ok(result.rows_affected())
}
