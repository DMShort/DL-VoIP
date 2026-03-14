use sqlx::PgPool;

use super::models::User;

pub async fn create_user(
    pool: &PgPool,
    org_id: i32,
    username: &str,
    password_hash: &str,
    display_name: Option<&str>,
    is_admin: bool,
) -> anyhow::Result<User> {
    let user = sqlx::query_as::<_, User>(
        r#"
        INSERT INTO users (org_id, username, password_hash, display_name, is_admin)
        VALUES ($1, $2, $3, $4, $5)
        RETURNING *
        "#,
    )
    .bind(org_id)
    .bind(username)
    .bind(password_hash)
    .bind(display_name)
    .bind(is_admin)
    .fetch_one(pool)
    .await?;

    Ok(user)
}

pub async fn find_by_username_and_org(
    pool: &PgPool,
    username: &str,
    org_tag: &str,
) -> anyhow::Result<Option<User>> {
    let user = sqlx::query_as::<_, User>(
        r#"
        SELECT u.* FROM users u
        JOIN organizations o ON u.org_id = o.id
        WHERE u.username = $1 AND o.tag = $2 AND u.is_active = TRUE
        "#,
    )
    .bind(username)
    .bind(org_tag)
    .fetch_optional(pool)
    .await?;

    Ok(user)
}

pub async fn find_by_id(pool: &PgPool, user_id: i32) -> anyhow::Result<Option<User>> {
    let user = sqlx::query_as::<_, User>(
        "SELECT * FROM users WHERE id = $1",
    )
    .bind(user_id)
    .fetch_optional(pool)
    .await?;

    Ok(user)
}

pub async fn update_last_login(pool: &PgPool, user_id: i32) -> anyhow::Result<()> {
    sqlx::query("UPDATE users SET last_login = NOW() WHERE id = $1")
        .bind(user_id)
        .execute(pool)
        .await?;
    Ok(())
}

pub async fn list_by_org(pool: &PgPool, org_id: i32) -> anyhow::Result<Vec<User>> {
    let users = sqlx::query_as::<_, User>(
        "SELECT * FROM users WHERE org_id = $1 ORDER BY username",
    )
    .bind(org_id)
    .fetch_all(pool)
    .await?;

    Ok(users)
}
