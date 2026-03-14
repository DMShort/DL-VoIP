use sqlx::PgPool;

use super::models::Role;

pub async fn list_by_org(pool: &PgPool, org_id: i32) -> anyhow::Result<Vec<Role>> {
    let roles = sqlx::query_as::<_, Role>(
        "SELECT * FROM roles WHERE org_id = $1 ORDER BY priority DESC, name",
    )
    .bind(org_id)
    .fetch_all(pool)
    .await?;

    Ok(roles)
}

pub async fn get_by_id(pool: &PgPool, role_id: i32) -> anyhow::Result<Option<Role>> {
    let role = sqlx::query_as::<_, Role>(
        "SELECT * FROM roles WHERE id = $1",
    )
    .bind(role_id)
    .fetch_optional(pool)
    .await?;

    Ok(role)
}

pub async fn create(
    pool: &PgPool,
    org_id: i32,
    name: &str,
    permissions: i32,
    priority: i32,
    color: Option<&str>,
) -> anyhow::Result<Role> {
    let role = sqlx::query_as::<_, Role>(
        r#"
        INSERT INTO roles (org_id, name, permissions, priority, color)
        VALUES ($1, $2, $3, $4, $5)
        RETURNING *
        "#,
    )
    .bind(org_id)
    .bind(name)
    .bind(permissions)
    .bind(priority)
    .bind(color)
    .fetch_one(pool)
    .await?;

    Ok(role)
}

pub async fn update(
    pool: &PgPool,
    role_id: i32,
    name: &str,
    permissions: i32,
    priority: i32,
    color: Option<&str>,
) -> anyhow::Result<Role> {
    let role = sqlx::query_as::<_, Role>(
        r#"
        UPDATE roles SET name = $2, permissions = $3, priority = $4, color = $5
        WHERE id = $1
        RETURNING *
        "#,
    )
    .bind(role_id)
    .bind(name)
    .bind(permissions)
    .bind(priority)
    .bind(color)
    .fetch_one(pool)
    .await?;

    Ok(role)
}

pub async fn delete(pool: &PgPool, role_id: i32) -> anyhow::Result<()> {
    sqlx::query("DELETE FROM roles WHERE id = $1")
        .bind(role_id)
        .execute(pool)
        .await?;
    Ok(())
}

/// Assign a role to a user.
pub async fn assign_role(pool: &PgPool, user_id: i32, role_id: i32) -> anyhow::Result<()> {
    sqlx::query(
        r#"
        INSERT INTO user_roles (user_id, role_id)
        VALUES ($1, $2)
        ON CONFLICT (user_id, role_id) DO NOTHING
        "#,
    )
    .bind(user_id)
    .bind(role_id)
    .execute(pool)
    .await?;
    Ok(())
}

/// Remove a role from a user.
pub async fn remove_role(pool: &PgPool, user_id: i32, role_id: i32) -> anyhow::Result<()> {
    sqlx::query("DELETE FROM user_roles WHERE user_id = $1 AND role_id = $2")
        .bind(user_id)
        .bind(role_id)
        .execute(pool)
        .await?;
    Ok(())
}

/// Get all role IDs for a user.
pub async fn get_user_role_ids(pool: &PgPool, user_id: i32) -> anyhow::Result<Vec<i32>> {
    let rows: Vec<(i32,)> = sqlx::query_as(
        "SELECT role_id FROM user_roles WHERE user_id = $1",
    )
    .bind(user_id)
    .fetch_all(pool)
    .await?;

    Ok(rows.into_iter().map(|(id,)| id).collect())
}

/// Get the permission values of all roles assigned to a user.
pub async fn get_user_permissions(pool: &PgPool, user_id: i32) -> anyhow::Result<Vec<i32>> {
    let rows: Vec<(i32,)> = sqlx::query_as(
        r#"
        SELECT r.permissions FROM roles r
        JOIN user_roles ur ON r.id = ur.role_id
        WHERE ur.user_id = $1
        "#,
    )
    .bind(user_id)
    .fetch_all(pool)
    .await?;

    Ok(rows.into_iter().map(|(p,)| p).collect())
}

/// Get all roles assigned to a user (full objects).
pub async fn get_user_roles(pool: &PgPool, user_id: i32) -> anyhow::Result<Vec<Role>> {
    let roles = sqlx::query_as::<_, Role>(
        r#"
        SELECT r.* FROM roles r
        JOIN user_roles ur ON r.id = ur.role_id
        WHERE ur.user_id = $1
        ORDER BY r.priority DESC
        "#,
    )
    .bind(user_id)
    .fetch_all(pool)
    .await?;

    Ok(roles)
}
