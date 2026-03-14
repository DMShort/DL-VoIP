use serde_json::Value as JsonValue;
use sqlx::PgPool;

use super::models::AuditEntry;

pub async fn log_action(
    pool: &PgPool,
    org_id: i32,
    user_id: i32,
    action: &str,
    target_type: Option<&str>,
    target_id: Option<i32>,
    details: Option<JsonValue>,
    ip_address: Option<&str>,
) -> anyhow::Result<()> {
    sqlx::query(
        r#"
        INSERT INTO audit_log (org_id, user_id, action, target_type, target_id, details, ip_address)
        VALUES ($1, $2, $3, $4, $5, $6, $7::inet)
        "#,
    )
    .bind(org_id)
    .bind(user_id)
    .bind(action)
    .bind(target_type)
    .bind(target_id)
    .bind(details)
    .bind(ip_address)
    .execute(pool)
    .await?;

    Ok(())
}

pub async fn list_recent(
    pool: &PgPool,
    org_id: i32,
    limit: i64,
    offset: i64,
) -> anyhow::Result<Vec<AuditEntry>> {
    let entries = sqlx::query_as::<_, AuditEntry>(
        r#"
        SELECT * FROM audit_log
        WHERE org_id = $1
        ORDER BY created_at DESC
        LIMIT $2 OFFSET $3
        "#,
    )
    .bind(org_id)
    .bind(limit)
    .bind(offset)
    .fetch_all(pool)
    .await?;

    Ok(entries)
}

pub async fn cleanup_old(pool: &PgPool, retention_days: i32) -> anyhow::Result<u64> {
    let result = sqlx::query(
        r#"
        DELETE FROM audit_log
        WHERE created_at < NOW() - ($1 || ' days')::INTERVAL
        "#,
    )
    .bind(retention_days.to_string())
    .execute(pool)
    .await?;

    Ok(result.rows_affected())
}
