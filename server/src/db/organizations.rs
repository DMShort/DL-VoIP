use sqlx::PgPool;

use super::models::Organization;

pub async fn create_org(
    pool: &PgPool,
    name: &str,
    tag: &str,
) -> anyhow::Result<Organization> {
    let org = sqlx::query_as::<_, Organization>(
        r#"
        INSERT INTO organizations (name, tag)
        VALUES ($1, $2)
        RETURNING *
        "#,
    )
    .bind(name)
    .bind(tag)
    .fetch_one(pool)
    .await?;

    Ok(org)
}

pub async fn set_owner(pool: &PgPool, org_id: i32, owner_id: i32) -> anyhow::Result<()> {
    sqlx::query("UPDATE organizations SET owner_id = $1 WHERE id = $2")
        .bind(owner_id)
        .bind(org_id)
        .execute(pool)
        .await?;
    Ok(())
}

pub async fn find_by_tag(pool: &PgPool, tag: &str) -> anyhow::Result<Option<Organization>> {
    let org = sqlx::query_as::<_, Organization>(
        "SELECT * FROM organizations WHERE tag = $1",
    )
    .bind(tag)
    .fetch_optional(pool)
    .await?;

    Ok(org)
}

pub async fn any_exist(pool: &PgPool) -> anyhow::Result<bool> {
    let count = sqlx::query_scalar::<_, i64>(
        "SELECT COUNT(*) FROM organizations",
    )
    .fetch_one(pool)
    .await?;

    Ok(count > 0)
}
