use sqlx::PgPool;

use super::models::Channel;

pub async fn list_by_org(pool: &PgPool, org_id: i32) -> anyhow::Result<Vec<Channel>> {
    let channels = sqlx::query_as::<_, Channel>(
        "SELECT * FROM channels WHERE org_id = $1 ORDER BY position, name",
    )
    .bind(org_id)
    .fetch_all(pool)
    .await?;

    Ok(channels)
}

pub async fn get_by_id(pool: &PgPool, channel_id: i32) -> anyhow::Result<Option<Channel>> {
    let channel = sqlx::query_as::<_, Channel>(
        "SELECT * FROM channels WHERE id = $1",
    )
    .bind(channel_id)
    .fetch_optional(pool)
    .await?;

    Ok(channel)
}

pub async fn create(
    pool: &PgPool,
    org_id: i32,
    name: &str,
    description: Option<&str>,
    parent_id: Option<i32>,
    max_users: i32,
    position: i32,
    password_hash: Option<&str>,
) -> anyhow::Result<Channel> {
    let channel = sqlx::query_as::<_, Channel>(
        r#"
        INSERT INTO channels (org_id, name, description, parent_id, max_users, position, password_hash)
        VALUES ($1, $2, $3, $4, $5, $6, $7)
        RETURNING *
        "#,
    )
    .bind(org_id)
    .bind(name)
    .bind(description)
    .bind(parent_id)
    .bind(max_users)
    .bind(position)
    .bind(password_hash)
    .fetch_one(pool)
    .await?;

    Ok(channel)
}

pub async fn update(
    pool: &PgPool,
    channel_id: i32,
    name: &str,
    description: Option<&str>,
    max_users: i32,
    position: i32,
) -> anyhow::Result<Channel> {
    let channel = sqlx::query_as::<_, Channel>(
        r#"
        UPDATE channels SET name = $2, description = $3, max_users = $4, position = $5
        WHERE id = $1
        RETURNING *
        "#,
    )
    .bind(channel_id)
    .bind(name)
    .bind(description)
    .bind(max_users)
    .bind(position)
    .fetch_one(pool)
    .await?;

    Ok(channel)
}

pub async fn delete(pool: &PgPool, channel_id: i32) -> anyhow::Result<()> {
    sqlx::query("DELETE FROM channels WHERE id = $1")
        .bind(channel_id)
        .execute(pool)
        .await?;
    Ok(())
}
